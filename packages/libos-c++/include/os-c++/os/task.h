#ifndef _OS_TASK_H
#define _OS_TASK_H

#include <os/ipc.h>
#include <os/service.h>
#include <os/syscall.h>

namespace os {
namespace detail {
class TaskControl;
}
namespace wire {
struct TaskIdentifiers;
struct SignalAction;
}  // namespace wire

enum class ProgramId : uint64_t;
enum class ServiceRole : uint32_t;
class Task : private Sys::Transport {
   public:
    Task();
    ~Task();
    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&& other) noexcept;
    class Image {
       public:
        const void* getImage() const;
        uint64_t getImageSize() const;
        const char* getName() const;

        void setImage(const void* image, uint64_t size);
        void setName(const char* value);

       private:
        const void* image_{};
        uint64_t imageSize_{};
        const char* name_{};
    };
    class Spawn {
       public:
        enum class Flag : uint32_t { NewSession = 1, ManagedService = 2 };
        const void* getImage() const;
        uint64_t getImageSize() const;
        const char* getName() const;
        uint64_t getBootstrapHandle() const;
        uint64_t getStandardInputHandle() const;
        uint64_t getStandardOutputHandle() const;
        uint64_t getStandardErrorHandle() const;
        uint64_t getCapabilities() const;
        uint64_t getParentTaskId() const;
        uint32_t getArgumentCount() const;
        uint32_t getServiceRole() const;
        const char* const* getArguments() const;
        uint32_t getSharedImageCount() const;
        uint32_t getFlags() const;
        uint32_t getTerminalKind() const;
        uint32_t getTerminalIndex() const;
        const Image* getSharedImages() const;

        void setImage(const void* image, uint64_t size);
        void setName(const char* value);
        void setBootstrapHandle(uint64_t value);
        void setStandardInputHandle(uint64_t value);
        void setStandardOutputHandle(uint64_t value);
        void setStandardErrorHandle(uint64_t value);
        void setCapabilities(uint64_t value);
        void setParentTaskId(uint64_t value);
        void setServiceRole(uint32_t value);
        void setArguments(const char* const* arguments, uint32_t count);
        void setFlags(uint32_t value);
        void setTerminalKind(uint32_t value);
        void setTerminalIndex(uint32_t value);
        void setSharedImages(const Image* images, uint32_t count);

       private:
        const void* image_{};
        uint64_t imageSize_{};
        const char* name_{};
        uint64_t bootstrapHandle_{};
        uint64_t standardInputHandle_{};
        uint64_t standardOutputHandle_{};
        uint64_t standardErrorHandle_{};
        uint64_t capabilities_{};
        uint64_t parentTaskId_{};
        uint32_t argumentCount_{};
        uint32_t serviceRole_{};
        const char* const* arguments_{};
        uint32_t sharedImageCount_{};
        uint32_t flags_{};
        uint32_t terminalKind_{};
        uint32_t terminalIndex_{};
        const Image* sharedImages_{};
    };
    class Exec {
       public:
        const void* getImage() const;
        uint64_t getImageSize() const;
        const char* getName() const;
        uint32_t getArgumentCount() const;
        uint32_t getEnvironmentCount() const;
        const char* const* getArguments() const;
        const char* const* getEnvironment() const;
        const void* getRuntimeState() const;
        uint64_t getRuntimeStateSize() const;
        uint32_t getSharedImageCount() const;
        const Image* getSharedImages() const;

        void setImage(const void* image, uint64_t size);
        void setName(const char* value);
        void setArguments(const char* const* arguments, uint32_t count);
        void setEnvironment(const char* const* environment, uint32_t count);
        void setRuntimeState(const void* value);
        void setRuntimeStateSize(uint64_t value);
        void setSharedImages(const Image* images, uint32_t count);

