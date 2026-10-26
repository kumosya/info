#pragma once
#include <stddef.h>
#include <stdint.h>

#include <os/syscall.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OsFileStat {
    uint64_t type;
    uint64_t size;
    uint32_t mode;
    uint64_t inode;
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t accessTime;
    uint64_t modificationTime;
    uint64_t changeTime;
    uint64_t device;
    uint64_t specialDevice;
} OsFileStat;

typedef struct OsDirectoryEntry {
    uint64_t type;
    char name[256];
    uint64_t inode;
    uint64_t nextOffset;
} OsDirectoryEntry;


typedef struct OsFileOperationResult {
    uint64_t requestId;
    int64_t status;
    uint64_t transferred;
} OsFileOperationResult;

typedef struct OsTaskIdentifiers {
    uint64_t parentId;
    uint64_t taskGroupId;
    uint64_t sessionId;
} OsTaskIdentifiers;

typedef struct OsTaskInfo {
    uint64_t id;
    uint32_t state;
    int32_t exitCode;
    char name[48];
    uint32_t uid, gid, effectiveUid, effectiveGid;
    uint64_t parentId;
    uint32_t trustedDeviceService;
    uint32_t serviceRole;
    uint64_t taskGroupId, sessionId;
    uint64_t terminalForegroundGroup;
    uint64_t startedNanoseconds, cpuNanoseconds;
    uint32_t terminalKind, terminalIndex;
    uint32_t commandLineSize;
    uint32_t managedService;
    char commandLine[384];
    uint64_t mainThreadId;
    int32_t nice;
    uint32_t threadCount;
} OsTaskInfo;
typedef struct OsCredentialInfo {
    uint32_t uid, effectiveUid, savedUid, gid, effectiveGid, savedGid,
        groupCount;
    uint32_t groups[32];
} OsCredentialInfo;

typedef struct OsSignalAction {
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
} OsSignalAction;

typedef struct OsMessageQueueAttributes {
    uint64_t flags;
    uint64_t maximumMessages;
    uint64_t messageSize;
    uint64_t currentMessages;
} OsMessageQueueAttributes;

typedef struct OsExecImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
    uint32_t argumentCount;
    uint32_t environmentCount;
    const char* const* arguments;
    const char* const* environment;
    const void* runtimeState;
    uint64_t runtimeStateSize;
    uint32_t sharedImageCount;
    uint32_t sharedReserved;
    const struct OsSharedImageInfo* sharedImages;
} OsExecImageInfo;

typedef struct OsSharedImageInfo {
    const void* image;
    uint64_t imageSize;
    const char* name;
} OsSharedImageInfo;

typedef struct OsIpcMessage {
    uint64_t type, senderPid, senderTid, replyToken;
    uint32_t length, handleCount;
    uint64_t data[515], handles[4];
    uint32_t handleRights[4], handleOptions[4];
} OsIpcMessage;
typedef struct OsMountStat {
    char device[64], target[64], type[16];
    uint64_t readOnly;
} OsMountStat;

int osFileOpen(uint64_t fs, const char* path, int flags, uint32_t mode,
               uint64_t* handle);

int osFileClose(uint64_t fs, uint64_t handle);

int64_t osFileRead(uint64_t fs, uint64_t handle, void* buffer, size_t size);

int64_t osFileWrite(uint64_t fs, uint64_t handle, const void* buffer,
                    size_t size);

int64_t osFileReadAt(uint64_t fs, uint64_t handle, void* buffer, size_t size,
                     uint64_t offset);

int64_t osFileWriteAt(uint64_t fs, uint64_t handle, const void* buffer,
                      size_t size, uint64_t offset);

int64_t osFileSeek(uint64_t fs, uint64_t handle, int64_t offset, int whence);

int osFileTruncate(uint64_t fs, uint64_t handle, uint64_t length);

int osFileFlush(uint64_t fs, uint64_t handle);

int osFileQuery(uint64_t fs, uint64_t handle, OsFileStat* result);

int osFileSetMode(uint64_t fs, uint64_t handle, uint32_t mode);

