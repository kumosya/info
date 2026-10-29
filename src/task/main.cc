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

namespace {
class TaskServer {
   public:
    static constexpr uint32_t maximumArgumentCount = 64;
    static constexpr uint32_t executePayloadSize =
        (os::ipcDataWordCount - 1) * sizeof(uint64_t);
    static constexpr uint64_t maximumExecutableSize = 8ULL * 1024 * 1024;
    static constexpr uint32_t sharedImageCount      = 1;


    bool parseArguments(const os::IpcMessage& request, const char*& path,
                        const char* (&arguments)[maximumArgumentCount],
                        uint32_t& argumentCount) {
        argumentCount = static_cast<uint32_t>(request.data[0]);
        if (request.length < 2 || request.length > os::ipcDataWordCount)
            return false;
        if (argumentCount == 0 || argumentCount > maximumArgumentCount) {
            return false;
        }
        const char* payload = reinterpret_cast<const char*>(&request.data[1]);
        const uint32_t payloadSize = (request.length - 1) * sizeof(uint64_t);
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

    const char* programName(const char* path) {
        const char* name = path;
        for (const char* cursor = path; *cursor != '\0'; ++cursor) {
            if (*cursor == '/' && cursor[1] != '\0') {
                name = cursor + 1;
            }
        }
        return name;
    }

    bool readExecutable(uint64_t fs, uint64_t descriptor, void* image,
                        uint64_t size) {
        os::File file;
        if (file.borrow(fs, descriptor, nullptr, O_RDONLY)) return false;
        uint64_t offset=0;
        while(offset<size) {
            const auto count=file.pread(static_cast<uint8_t*>(image)+offset,size-offset,offset);
            if(count<=0)return false;
            offset+=uint64_t(count);
        }
        return true;
    }

    uint64_t loadAndSpawn(uint64_t fs, const char* path, uint64_t bootstrap,
                          uint64_t standardInput, uint64_t standardOutput,
                          uint64_t standardError, uint32_t argumentCount,
                          const char* const* arguments, uint64_t parentTaskId,
                          bool managed) {
        os::File stat;
        if (!(stat.refresh(path) == 0) ||
            stat.getType() != os::File::Type::File || stat.getSize() == 0 ||
            stat.getSize() > maximumExecutableSize) {
            os::Sys().log("[TASK] executable stat failed\n");
            return 0;
        }
        void* image =
            os::Runtime::allocateMemory(static_cast<size_t>(stat.getSize()));
        if (image == nullptr) {
            os::Sys().log("[TASK] executable allocation failed\n");
            return 0;
        }
        const uint64_t descriptor = ([&]() -> uint64_t {
            os::File opened;
            return opened.open(path, O_RDONLY) == 0 ? opened.releaseHandle()
                                                    : 0;
        }());
        const bool read           = descriptor != 0 &&
                          readExecutable(fs, descriptor, image, stat.getSize());
        if (descriptor != 0) {
            {
                os::File opened;
                if (!opened.adopt(fs, descriptor, nullptr, O_RDONLY))
                    opened.close();
            }
        }
        uint64_t task = 0;
        os::wire::SharedImageInfo sharedImages[sharedImageCount]{};
        void* sharedBuffers[sharedImageCount]{};
        bool sharedRead = read;
        char interpreter[256]{};
        os::Executable executable;
        const int validation =
            read ? executable.open(image, stat.getSize()) : -1;
        const int interpreted =
            validation
                ? validation
                : executable.getInterpreter(interpreter, sizeof(interpreter));
        const uint32_t imagesToRead = interpreted == 1 ? 1 : 0;
        sharedRead                  = sharedRead && interpreted >= 0;
        for (uint32_t index = 0; index < imagesToRead && sharedRead; ++index) {
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
                if (!opened.adopt(fs, sharedDescriptor, nullptr, O_RDONLY))
                    opened.close();
            }
            if (sharedRead) {
                sharedImages[index] = {sharedBuffers[index],
                                       sharedStat.getSize(), interpreter};
            }
        }
        if (sharedRead) {
            os::wire::SpawnImageInfo info{};
            info.image                = image;
            info.imageSize            = stat.getSize();
            info.name                 = path;
            info.bootstrapHandle      = bootstrap;
            info.standardInputHandle  = standardInput;
            info.standardOutputHandle = standardOutput;
            info.standardErrorHandle  = standardError;
            info.capabilities         = 0;
            if (managed) {
                info.flags |= os::spawnFlagManagedService;
                if (os::Runtime::stringsEqual(path, "/sbin/sys/users.elf")) {
                    info.serviceRole =
                        static_cast<uint32_t>(os::ServiceRole::Users);
                } else if (os::Runtime::stringsEqual(path, "/sbin/sys/tty.elf"))
                    info.serviceRole =
                        static_cast<uint32_t>(os::ServiceRole::Tty);
                else if (os::Runtime::stringsEqual(path,
                                                   "/sbin/sys/console.elf")) {
                    info.serviceRole =
                        static_cast<uint32_t>(os::ServiceRole::Console);
                } else if (os::Runtime::stringsEqual(path,
                                                     "/sbin/sys/guiw.elf")) {
                    info.serviceRole =
                        static_cast<uint32_t>(os::ServiceRole::Window);
                }
            }
            os::wire::TaskInfo parentInfo{};
            const bool parentValid =
                os::detail::TaskQuery().query(parentTaskId, parentInfo) == 0;
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
                for (uint32_t i = 0; i < argumentCount; ++i)
                    if (isTerminalArgument(arguments[i]) &&
                        arguments[i][14] >= '1' && arguments[i][14] <= '7' &&
                        !arguments[i][15]) {
                        info.flags |= os::spawnFlagNewSession;
                        info.terminalKind  = 1;
                        info.terminalIndex = arguments[i][14] - '1';
                    }
            }
            info.parentTaskId     = parentTaskId;
            info.argumentCount    = argumentCount;
            info.arguments        = arguments;
            info.sharedImageCount = imagesToRead;
            info.sharedImages     = imagesToRead == 0 ? nullptr : sharedImages;
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
        for (void* buffer : sharedBuffers) {
            if (buffer != nullptr) os::Runtime::freeMemory(buffer);
        }
        os::Runtime::freeMemory(image);
        return task;
    }

