/*
 * src/task/main.cc
 * 用户态任务管理服务（task server）。
 *
 * 它处在启动链的早期：init 启动它，它再拉起并监护 pipe/msgq 等子服务，
 * 之后才注册自己的业务端点，接受其他进程的 Execute / ExecuteService / Wait
 * 请求。真正的进程创建、地址空间切换和资源回收仍由内核 task.kernel 完成；
 * 本文件负责“读哪个文件、参数怎么解析、以什么身份启动”这些策略。
 *
 * 关键约束：
 *   1. 普通 Execute 不接受能力或服务角色；只有 Init 能发 ExecuteService。
 *   2. 可执行文件必须是非空普通文件，且不超过 8 MiB。
 *   3. 请求里的句柄、路径和参数都来自调用者，解析时必须做边界检查，
 *      不能假设消息一定合法。
 *
 * 建议阅读顺序：
 *   TaskServer::run（请求分派）→ parseArguments（参数格式）→
 *   loadAndSpawn（读 ELF、准备 SpawnImageInfo）→ 内核 SpawnImage。
 */

#include "service/common/kernel-requests.h"
#include "service/common/lifecycle.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"
#include "service/task/core_dump.h"
#include <os/console.h>
#include <os/device.h>
#include <os/elf.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/pty.h>
#include <os/service.h>
#include <os/task.h>

// task 服务的请求处理对象。整个服务是单线程循环：主线程一次处理一个请求，
// 读文件、解析 ELF、调用内核 SpawnImage 都在这条线程上完成；pipe/msgq 的
// 监护由 Lifecycle 辅助类负责，不占用这里的请求循环。
class TaskServer {
    public:
        // 一条消息最多携带 64 个参数（路径本身也算在 values[0] 里）。
        static constexpr uint32_t maximumArgumentCount = 64;
        // data[0] 之后还能放参数/路径字节的字数；再大就超过 IPC 负载上限。
        static constexpr uint32_t executePayloadSize =
            (os::ipcDataWordCount - 1) * sizeof(uint64_t);
        // 单个可执行文件/共享库的上限，避免一次请求占满内核和服务内存。
        static constexpr uint64_t maximumExecutableSize = 8ULL * 1024 * 1024;
        // 目前只支持一个共享映像（解释器）；数组大小集中在这里，便于以后扩展。
        static constexpr uint32_t sharedImageCount = 1;

        /*
         * 解析 Execute 请求的负载：data[1..] 是一串 NUL 结尾的字符串，
         * 依次为“绝对路径、参数 1、参数 2……”。data[0] 低 32 位是参数个数。
         * 成功时 path 指向路径，arguments 指向参数数组，argumentCount
         * 是参数数。 失败返回 false（长度非法、没有 NUL
         * 结尾、参数过多、路径不是绝对路径）。
         * 注意：这里只做格式校验，不判断文件是否存在、是否可执行。
         */
        bool parseArguments(const os::IpcMessage& request, const char*& path,
                            const char* (&arguments)[maximumArgumentCount],
                            uint32_t& argumentCount) {
            argumentCount = static_cast<uint32_t>(request.data[0]);

            if (request.length < 2 || request.length > os::ipcDataWordCount) {
                return false;
            }

            if (argumentCount == 0 || argumentCount > maximumArgumentCount) {
                return false;
            }

            const char* payload =
                reinterpret_cast<const char*>(&request.data[1]);
            const uint32_t payloadSize =
                (request.length - 1) * sizeof(uint64_t);
            const char* values[maximumArgumentCount + 1]{};
            uint32_t offset = 0;

            for (uint32_t index = 0; index <= argumentCount; ++index) {
                if (offset >= payloadSize) {
                    return false;
                }

                values[index] = payload + offset;

                while (offset < payloadSize && payload[offset] != '\0') {
                    ++offset;
                }

                if (offset == payloadSize) {
                    return false;
                }

                ++offset;
            }

            path = values[0];

            for (uint32_t index = 0; index < argumentCount; ++index) {
                arguments[index] = values[index + 1];
            }

            return path[0] == '/';
        }