int osFileSetOwner(uint64_t fs, uint64_t handle, uint32_t uid, uint32_t gid);

int osFileSetTimes(uint64_t fs, uint64_t handle, uint64_t accessTime,
                   uint64_t modificationTime);

int osFileReadAsync(uint64_t fs, uint64_t handle, uint64_t offset, size_t size,
                    uint64_t completionEndpoint, uint64_t requestId);

int osFileWriteAsync(uint64_t fs, uint64_t handle, uint64_t offset,
                     const void* buffer, size_t size,
                     uint64_t completionEndpoint, uint64_t requestId);

int osFileWaitOperation(uint64_t completionEndpoint,
                        OsFileOperationResult* result, void* buffer,
                        size_t capacity, uint64_t deadline);

int osFsQuery(const char* path, OsFileStat* result);

int osFsRemove(const char* path);

int osFsRename(const char* existing, const char* target);

int osFsLink(const char* existing, const char* target);

int osFsFormat(const char* device, const char* type);

int osDirOpen(const char* path, uint64_t* handle);

int osDirClose(uint64_t handle);

int osDirCreate(const char* path, uint32_t mode);

int osDirRemove(const char* path);

int osDirRename(const char* existing, const char* target);

int osDirRead(uint64_t handle, uint64_t offset, OsDirectoryEntry* entries,
              size_t capacity, size_t* count);

int osMountCreate(const char* source, const char* target, const char* type,
                  unsigned long flags, const void* data, uint64_t* mountId);

int osMountClose(uint64_t mountId);

int osMountQuery(uint32_t index, OsMountStat* result);

int osIpcCreate(uint64_t* endpoint);

int osIpcSend(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline);

int osIpcReceive(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline);

int osIpcCall(uint64_t endpoint, OsIpcMessage* request, OsIpcMessage* reply,
              uint64_t deadline);

int osIpcReply(uint64_t endpoint, uint64_t replyToken, OsIpcMessage* reply);

int osIpcHandleClose(uint64_t handle);

int osIpcHandleDuplicate(uint64_t handle, uint32_t rights, uint32_t flags,
                         uint64_t* result);

int osIpcHandleRestrict(uint64_t handle, uint32_t rights);

int osIpcHandleSetFlags(uint64_t handle, uint32_t flags);

int osIpcPipeCreate(uint64_t* readHandle, uint64_t* writeHandle);

int64_t osIpcPipeRead(uint64_t readHandle, void* buffer, size_t size);

int64_t osIpcPipeWrite(uint64_t writeHandle, const void* buffer, size_t size);

int osIpcMsgQueueOpen(const char* name, int flags, uint32_t maximumMessages,
                      uint32_t messageSize, uint64_t* handle);

int osIpcMsgQueueUnlink(const char* name);

int osIpcMsgQueueSend(uint64_t handle, const void* buffer, size_t size,
                      uint32_t priority);

int64_t osIpcMsgQueueReceive(uint64_t handle, void* buffer, size_t capacity,
                             uint32_t* priority);

int osIpcMsgQueueQuery(uint64_t handle, OsMessageQueueAttributes* result);

int osIpcMsgQueueSetFlags(uint64_t handle, uint64_t flags, uint64_t* previous);

int osIpcMsgQueueSetNotification(uint64_t handle, int type, uint32_t signal);

int64_t osConsoleRead(void* buffer, size_t size);

int64_t osConsoleWrite(const void* buffer, size_t size);

int osConsoleGetId(uint32_t* id);

int osConsoleSetId(uint32_t id);

int osPtyCreate(uint64_t* master, uint64_t* slave);

int osPtyGetNumber(uint64_t master, uint32_t* number);

int osPtySetNonblocking(uint64_t master, int enabled);

int osPtySetForegroundGroup(uint64_t master, uint64_t group);

int osTaskGetId(uint64_t* id);

int osTaskQuery(uint64_t id, OsTaskInfo* result);

int osTaskGetParentId(uint64_t id, uint64_t* parentId);

int osTaskGetGroupId(uint64_t id, uint64_t* groupId);