    int run() {
        os::Sys().log("[TASK] Task Server started in Ring 3\n");
        const uint64_t registry = os::getBootstrapHandle();
        static services::Lifecycle children;
        if (!children.init("task", registry) || !children.bootstrap() ||
            !children.supervise())
            return 1;
        const uint64_t endpoint = os::detail::EndpointControl().create();
        if (registry == 0 || endpoint == 0 ||
            !os::detail::RegistryClient(registry).registerName(
                os::taskServiceName, endpoint)) {
            os::Sys().log("[TASK] registration failed\n");
            return 1;
        }
        uint64_t fs = 0;
        if (!taskCore::start())
            os::Sys().log("[CORE] writer thread unavailable\n");
        while (true) {
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint, request) != 0) {
                continue;
            }
            os::IpcMessage reply{};
            const bool serviceRequest =
                request.type ==
                static_cast<uint64_t>(os::TaskRequest::ExecuteService);
            const bool executeRequest =
                serviceRequest ||
                request.type == static_cast<uint64_t>(os::TaskRequest::Execute);
            const uint64_t bootstrap =
                executeRequest && request.handleCount ? request.handles[0] : 0;
            if (executeRequest) {
                const char* arguments[maximumArgumentCount]{};
                const char* path       = nullptr;
                uint32_t argumentCount = 0;
                bool valid =
                    parseArguments(request, path, arguments, argumentCount);
                os::wire::TaskInfo caller{};
                if (serviceRequest &&
                    (os::detail::TaskQuery().query(request.senderPid, caller) ||
                     caller.serviceRole !=
                         static_cast<uint32_t>(os::ServiceRole::Init)))
                    valid = false;
                const uint32_t encodedTerminal =
                    static_cast<uint32_t>((request.data[0] >> 32) & 0xffU);
                uint32_t handleIndex    = 1;
                uint64_t standardInput  = 0;
                uint64_t standardOutput = 0;
                uint64_t standardError  = 0;
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
                if (task != 0) {
                    reply.handleCount = 1;
                    reply.handles[0]  = task;
                }
            } else if (request.type ==
                           static_cast<uint64_t>(os::TaskRequest::Wait) &&
                       request.handleCount == 1) {
                int exitCode    = 0;
                const int state = os::detail::TaskControl().getState(
                    request.handles[0], exitCode);
                os::closeHandle(request.handles[0]);
                reply.data[0] = state < 0 ? 2 : static_cast<uint64_t>(state);
                reply.data[1] = static_cast<uint64_t>(exitCode);
                reply.length  = 2;
            } else {
                reply.data[0] = 3;
            }
            if (executeRequest) {
                for (uint32_t index = 0; index < request.handleCount; ++index) {
                    os::closeHandle(request.handles[index]);
                }
            }
            if (reply.length == 0) {
                reply.length = 1;
            }
            os::replyMessage(endpoint, request.replyToken, reply);
            for (uint32_t i = 0; i < reply.handleCount; ++i)
                os::closeHandle(reply.handles[i]);
        }
    }
};
}  // namespace

int main(int, char**) {
    static TaskServer server;
    return server.run();
}