        /*
         * 判断一个参数是不是内部使用的终端选择参数。这里只比较前缀，
         * 真正的编号范围（1..7）由调用方检查，避免把 "--os-terminal=abc"
         * 误当成合法终端。
         */
        bool isTerminalArgument(const char* argument) {
            static constexpr char prefix[] = "--os-terminal=";

            if (argument == nullptr) {
                return false;
            }

            uint32_t index = 0;

            while (prefix[index] != '\0' && argument[index] == prefix[index]) {
                ++index;
            }

            return prefix[index] == '\0';
        }

        /*
         * 从绝对路径中取出最后一段作为程序名。例如 "/sbin/init.elf" 得到
         * "init.elf"；结尾的 '/' 不算新段。当前请求路径没有调用它，保留作
         * 辅助函数，修改时不要误以为它参与了加载或权限判断。
         */
        const char* programName(const char* path) {
            const char* name = path;

            for (const char* cursor = path; *cursor != '\0'; ++cursor) {
                if (*cursor == '/' && cursor[1] != '\0') {
                    name = cursor + 1;
                }
            }

            return name;
        }

        /*
         * 把描述符对应的文件完整读进 image。文件可能大于一次 IPC 读取的上限，
         * 所以用 pread 循环，每次从 offset 继续，直到读满 size 字节。
         * 返回 false 表示读失败或提前遇到 EOF；缓冲区由调用者负责释放。
         */
        bool readExecutable(uint64_t fs, uint64_t descriptor, void* image,
                            uint64_t size) {
            os::File file;

            if (file.borrow(fs, descriptor, nullptr, O_RDONLY)) {
                return false;
            }

            uint64_t offset = 0;

            // 按偏移循环读取以处理短读，只有累计读满声明大小才算成功；提前结束不能把残缺映像交给加载器。
            while (offset < size) {
                const ssize_t count =
                    file.pread(static_cast<uint8_t*>(image) + offset,
                               size - offset, offset);

                if (count <= 0) {
                    return false;
                }

                offset += uint64_t(count);
            }

            return true;
        }