int osTaskGetSessionId(uint64_t id, uint64_t* sessionId);

int osTaskExecute(const char* path, uint32_t argumentCount,
                  const char* const* arguments, uint64_t input, uint64_t output,
                  uint64_t error, uint64_t* handle);

int64_t osTaskFork(void);

int osTaskReplaceImage(const OsExecImageInfo* image);

int64_t osTaskWaitChild(int64_t id, int* status, int options);

int osTaskSendSignal(uint64_t id, uint32_t signal);

void osTaskExit(int status);

int osTaskThreadGetId(uint64_t* id);

int osTaskThreadCreate(void (*entry)(void*), void* argument, void* stack,
                       size_t stackSize, uint64_t* id);

int osTaskThreadWait(uint64_t id, int* exitCode, int noHang);

int osTaskThreadSendSignal(uint64_t id, uint32_t signal);

void osTaskThreadExit(int status);

int osTaskSessionCreate(uint64_t* id);

int osTaskSetGroup(uint64_t id, uint64_t group);

int osTaskGetNice(uint64_t id, int* value);

int osTaskSetNice(uint64_t id, int value);

int osTaskGetResourceLimit(uint64_t id, int resource, uint64_t* soft,
                           uint64_t* hard);

int osTaskSetResourceLimit(uint64_t id, int resource, uint64_t soft,
                           uint64_t hard);

int osTaskGetCredentials(uint64_t id, OsCredentialInfo* result);

int osTaskSetUserIds(uint32_t real, uint32_t effective, uint32_t saved);

int osTaskSetGroupIds(uint32_t real, uint32_t effective, uint32_t saved);

int osTaskSetGroups(const uint32_t* groups, uint32_t count);

void osTaskYield(void);

int osTaskSleep(uint64_t milliseconds);

int osTaskGetSignalAction(uint32_t signal, OsSignalAction* result);

int osTaskSetSignalAction(uint32_t signal, const OsSignalAction* action,
                          OsSignalAction* previous);

int osTaskGetSignalMask(uint64_t* mask);

int osTaskSetSignalMask(int how, uint64_t mask, uint64_t* previous);

int osTaskGetPendingSignals(uint64_t* mask);

int osTaskWaitSignal(uint64_t mask);

int osTaskGetSignalStack(OsSignalStack* result);

int osTaskSetSignalStack(const OsSignalStack* stack, OsSignalStack* previous);

int osTaskGetCoreLimit(uint64_t* soft, uint64_t* hard);

int osTaskSetCoreLimit(uint64_t soft, uint64_t hard);

void osTaskSignalReturn(void* frame);

int osMemMapAnonymous(uint64_t address, uint64_t size, uint32_t protection,
                      uint32_t flags, uint64_t* result);

int osMemMapFile(uint64_t handle, uint64_t address, uint64_t size,
                 uint64_t offset, uint32_t protection, uint32_t flags,
                 uint64_t* result);

int osMemMapShared(uint64_t handle, uint64_t address, uint64_t size,
                   uint64_t offset, uint32_t protection, uint64_t* result);

int osMemMapUnmap(uint64_t address, uint64_t size);

int osMemMapSetProtection(uint64_t address, uint64_t size, uint32_t protection);

int osMemMapSync(uint64_t address, uint64_t size, int mode);

int osMemShareOpen(const char* name, int flags, uint64_t* handle);

int osMemShareUnlink(const char* name);

int osMemShareResize(uint64_t handle, uint64_t size);

int osMemCheckRange(const void* address, uint64_t size, int writable);

int osSyncWaitAddress(volatile uint32_t* address, uint32_t expected);

int osSyncWakeAddress(volatile uint32_t* address, uint32_t count);

int osSysReadMonotonicTime(uint64_t* nanoseconds);

int osSysReadWallClockTime(uint64_t* nanoseconds);

int osSysReadClockResolution(uint64_t* nanoseconds);

int osSysConnectionLookup(const char* name, uint64_t* endpoint,
                          uint64_t* generation);

#ifdef __cplusplus
}
#endif