       private:
        const void* image_{};
        uint64_t imageSize_{};
        const char* name_{};
        uint32_t argumentCount_{};
        uint32_t environmentCount_{};
        const char* const* arguments_{};
        const char* const* environment_{};
        const void* runtimeState_{};
        uint64_t runtimeStateSize_{};
        uint32_t sharedImageCount_{};
        uint32_t sharedReserved_{};
        const Image* sharedImages_{};
    };
    class Core {
       public:
        enum class State : uint32_t {
            None,
            Requested,
            Capturing,
            Ready,
            Writing,
            Complete,
            Truncated,
            Failed,
            Skipped,
            TimedOut
        };
        State getState() const;
        bool getEnabled() const;
        uint64_t getMaximumBytes() const;
        uint64_t getMaximumConcurrent() const;
        uint64_t getTimeoutMilliseconds() const;
        uint64_t getId() const;
        uint64_t getTaskId() const;
        uint64_t getStarted() const;
        uint64_t getBytes() const;
        uint64_t getDeadline() const;
        uint32_t getSignal() const;
        bool getTruncated() const;
        void setEnabled(bool value);
        void setMaximumBytes(uint64_t value);
        void setMaximumConcurrent(uint64_t value);
        void setTimeoutMilliseconds(uint64_t value);

       private:
        friend class Task;
        State state_{};
        bool enabled_{};
        uint64_t maximumBytes_{};
        uint64_t maximumConcurrent_{};
        uint64_t timeoutMilliseconds_{};
        uint64_t id_{};
        uint64_t taskId_{};
        uint64_t started_{};
        uint64_t bytes_{};
        uint64_t deadline_{};
        uint32_t signal_{};
        bool truncated_{};
    };

    class Signal;
    class Thread;
    class Session;
    class SignalStack;
    class Child;
    class ImageMapping;
    int refresh();
    int refreshExit();
    int open(uint64_t id);
    int openCurrent();
    int create(const Spawn& image);
    int close();
    int terminate(int exitCode);
    int wait(int& exitCode);
    int next(uint64_t minimumId, Task& task);
    int querySignalMask(uint64_t& mask);
    int queryPendingSignals(uint64_t& mask);
    int querySignalStack(SignalStack& stack);
    int queryCorePolicy(Core& policy);
    int queryResourceLimit(int resource, uint64_t& soft, uint64_t& hard);
    bool getValid() const;
    int getError() const;

    class Credential;
    int sendSignal(uint32_t signal);
    int waitSignal(uint64_t mask);
    int querySignalAction(uint32_t signal, Signal& action);
    uint32_t getState() const;
    int32_t getExitCode() const;
    const char* getName() const;
    uint32_t getUid() const;
    uint32_t getGid() const;
    uint32_t getEffectiveUid() const;
    uint32_t getEffectiveGid() const;
    uint64_t getParentId() const;
    uint32_t getTrustedDeviceService() const;
    uint32_t getServiceRole() const;
    uint64_t getGroupId() const;
    uint64_t getSessionId() const;
    uint64_t getTerminalForegroundGroup() const;
    uint64_t getStartedNanoseconds() const;
    uint64_t getCpuNanoseconds() const;
    uint32_t getTerminalKind() const;
    uint32_t getTerminalIndex() const;
    uint32_t getCommandLineSize() const;
    uint32_t getManagedService() const;
    const char* getCommandLine() const;
    uint64_t getMainThreadId() const;
    int32_t getNice() const;
    uint32_t getThreadCount() const;
    uint64_t getId() const;
    uint32_t getExitSignal() const;
    Core::State getExitCoreState() const;
    uint64_t getExitFaultThread() const;
    uint64_t getExitVector() const;
    uint64_t getExitError() const;
    uint64_t getExitRip() const;
    uint64_t getExitRsp() const;
    uint64_t getExitAddress() const;
    uint64_t getExitStarted() const;
    uint32_t getExitAddressValid() const;
    int32_t getExitSignalCode() const;

    int setNice(int value);
    int setGroup(uint64_t group);
    int setSignalMask(int how, uint64_t mask);
    int setSignalStack(const SignalStack& stack);
    int setCorePolicy(const Core& policy);
    int setResourceLimit(int resource, uint64_t soft, uint64_t hard);

    int setSignalAction(uint32_t signal, const Signal& action);

