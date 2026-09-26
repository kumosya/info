#include "os/abi.h"
#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/terminal.h"

namespace {

constexpr uint32_t maximumArgumentCount  = 8;
constexpr uint32_t executePayloadSize    = 7 * sizeof(uint64_t);
constexpr uint64_t maximumExecutableSize = 8ULL * 1024 * 1024;
constexpr uint32_t sharedImageCount = 3;
constexpr const char* sharedImagePaths[sharedImageCount] = {
    "/lib/x86-64-osinfo/libos-c++.so",
    "/lib/x86-64-osinfo/libc.so",
    "/lib/x86-64-osinfo/libc++.so"};
constexpr uint32_t processRecordCapacity = 64;
constexpr uint32_t executableReadWindow  = 8;
constexpr uint32_t elfProgramDynamic     = 2;

struct [[gnu::packed]] ElfHeader {
    uint8_t identifier[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t programHeaderOffset;
    uint64_t sectionHeaderOffset;
    uint32_t flags;
    uint16_t headerSize;
    uint16_t programHeaderSize;
    uint16_t programHeaderCount;
    uint16_t sectionHeaderSize;
    uint16_t sectionHeaderCount;
    uint16_t sectionNameIndex;
};

struct [[gnu::packed]] ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtualAddress;
    uint64_t physicalAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint64_t alignment;
};

struct ProcessRecord {
    uint64_t handle;
    uint64_t id;
    char name[48];
};

ProcessRecord processRecords[processRecordCapacity];
uint64_t executableCompletionEndpoint;

void reapProcessRecords() {
    for (ProcessRecord& record : processRecords) {
        if (record.handle == 0) continue;
        os::ProcessInfo info{};
        if (os::getProcessInfo(record.handle, info) != 0 || info.state == 2) {
            os::closeHandle(record.handle);
            record = {};
        }
    }
}

void trackProcess(uint64_t handle, const char* name) {
    reapProcessRecords();
    os::ProcessInfo info{};
    if (handle == 0 || os::getProcessInfo(handle, info) != 0) return;
    for (ProcessRecord& record : processRecords) {
        if (record.handle == 0) {
            record.handle = handle;
            record.id = info.id;
            os::copyString(record.name, name, sizeof(record.name));
            return;
        }
    }
}

bool listProcess(uint32_t requested, os::IpcMessage& reply) {
    reapProcessRecords();
    uint32_t visible = 0;
    for (ProcessRecord& record : processRecords) {
        if (record.handle == 0) continue;
        os::ProcessInfo info{};
        if (os::getProcessInfo(record.handle, info) != 0) continue;
        if (visible++ != requested) continue;
        reply.data[0] = 0;
        reply.data[1] = info.id;
        reply.data[2] = info.state;
        os::copyString(reinterpret_cast<char*>(&reply.data[3]), record.name,
                       6 * sizeof(uint64_t));
        reply.length = 9;
        return true;
    }
    reply.data[0] = 1;
    return false;
}

bool killProcess(uint64_t processId) {
    reapProcessRecords();
    for (ProcessRecord& record : processRecords) {
        if (record.handle != 0 && record.id == processId) {
            return os::terminateProcess(record.handle, -15) == 0;
        }
    }
    return false;
}

bool parseArguments(const os::IpcMessage& request, const char*& path,
                    const char* (&arguments)[maximumArgumentCount],
                    uint32_t& argumentCount) {
    argumentCount = static_cast<uint32_t>(request.data[0]);
    if (argumentCount == 0 || argumentCount > maximumArgumentCount) {
        return false;
    }
    const char* payload = reinterpret_cast<const char*>(&request.data[1]);
    const char* values[maximumArgumentCount + 1]{};
    uint32_t offset = 0;
    for (uint32_t index = 0; index <= argumentCount; ++index) {
        if (offset >= executePayloadSize) {
            return false;
        }
        values[index] = payload + offset;
        while (offset < executePayloadSize && payload[offset] != '\0') {
            ++offset;
        }
        if (offset == executePayloadSize) {
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
    constexpr char prefix[] = "--os-terminal=";
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

uint64_t capabilitiesForPath(const char* path) {
    return os::stringsEqual(path, "/system/services/console.elf") ||
                   os::stringsEqual(path, "/system/services/window.elf")
               ? os::processCapabilityDisplay
               : 0;
}

bool usesDynamicLinking(const void* image, uint64_t size) {
    if (image == nullptr || size < sizeof(ElfHeader)) return false;
    const auto* header = static_cast<const ElfHeader*>(image);
    if (header->identifier[0] != 0x7f || header->identifier[1] != 'E' ||
        header->identifier[2] != 'L' || header->identifier[3] != 'F' ||
        header->programHeaderSize != sizeof(ProgramHeader) ||
        header->programHeaderOffset > size ||
        header->programHeaderCount >
            (size - header->programHeaderOffset) / sizeof(ProgramHeader)) {
        return false;
    }
    const auto* headers = reinterpret_cast<const ProgramHeader*>(
        static_cast<const uint8_t*>(image) + header->programHeaderOffset);
    for (uint16_t index = 0; index < header->programHeaderCount; ++index) {
        if (headers[index].type == elfProgramDynamic &&
            headers[index].memorySize != 0) {
            return true;
        }
    }
    return false;
}

bool readExecutable(uint64_t fs, uint64_t descriptor, void* image,
                    uint64_t size) {
    if (executableCompletionEndpoint == 0) return false;
    uint64_t nextOffset = 0;
    uint32_t inFlight   = 0;
    bool success        = true;
    while (nextOffset < size || inFlight != 0) {
        while (success && inFlight < executableReadWindow &&
               nextOffset < size) {
            const size_t chunk =
                size - nextOffset < os::maximumAsyncFileTransfer
                    ? static_cast<size_t>(size - nextOffset)
                    : os::maximumAsyncFileTransfer;
            if (!os::readFileAsync(fs, descriptor, nextOffset, chunk,
                                   executableCompletionEndpoint,
                                   nextOffset)) {
                success = false;
                break;
            }
            nextOffset += chunk;
            ++inFlight;
        }
        if (inFlight == 0) break;
        uint8_t data[os::maximumAsyncFileTransfer]{};
        os::FileOperationResult result{};
        if (!os::waitFileOperation(executableCompletionEndpoint, result, data,
                                   sizeof(data)) ||
            result.status != 0 || result.requestId >= size) {
            success = false;
            --inFlight;
            continue;
        }
        const size_t expected =
            size - result.requestId < os::maximumAsyncFileTransfer
                ? static_cast<size_t>(size - result.requestId)
                : os::maximumAsyncFileTransfer;
        if (result.transferred != expected) {
            success = false;
            break;
        }
        for (size_t index = 0; index < expected; ++index) {
            static_cast<uint8_t*>(image)[result.requestId + index] = data[index];
        }
        --inFlight;
    }
    return success && nextOffset == size && inFlight == 0;
}

uint64_t loadAndSpawn(uint64_t fs, const char* path, uint64_t bootstrap,
                      uint64_t standardInput, uint64_t standardOutput,
                      uint64_t standardError, uint32_t argumentCount,
                      const char* const* arguments,
                      uint64_t parentProcessId) {
    os::FileStat stat{};
    if (!os::statFile(fs, path, stat) || stat.type != os::FileType::File ||
        stat.size == 0 || stat.size > maximumExecutableSize) {
        os::log("[TASK] executable stat failed\n");
        return 0;
    }
    void* image = os::allocateMemory(static_cast<size_t>(stat.size));
    if (image == nullptr) {
        os::log("[TASK] executable allocation failed\n");
        return 0;
    }
    const uint64_t descriptor = os::openFile(fs, path);
    const bool read = descriptor != 0 &&
                      readExecutable(fs, descriptor, image, stat.size);
    if (descriptor != 0) {
        os::closeFile(fs, descriptor);
    }
    uint64_t process = 0;
    os::SharedImageInfo sharedImages[sharedImageCount]{};
    void* sharedBuffers[sharedImageCount]{};
    bool sharedRead = read;
    const bool dynamicExecutable = read && usesDynamicLinking(image, stat.size);
    const uint32_t imagesToRead =
        dynamicExecutable ? sharedImageCount : 0;
    for (uint32_t index = 0; index < imagesToRead && sharedRead; ++index) {
        os::FileStat sharedStat{};
        if (!os::statFile(fs, sharedImagePaths[index], sharedStat) ||
            sharedStat.type != os::FileType::File || sharedStat.size == 0 ||
            sharedStat.size > maximumExecutableSize) {
            sharedRead = false;
            break;
        }
        sharedBuffers[index] =
            os::allocateMemory(static_cast<size_t>(sharedStat.size));
        const uint64_t sharedDescriptor =
            os::openFile(fs, sharedImagePaths[index]);
        sharedRead = sharedBuffers[index] != nullptr &&
                     sharedDescriptor != 0 &&
                     readExecutable(fs, sharedDescriptor,
                                    sharedBuffers[index], sharedStat.size);
        if (sharedDescriptor != 0) os::closeFile(fs, sharedDescriptor);
        if (sharedRead) {
            sharedImages[index] = {sharedBuffers[index], sharedStat.size,
                                   sharedImagePaths[index]};
        }
    }
    if (sharedRead) {
        os::SpawnImageInfo info{};
        info.image = image;
        info.imageSize = stat.size;
        info.name = programName(path);
        info.bootstrapHandle = bootstrap;
        info.standardInputHandle = standardInput;
        info.standardOutputHandle = standardOutput;
        info.standardErrorHandle = standardError;
        info.capabilities = capabilitiesForPath(path);
        info.parentProcessId = parentProcessId;
        info.argumentCount = argumentCount;
        info.arguments = arguments;
        info.sharedImageCount = imagesToRead;
        info.sharedImages = imagesToRead == 0 ? nullptr : sharedImages;
        const int64_t result = static_cast<int64_t>(os::spawnImage(info));
        if (result > 0) {
            process = static_cast<uint64_t>(result);
        } else {
            os::log("[TASK] SpawnImage failed\n");
        }
    } else if (!read) {
        os::log("[TASK] executable read failed\n");
    } else {
        os::log("[TASK] shared library load failed\n");
    }
    for (void* buffer : sharedBuffers) {
        if (buffer != nullptr) os::freeMemory(buffer);
    }
    os::freeMemory(image);
    return process;
}

}  // namespace

extern "C" int userMain() {
    os::log("[TASK] Task Server started in Ring 3\n");
    const uint64_t registry = os::getBootstrapHandle();
    const uint64_t endpoint = os::createEndpoint();
    executableCompletionEndpoint = os::createEndpoint();
    if (registry == 0 || endpoint == 0 ||
        executableCompletionEndpoint == 0 ||
        !os::registerService(registry, os::taskServiceName, endpoint)) {
        os::log("[TASK] registration failed\n");
        return 1;
    }
    uint64_t fs = 0;
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(endpoint, request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        const bool spawnRequest =
            request.type == static_cast<uint64_t>(os::TaskRequest::Spawn);
        const bool executeRequest =
            request.type == static_cast<uint64_t>(os::TaskRequest::Execute);
        const uint64_t bootstrap =
            (spawnRequest || executeRequest) && request.handleCount >= 1
                ? request.handles[0]
                : 0;
        if (request.type == static_cast<uint64_t>(os::TaskRequest::Spawn)) {
            const int64_t result = static_cast<int64_t>(os::spawnProgram(
                static_cast<os::ProgramId>(request.data[0]), bootstrap));
            const uint64_t process =
                result > 0 ? static_cast<uint64_t>(result) : 0;
            trackProcess(process, "system-service");
            reply.data[0] = process == 0 ? 1 : 0;
            if (process != 0) {
                reply.handleCount = 1;
                reply.handles[0]  = process;
            }
        } else if (request.type ==
                   static_cast<uint64_t>(os::TaskRequest::Execute)) {
            const char* arguments[maximumArgumentCount]{};
            const char* path       = nullptr;
            uint32_t argumentCount = 0;
            bool valid =
                parseArguments(request, path, arguments, argumentCount);
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
                terminalArgument[14] = static_cast<char>('0' + encodedTerminal);
                arguments[argumentCount++] = terminalArgument;
            } else if (!hasTerminal && encodedTerminal != 0) {
                valid = false;
            }
            if (fs == 0) {
                fs = os::lookupService(registry, os::fsServiceName, 30000);
            }
            if (!valid) {
                os::log("[TASK] invalid execute request\n");
            } else if (fs == 0) {
                os::log("[TASK] fs service unavailable\n");
            }
            const uint64_t process =
                valid && fs != 0
                    ? loadAndSpawn(fs, path, bootstrap, standardInput,
                                   standardOutput, standardError, argumentCount,
                                   arguments, request.senderPid)
                    : 0;
            for (uint32_t index = 0; index < request.handleCount; ++index) {
                os::closeHandle(request.handles[index]);
            }
            if (process != 0) {
                trackProcess(process, programName(path));
            }
            reply.data[0] = process == 0 ? 1 : 0;
            if (process != 0) {
                reply.handleCount = 1;
                reply.handles[0]  = process;
            }
        } else if (request.type ==
                       static_cast<uint64_t>(os::TaskRequest::Wait) &&
                   request.handleCount == 1) {
            int exitCode    = 0;
            const int state = os::getProcessState(request.handles[0], exitCode);
            os::closeHandle(request.handles[0]);
            reply.data[0] = state < 0 ? 2 : static_cast<uint64_t>(state);
            reply.data[1] = static_cast<uint64_t>(exitCode);
            reply.length  = 2;
        } else if (request.type ==
                   static_cast<uint64_t>(os::TaskRequest::List)) {
            listProcess(static_cast<uint32_t>(request.data[0]), reply);
        } else if (request.type ==
                   static_cast<uint64_t>(os::TaskRequest::Kill)) {
            reply.data[0] = killProcess(request.data[0]) ? 0 : 1;
        } else {
            reply.data[0] = 3;
        }
        if (spawnRequest || executeRequest) {
            for (uint32_t index = 0; index < request.handleCount; ++index) {
                os::closeHandle(request.handles[index]);
            }
        }
        if (reply.length == 0) {
            reply.length = 1;
        }
        os::replyMessage(endpoint, reply);
    }
}