        /*
         * 读取并启动一个程序：先确认文件合法，再把映像读进内存、解析 ELF，
         * 组装 SpawnImageInfo，交给内核创建子任务。
         * 返回子任务的 TaskHandle；0 表示失败（失败路径会打印日志并释放缓冲）。
         * 参数 parentTaskId 是逻辑父进程（来自 request.senderPid，而不是 task
         * 服务自己）；managed 表示这是 Init 请求启动的受监护服务。
         */
        uint64_t loadAndSpawn(uint64_t fs, const char* path, uint64_t bootstrap,
                              uint64_t standardInput, uint64_t standardOutput,
                              uint64_t standardError, uint32_t argumentCount,
                              const char* const* arguments,
                              uint64_t parentTaskId, bool managed) {
            // 先 stat：必须是普通文件、非空、且不超过 8 MiB。
            // 目录、设备文件、空文件都在这里被拒绝，后面的读取才安全。
            os::File stat;

            if (!(stat.refresh(path) == 0) ||
                stat.getType() != os::File::Type::File || stat.getSize() == 0 ||
                stat.getSize() > maximumExecutableSize) {
                os::Sys().log("[TASK] executable stat failed\n");

                return 0;
            }

            // 按文件大小一次性分配映像缓冲；随后打开文件、循环读满。
            // 打开操作放在小 lambda 里，失败时返回 0；描述符的关闭由下面的
            // adopt/close 分支统一负责，读失败也不会泄漏。
            void* image = os::Runtime::allocateMemory(
                static_cast<size_t>(stat.getSize()));

            if (image == nullptr) {
                os::Sys().log("[TASK] executable allocation failed\n");

                return 0;
            }

            const uint64_t descriptor = ([&]() -> uint64_t {
                os::File opened;

                return opened.open(path, O_RDONLY) == 0 ? opened.releaseHandle()
                                                        : 0;
            }());
            const bool read =
                descriptor != 0 &&
                readExecutable(fs, descriptor, image, stat.getSize());

            if (descriptor != 0) {
                {
                    os::File opened;

                    if (!opened.adopt(fs, descriptor, nullptr, O_RDONLY)) {
                        opened.close();
                    }
                }
            }

            uint64_t task = 0;
            os::wire::SharedImageInfo sharedImages[sharedImageCount]{};
            void* sharedBuffers[sharedImageCount]{};
            bool sharedRead = read;
            // 解析 ELF 头。getInterpreter 返回 1 表示这是动态程序，interpreter
            // 里是 PT_INTERP 指定的解释器路径（如 /lib/ld-osinfo-x86-64.so）。
            // 只有解释器需要在这里预读；DT_NEEDED 的共享库由 Ring3 链接器处理。
            char interpreter[256]{};
            os::Executable executable;
            const int validation =
                read ? executable.open(image, stat.getSize()) : -1;
            const int interpreted = validation
                                        ? validation
                                        : executable.getInterpreter(
                                              interpreter, sizeof(interpreter));
            // 动态程序要连同解释器一起交给内核映射；解释器同样按普通文件读取，
            // 大小、类型检查与主映像一致。imagesToRead 为 0 表示静态程序。
            const uint32_t imagesToRead = interpreted == 1 ? 1 : 0;
            sharedRead                  = sharedRead && interpreted >= 0;

            for (uint32_t index = 0; index < imagesToRead && sharedRead;
                 ++index) {
                os::File sharedStat;

                if (!(sharedStat.refresh(interpreter) == 0) ||
                    sharedStat.getType() != os::File::Type::File ||
                    sharedStat.getSize() == 0 ||
                    sharedStat.getSize() > maximumExecutableSize) {
                    sharedRead = false;
                    break;
                }

                sharedBuffers[index] = os::Runtime::allocateMemory(
                    static_cast<size_t>(sharedStat.getSize()));
                const uint64_t sharedDescriptor = ([&]() -> uint64_t {
                    os::File opened;

                    return opened.open(interpreter, O_RDONLY) == 0
                               ? opened.releaseHandle()
                               : 0;
                }());
                sharedRead =
                    sharedBuffers[index] != nullptr && sharedDescriptor != 0 &&
                    readExecutable(fs, sharedDescriptor, sharedBuffers[index],
                                   sharedStat.getSize());

                if (sharedDescriptor != 0) {
                    os::File opened;

                    if (!opened.adopt(fs, sharedDescriptor, nullptr,
                                      O_RDONLY)) {
                        opened.close();
                    }
                }

                if (sharedRead) {
                    sharedImages[index] = {sharedBuffers[index],
                                           sharedStat.getSize(), interpreter};
                }
            }

            // 所有输入都读完后再组装请求。image/arguments 仍指向本进程内存，
            // 内核会在 SpawnImage 调用中把它们复制到子进程的地址空间。
            if (sharedRead) {
                // 文件数据、参数和标准句柄准备好后组装内核加载描述，真正创建进程由内核完成。
                os::wire::SpawnImageInfo info{};
                info.image                = image;
                info.imageSize            = stat.getSize();
                info.name                 = path;
                info.bootstrapHandle      = bootstrap;
                info.standardInputHandle  = standardInput;
                info.standardOutputHandle = standardOutput;
                info.standardErrorHandle  = standardError;
                info.capabilities         = 0;
                // 只有 Init 发起的受监护启动才允许带服务角色。这里按固定路径
                // 识别 users/tty/console/guiw；内核还会再次核对调用链和能力，
                // 不能因为路径匹配就认为身份已经可信。
                if (managed) {
                    info.flags |= os::spawnFlagManagedService;

                    if (os::Runtime::stringsEqual(path,
                                                  "/sbin/sys/users.elf")) {
                        info.serviceRole =
                            static_cast<uint32_t>(os::ServiceRole::Users);
                    } else if (os::Runtime::stringsEqual(path,
                                                         "/sbin/sys/tty.elf")) {
                        info.serviceRole =
                            static_cast<uint32_t>(os::ServiceRole::Tty);
                    } else if (os::Runtime::stringsEqual(
                                   path, "/sbin/sys/console.elf")) {
                        info.serviceRole =
                            static_cast<uint32_t>(os::ServiceRole::Console);
                    } else if (os::Runtime::stringsEqual(
                                   path, "/sbin/sys/guiw.elf")) {
                        info.serviceRole =
                            static_cast<uint32_t>(os::ServiceRole::Window);
                    }
                }

                // 新进程的父进程不一定还在运行；查不到时 parentValid 为 false，
                // 后面会直接返回失败，不带着不存在的父任务去创建子任务。
                // 终端关联分两种情况：stdin 是 PTY 时继承编号；Init 启动普通
                // 程序时，从 "--os-terminal=N" 参数推导 console 终端槽位。
                os::wire::TaskInfo parentInfo{};
                const bool parentValid = os::detail::TaskQuery().query(
                                             parentTaskId, parentInfo) == 0;
                uint32_t pty = 0;

                if (!parentInfo.terminalKind && standardInput &&
                    os::detail::PtyClient(standardInput).getNumber(pty)) {
                    info.flags |= os::spawnFlagNewSession;
                    info.terminalKind  = 2;
                    info.terminalIndex = pty;
                }

                if (!managed && !parentInfo.terminalKind &&
                    parentInfo.serviceRole ==
                        static_cast<uint32_t>(os::ServiceRole::Init)) {
                    for (uint32_t i = 0; i < argumentCount; ++i) {
                        if (isTerminalArgument(arguments[i]) &&
                            arguments[i][14] >= '1' &&
                            arguments[i][14] <= '7' && !arguments[i][15]) {
                            info.flags |= os::spawnFlagNewSession;
                            info.terminalKind  = 1;
                            info.terminalIndex = arguments[i][14] - '1';
                        }
                    }
                }

                info.parentTaskId     = parentTaskId;
                info.argumentCount    = argumentCount;
                info.arguments        = arguments;
                info.sharedImageCount = imagesToRead;
                info.sharedImages = imagesToRead == 0 ? nullptr : sharedImages;
                // 真正创建子任务：成功返回正的任务句柄，失败返回负错误码。
                // 错误码只在这里转成日志；调用者拿到的是统一的 0/1 状态。
                const int64_t result =
                    parentValid ? static_cast<int64_t>(
                                      os::detail::TaskQuery().spawnImage(info))
                                : -1;

                if (result > 0) {
                    task = static_cast<uint64_t>(result);
                } else {
                    os::Sys().log("[TASK] SpawnImage failed\n");
                    char status[]       = "[TASK] SpawnImage status=-00\n";
                    const uint64_t code = -result;
                    status[26]          = '0' + (code / 10) % 10;
                    status[27]          = '0' + code % 10;
                    os::Sys().log(status);
                }
            } else if (!read) {
                os::Sys().log("[TASK] executable read failed\n");
            } else {
                os::Sys().log("[TASK] shared library load failed\n");
            }

            // 无论成功还是失败，主映像和解释器缓冲都由本进程释放；
            // 子进程使用的是内核复制后的副本，不引用这里的地址。
            for (void* buffer : sharedBuffers) {
                if (buffer != nullptr) {
                    os::Runtime::freeMemory(buffer);
                }
            }

            os::Runtime::freeMemory(image);

            return task;
        }