   private:
    friend class detail::TaskControl;
    uint64_t readId() const;
    explicit Task(uint64_t id);
    uint64_t id() const;

    bool kill(uint64_t taskId);
    int wait(uint64_t task, int* exitCode);
    uint64_t executeWithIo(const char* path, int argumentCount,
                           const char* const* arguments,
                           uint64_t standardInputHandle,
                           uint64_t standardOutputHandle,
                           uint64_t standardErrorHandle = 0);
    uint64_t execute(const char* path);
    uint64_t execute(const char* path, int argumentCount,
                     const char* const* arguments);
    uint64_t executeService(const char* path, const char* name);
    uint64_t executeService(const char* path, int argumentCount,
                            const char* const* arguments);

    int refreshTarget(uint64_t taskId = 0);
    int enumerate(uint64_t minimumId);
    int refreshHandle(uint64_t taskHandle);
    [[noreturn]] void signalReturn(void* frame);
    uint64_t spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                          uint32_t argumentCount       = 0,
                          const char* const* arguments = nullptr);
    uint64_t spawnImage(const Spawn& image);
    int64_t waitChild(int64_t taskId, int& status, int options);
    int waitThread(uint64_t threadId, int& exitCode, bool noHang = false);
    int terminate(uint64_t taskHandle, int exitCode);
    int sendSignal(uint64_t taskId, uint32_t signal);
    int sendThreadSignal(uint64_t threadId, uint32_t signal);
    int64_t createSession();
    int signalStack(const OsSignalStack* stack, OsSignalStack* oldStack);
    int coreLimit(bool set, uint64_t& soft, uint64_t& hard);
    int refreshExitTarget(uint64_t taskId = 0);
    int recordImage(const char* path, uint64_t begin, uint64_t end,
                    uint64_t offset);
    bool accessible(const void* address, uint64_t bytes, bool writable);
    int recordAuxiliary(uint64_t headers, uint64_t count, uint64_t entry,
                        uint64_t loader);

    uint64_t getThreadId();
    int getState(uint64_t taskHandle, int& exitCode);
    int getIdentifiers(wire::TaskIdentifiers& identifiers, uint64_t taskId = 0);
    int getPendingSignals(uint64_t& mask);
    int setGroupTarget(uint64_t taskGroupId, uint64_t taskId = 0);
    int setSignalAction(uint32_t signal, const wire::SignalAction* action,
                        wire::SignalAction* oldAction);
    int setSignalMask(int how, uint64_t mask, uint64_t& oldMask);
    int setCredentials(uint64_t taskId, uint32_t uid, uint32_t gid,
                       bool effectiveOnly = false);

    Ipc::Handle handle_;
    int error_{};
    void decode(const void* bytes);
    uint32_t exitSignal_{};
    uint32_t exitCoreState_{};
    uint64_t exitFaultThread_{};
    uint64_t exitVector_{};
    uint64_t exitError_{};
    uint64_t exitRip_{};
    uint64_t exitRsp_{};
    uint64_t exitAddress_{};
    uint64_t exitStarted_{};
    uint32_t exitAddressValid_{};
    int32_t exitSignalCode_{};
    uint64_t id_{};
    uint32_t state_{};
    int32_t exitCode_{};
    char name_[48]{};
    uint32_t uid_{};
    uint32_t gid_{};
    uint32_t effectiveUid_{};
    uint32_t effectiveGid_{};
    uint64_t parentId_{};
    uint32_t trustedDeviceService_{};
    uint32_t serviceRole_{};
    uint64_t taskGroupId_{};
    uint64_t sessionId_{};
    uint64_t terminalForegroundGroup_{};
    uint64_t startedNanoseconds_{};
    uint64_t cpuNanoseconds_{};
    uint32_t terminalKind_{};
    uint32_t terminalIndex_{};
    uint32_t commandLineSize_{};
    uint32_t managedService_{};
    char commandLine_[384]{};
    uint64_t mainThreadId_{};
    int32_t nice_{};
    uint32_t threadCount_{};

   private:
    uint64_t executeRequest(const char* path, int argumentCount,
                            const char* const* arguments, uint64_t input,
                            uint64_t output, uint64_t error, bool service);
};

