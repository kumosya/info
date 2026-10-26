#pragma once
#include <os/ipc.h>
namespace os {
namespace detail {
class PtyClient;
}
class Pty {
   public:
    Pty();
    ~Pty();
    int adopt(uint64_t master, uint64_t slave);
    int borrow(uint64_t master);
    int open(Ipc::Handle&& master);
    int close();
    int queryForegroundGroup(uint64_t& group);
    Pty(const Pty&)            = delete;
    Pty& operator=(const Pty&) = delete;
    Pty(Pty&&) noexcept;
    Pty& operator=(Pty&&) noexcept;
    int create();
    uint64_t releaseMaster();
    uint64_t releaseSlave();
    bool refreshAttributes();
    bool applyAttributes();
    bool refreshWindowSize();
    bool applyWindowSize();

    uint64_t getMasterHandle() const;
    uint64_t getSlaveHandle() const;
    uint32_t getNumber() const;
    bool getValid() const;
    int getError() const;
    bool getCanonicalMode() const;
    bool getEcho() const;
    bool getSignalMode() const;
    uint8_t getEraseCharacter() const;
    uint8_t getInterruptCharacter() const;
    uint8_t getEofCharacter() const;
    uint8_t getSuspendCharacter() const;
    uint8_t getQuitCharacter() const;
    uint32_t getRows() const;
    uint32_t getColumns() const;
    uint32_t getPixelWidth() const;
    uint32_t getPixelHeight() const;

    bool setNonblocking(bool enabled);
    bool setForegroundGroup(uint64_t group);
    void setCanonicalMode(bool value);
    void setEcho(bool value);
    void setSignalMode(bool value);
    void setEraseCharacter(uint8_t value);
    void setInterruptCharacter(uint8_t value);
    void setEofCharacter(uint8_t value);
    void setSuspendCharacter(uint8_t value);
    void setQuitCharacter(uint8_t value);
    void setRows(uint32_t value);
    void setColumns(uint32_t value);
    void setPixelWidth(uint32_t value);
    void setPixelHeight(uint32_t value);

   private:
    friend class detail::PtyClient;
    explicit Pty(uint64_t handle);
    bool refreshNumber();
    uint32_t number_{};
    int error_{};
    bool canonicalMode_{};
    bool echo_{};
    bool signalMode_{};
    uint8_t eraseCharacter_{};
    uint8_t interruptCharacter_{};
    uint8_t eofCharacter_{};
    uint8_t suspendCharacter_{};
    uint8_t quitCharacter_{};
    uint32_t rows_{};
    uint32_t columns_{};
    uint32_t pixelWidth_{};
    uint32_t pixelHeight_{};
    Ipc::Handle master_, slave_, service_;
    bool request(uint64_t operation, const void* input, size_t inputSize,
                 void* output, size_t outputSize);
};
}  // namespace os