        /*
         * 服务主循环：先拉起并等待子服务，再注册端点，然后逐条处理请求。
         * 这里的顺序不能乱：pipe/msgq 没就绪就注册端点，其他服务发来的请求
         * 可能找不到可用的 IPC 通道。
         */
        int run() {
            os::Sys().log("[TASK] Task Server started in Ring 3\n");
            const uint64_t registry = os::getBootstrapHandle();
            // Lifecycle 负责按配置启动/监护子服务（pipe、msgq 等）。
            // 三步都成功才继续；失败说明基础 IPC 不可用，直接退出由 init 处理。
            static services::Lifecycle children;

            if (!children.init("task", registry) || !children.bootstrap() ||
                !children.supervise()) {
                return 1;
            }

            // 注册服务名，让其他进程能按名字找到 task。端点创建失败或注册
            // 失败都意味着后续请求收不到，直接结束进程。
            const uint64_t endpoint = os::detail::EndpointControl().create();

            if (registry == 0 || endpoint == 0 ||
                !os::detail::RegistryClient(registry).registerName(
                    os::taskServiceName, endpoint)) {
                os::Sys().log("[TASK] registration failed\n");

                return 1;
            }

            uint64_t fs = 0;
            // core writer 是独立线程；启动失败不阻止 task 服务继续处理
            // 启动请求，只是崩溃时没有 core 可写，所以这里只记日志。
            if (!taskCore::start()) {
                os::Sys().log("[CORE] writer thread unavailable\n");
            }

            while (true) {
                os::IpcMessage request{};

                if (os::receiveMessage(endpoint, request) != 0) {
                    continue;
                }

                os::IpcMessage reply{};
                // ExecuteService 是“启动受监护服务”的专用入口，只允许真实
                // 发送者为 Init；普通 Execute 没有角色参数，不能借此提权。
                const bool serviceRequest =
                    request.type ==
                    static_cast<uint64_t>(os::TaskRequest::ExecuteService);
                const bool executeRequest =
                    serviceRequest ||
                    request.type ==
                        static_cast<uint64_t>(os::TaskRequest::Execute);
                const uint64_t bootstrap = executeRequest && request.handleCount
                                               ? request.handles[0]
                                               : 0;

                if (executeRequest) {
                    const char* arguments[maximumArgumentCount]{};
                    const char* path       = nullptr;
                    uint32_t argumentCount = 0;
                    bool valid =
                        parseArguments(request, path, arguments, argumentCount);
                    os::wire::TaskInfo caller{};
                    // 不要相信消息里自报的身份：用内核记录的 senderPid 查询
                    // 真实角色，再决定是否接受 ExecuteService。
                    if (serviceRequest &&
                        (os::detail::TaskQuery().query(request.senderPid,
                                                       caller) ||
                         caller.serviceRole !=
                             static_cast<uint32_t>(os::ServiceRole::Init))) {
                        valid = false;
                    }

                    const uint32_t encodedTerminal =
                        static_cast<uint32_t>((request.data[0] >> 32) & 0xffU);
                    uint32_t handleIndex    = 1;
                    uint64_t standardInput  = 0;
                    uint64_t standardOutput = 0;
                    uint64_t standardError  = 0;
                    // 句柄按 registry、stdin、stdout、stderr 的顺序排列；
                    // 只有标志位置位的才消费一个句柄。最后要求句柄数正好用完，
                    // 多传或少传都判为非法请求。
                    if ((request.data[0] & os::taskExecuteHasStdin) != 0) {
                        if (handleIndex < request.handleCount) {
                            standardInput = request.handles[handleIndex++];
                        } else {
                            valid = false;
                        }
                    }

                    if ((request.data[0] & os::taskExecuteHasStdout) != 0) {
                        if (handleIndex < request.handleCount) {
                            standardOutput = request.handles[handleIndex++];
                        } else {
                            valid = false;
                        }
                    }

                    if ((request.data[0] & os::taskExecuteHasStderr) != 0) {
                        if (handleIndex < request.handleCount) {
                            standardError = request.handles[handleIndex++];
                        } else {
                            valid = false;
                        }
                    }

                    if (handleIndex != request.handleCount) {
                        valid = false;
                    }

                    char terminalArgument[] = "--os-terminal=1";
                    bool hasTerminal        = false;
                    // 调用者没有显式带终端参数时，把请求里的终端编号补成
                    // "--os-terminal=N"。编号 0
                    // 表示不关联终端；1..(graphical+1)
                    // 是有效范围，越界就拒绝整个请求。
                    for (uint32_t index = 0; index < argumentCount; ++index) {
                        hasTerminal =
                            hasTerminal || isTerminalArgument(arguments[index]);
                    }

                    if (valid && !hasTerminal && encodedTerminal >= 1 &&
                        encodedTerminal <= os::graphicalTerminalId + 1 &&
                        argumentCount < maximumArgumentCount) {
                        terminalArgument[14] =
                            static_cast<char>('0' + encodedTerminal);
                        arguments[argumentCount++] = terminalArgument;
                    } else if (!hasTerminal && encodedTerminal != 0) {
                        valid = false;
                    }

                    // fs 服务可能比 task 晚就绪；第一次需要时按名字查找并缓存。
                    // 30 秒是等待上限，超时只影响本次请求，下次还会重试。
                    if (fs == 0) {
                        fs = os::detail::RegistryClient(registry).lookup(
                            os::fsServiceName, 30000);
                    }

                    if (!valid) {
                        os::Sys().log("[TASK] invalid execute request\n");
                    } else if (fs == 0) {
                        os::Sys().log("[TASK] fs service unavailable\n");
                    }

                    const uint64_t task =
                        valid && fs != 0
                            ? loadAndSpawn(fs, path, bootstrap, standardInput,
                                           standardOutput, standardError,
                                           argumentCount, arguments,
                                           request.senderPid, serviceRequest)
                            : 0;
                    reply.data[0] = task == 0 ? 1 : 0;
                    // 成功时把子任务句柄放进回复，句柄所有权随消息转移给
                    // 调用者；下面的循环会关闭请求里收到的临时句柄。
                    if (task != 0) {
                        reply.handleCount = 1;
                        reply.handles[0]  = task;
                    }
                } else if (request.type ==
                               static_cast<uint64_t>(os::TaskRequest::Wait) &&
                           request.handleCount == 1) {
                    // Wait 只查询一次状态，不在这里阻塞：state<0 统一回报 2，
                    // 0/1/2 分别表示运行、结束、停止或等待被中断。
                    int exitCode    = 0;
                    const int state = os::detail::TaskControl().getState(
                        request.handles[0], exitCode);
                    os::closeHandle(request.handles[0]);
                    reply.data[0] =
                        state < 0 ? 2 : static_cast<uint64_t>(state);
                    reply.data[1] = static_cast<uint64_t>(exitCode);
                    reply.length  = 2;
                } else {
                    reply.data[0] = 3;
                }

                if (executeRequest) {
                    // 请求里的标准流/registry 句柄只在本次启动中使用；
                    // 回复携带的是新任务的句柄，不能混在一起关闭。
                    for (uint32_t index = 0; index < request.handleCount;
                         ++index) {
                        os::closeHandle(request.handles[index]);
                    }
                }

                if (reply.length == 0) {
                    reply.length = 1;
                }

                os::replyMessage(endpoint, request.replyToken, reply);

                for (uint32_t i = 0; i < reply.handleCount; ++i) {
                    os::closeHandle(reply.handles[i]);
                }
            }
        }
};

/*
 * 服务入口：对象是静态的，避免把整个 TaskServer 放进栈；run() 正常情况下
 * 是无限循环，只有初始化失败才会返回非零。
 */
int main(int, char**) {
    static TaskServer server;

    return server.run();
}