class Task::Signal {
   public:
    uint64_t getHandler() const;
    uint64_t getMask() const;
    uint64_t getFlags() const;
    uint64_t getRestorer() const;
    void setHandler(uint64_t value);
    void setMask(uint64_t value);
    void setFlags(uint64_t value);
    void setRestorer(uint64_t value);

   private:
    uint64_t handler_{};
    uint64_t mask_{};
    uint64_t flags_{};
    uint64_t restorer_{};
};

class Task::Credential {
   public:
    explicit Credential(const Task& task);
    uint32_t getMaximumGroups() const;
    int refresh();

    uint32_t getUid() const;
    uint32_t getEffectiveUid() const;
    uint32_t getSavedUid() const;
    uint32_t getGid() const;
    uint32_t getEffectiveGid() const;
    uint32_t getSavedGid() const;
    uint32_t getGroupCount() const;
    uint32_t getGroup(uint32_t index) const;

    int setUserIds(uint32_t real, uint32_t effective, uint32_t saved);
    int setGroupIds(uint32_t real, uint32_t effective, uint32_t saved);
    int setGroups(const uint32_t* groups, uint32_t count);

   private:
    uint64_t taskId_;
    uint32_t uid_{};
    uint32_t effectiveUid_{};
    uint32_t savedUid_{};
    uint32_t gid_{};
    uint32_t effectiveGid_{};
    uint32_t savedGid_{};
    uint32_t groupCount_{};
    uint32_t groups_[32]{};
};

class Task::SignalStack {
   public:
    uint64_t getAddress() const;
    uint64_t getSize() const;
    uint32_t getFlags() const;
    void setAddress(uint64_t value);
    void setSize(uint64_t value);
    void setFlags(uint32_t value);

   private:
    uint64_t address_{}, size_{};
    uint32_t flags_{};
};
class Task::Child {
   public:
    uint64_t getId() const;
    uint32_t getUid() const;
    uint32_t getCode() const;
    int32_t getStatus() const;

   private:
    friend class Task;
    uint64_t id_{};
    uint32_t uid_{}, code_{};
    int32_t status_{};
};
class Task::ImageMapping {
   public:
    uint64_t getBegin() const;
    uint64_t getEnd() const;
    uint64_t getOffset() const;
    const char* getPath() const;

   private:
    uint64_t begin_{}, end_{}, offset_{};
    char path_[192]{};
};
class Task::Thread {
   public:
    Thread();
    ~Thread();
    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread& operator=(Thread&& other) noexcept;
    int refresh();
    int open(uint64_t taskId, uint64_t threadId);
    int openCurrent();
    int create(void (*entry)(void*), void* argument, void* stack,
               size_t stackSize);
    int close();
    int wait(int& exitCode);
    int sendSignal(uint32_t signal);
    uint64_t getId() const;
    uint64_t getTaskId() const;
    uint64_t getCpuNanoseconds() const;
    int32_t getNice() const;
    uint32_t getState() const;
    bool getValid() const;
    int getError() const;
    int setNice(int value);

   private:
    uint64_t id_{}, taskId_{}, cpuNanoseconds_{};
    int32_t nice_{};
    uint32_t state_{};
    int error_{};
};
class Task::Session {
   public:
    Session();
    ~Session();
    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session& operator=(Session&& other) noexcept;
    int refresh();
    int openCurrent();
    int create();
    int close();
    uint64_t getId() const;
    uint64_t getLeaderId() const;
    uint64_t getForegroundGroup() const;
    uint32_t getTerminalKind() const;
    uint32_t getTerminalIndex() const;
    bool getValid() const;
    int getError() const;
    int setForegroundGroup(uint64_t group);

   private:
    uint64_t id_{}, leader_{}, foreground_{};
    uint32_t terminalKind_{}, terminalIndex_{};
    int error_{};
};

}  // namespace os

#endif
