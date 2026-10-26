#pragma once
#include <cstddef>
#include <cstdint>

#include <os/syscall.h>
namespace os {
namespace detail {
class PipeClient;
class QueueClient;
}  // namespace detail
constexpr uint32_t ipcDataWordCount = 515;
struct IpcMessage {
    uint64_t type;
    uint64_t senderPid;
    uint64_t senderTid;
    uint64_t replyToken;
    uint32_t length;
    uint32_t handleCount;
    uint64_t data[ipcDataWordCount];
    uint64_t handles[4];
    uint32_t handleRights[4];
    uint32_t handleOptions[4];
};
class Ipc {
   public:
    class Handle {
       public:
        enum class Flag : uint32_t { NoInherit = 1, CloseOnExec = 2 };
        explicit Handle(uint64_t value = 0, bool owned = true);
        ~Handle();
        Handle(const Handle&)            = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept;
        Handle& operator=(Handle&& other) noexcept;
        uint64_t release();
        int reset(uint64_t value = 0, bool owned = true);
        int restrictRights(uint32_t rights);
        int destroyEndpoint();
        int requestActive(uint64_t token) const;
        int queryState() const;
        uint64_t queryIdentity() const;
        int64_t queryReferences() const;
        int adopt(uint64_t handle);
        int borrow(uint64_t handle);
        int duplicate(uint32_t rights, uint32_t flags, Handle& result) const;
        int close();
        uint64_t getValue() const;
        bool getValid() const;
        bool getOwned() const;
        int setFlags(uint32_t flags);

       private:
        int64_t invoke(uint64_t operation, uint64_t rights = 0,
                       uint64_t flags = 0) const;
        uint64_t value_{};
        bool owned_{};
    };
    class Msg {
       public:
        enum class HandleOption : uint32_t { RestrictRights = 1, Move = 2 };
        Msg();
        ~Msg();
        Msg(const Msg&)            = delete;
        Msg& operator=(const Msg&) = delete;
        Msg(Msg&& other) noexcept;
        Msg& operator=(Msg&& other) noexcept;
        int clear();
        int read(size_t offset, void* buffer, size_t size) const;
        int write(size_t offset, const void* buffer, size_t size);
        int attachHandle(Handle& handle, bool transfer);
        int takeHandle(uint32_t index, Handle& handle);
        uint64_t getType() const;
        size_t getSize() const;
        size_t getCapacity() const;
        uint32_t getHandleCount() const;
        uint64_t getSenderId() const;
        uint64_t getSenderThreadId() const;
        void setType(uint64_t type);

       private:
        friend class Ipc;
        IpcMessage wire_{};
        size_t size_{};
        bool owned_[4]{};
        void sent(bool success);
        int accepted();
    };
    class Pipe {
       public:
        Pipe();
        ~Pipe();
        Pipe(const Pipe&)            = delete;
        Pipe& operator=(const Pipe&) = delete;
        Pipe(Pipe&&) noexcept;
        Pipe& operator=(Pipe&&) noexcept;
        int create(bool duplex = false);
        int close();
        int64_t read(void* buffer, size_t size);
        int64_t write(const void* buffer, size_t size);
        bool signalEof();
        uint64_t releaseReadHandle();
        uint64_t releaseWriteHandle();

        uint64_t getIdentity() const;
        uint64_t getReadHandle() const;
        uint64_t getWriteHandle() const;

        bool getValid() const;
        int getError() const;

        bool setNonblocking(bool enabled);

       private:
        friend class detail::PipeClient;
        explicit Pipe(uint64_t handle);
        uint64_t queryIdentity() const;
        Handle read_, write_;
        Handle cleanup_[4];
        uint64_t identity_{};
        int error_{};
    };
    class MsgQueue {
       public:
        MsgQueue();
        ~MsgQueue();
        MsgQueue(const MsgQueue&)            = delete;
        MsgQueue& operator=(const MsgQueue&) = delete;
        int close();
        int open(const char* name, int flags, uint32_t maximumMessages,
                 uint32_t messageSize);
        int unlink(const char* name = nullptr);
        uint64_t release();
        int send(const void* data, size_t size, uint32_t priority);
        int64_t receive(void* data, size_t capacity, uint32_t& priority);
        int refresh();

        bool getValid() const;
        int getError() const;
        uint64_t getHandle() const;
        uint64_t getFlags() const;
        uint64_t getMaximumMessages() const;
        uint64_t getMessageSize() const;
        uint64_t getCurrentMessages() const;

        int setFlags(uint64_t flags, uint64_t* previous = nullptr);
        int setNotification(int type, uint32_t signal);

       private:
        friend class detail::QueueClient;
        explicit MsgQueue(uint64_t handle);
        int error_{};
        uint64_t flags_{};
        uint64_t maximumMessages_{};
        uint64_t messageSize_{};
        uint64_t currentMessages_{};
        Handle handle_;
        Handle cleanup_[4];
        char name_[64]{};
    };
    Ipc();
    ~Ipc();
    Ipc(const Ipc&)            = delete;
    Ipc& operator=(const Ipc&) = delete;
    Ipc& operator=(Ipc&& other) noexcept;
    int destroy();
    int send(Msg& message, uint64_t deadline);
    int receive(Msg& message, uint64_t deadline);
    int call(Msg& request, Msg& reply, uint64_t deadline);
    int reply(const Msg& request, Msg& reply);
    int cancel(uint64_t threadId);
    int open(Handle&& handle);
    int create();
    int close();
    uint64_t getHandle() const;
    bool getValid() const;
    int getError() const;
    uint64_t getInfiniteDeadline() const;

   private:
    Handle handle_;
    int error_{};
};

static_assert(sizeof(IpcMessage) == 4224);
}  // namespace os
