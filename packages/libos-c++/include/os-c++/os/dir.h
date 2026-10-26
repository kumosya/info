#pragma once
#include <cstddef>
#include <cstdint>

#include <os/file.h>
namespace os {
class Dir {
   public:
    enum class Scope : int { User = 0, System = 1 };
    enum class Kind : int {
        Data    = 0,
        State   = 1,
        Config  = 2,
        Cache   = 3,
        Runtime = 4,
        Log     = 5
    };
    class Entry {
       public:
        const char* getName() const;
        File::Type getType() const;
        uint64_t getInode() const;
        uint64_t getNextOffset() const;

       private:
        friend class Dir;
        char name_[256]{};
        File::Type type_{};
        uint64_t inode_{}, nextOffset_{};
    };
    Dir();
    ~Dir();
    Dir(const Dir&)            = delete;
    Dir& operator=(const Dir&) = delete;
    Dir(Dir&& other) noexcept;
    Dir& operator=(Dir&& other) noexcept;
    int open(const char* path);
    int close();
    int rewind();
    int create(const char* path, uint32_t mode = 0755);
    int remove(const char* path);
    int rename(const char* existing, const char* target);
    int readNext(Entry& entry);
    int readBatch(Entry* entries, size_t capacity, size_t& count);
    int open(Scope scope, const char* application = nullptr);

    char* getPath(Kind kind, char* buffer, size_t capacity);
    char* getHome(char* buffer, size_t capacity);
    char* getData(char* buffer, size_t capacity);
    char* getState(char* buffer, size_t capacity);
    char* getConfig(char* buffer, size_t capacity);
    char* getCache(char* buffer, size_t capacity);
    char* getRuntime(char* buffer, size_t capacity);
    char* getLog(char* buffer, size_t capacity);
    char* getDataSearch(uint32_t index, char* buffer, size_t capacity);
    char* getConfigSearch(uint32_t index, char* buffer, size_t capacity);
    const char* getPath() const;
    bool getOpened() const;
    uint64_t getOffset() const;
    int getError() const;

   private:
    uint64_t service_{}, descriptor_{}, offset_{};
    char path_[384]{};
    Scope scope_{Scope::User};
    char application_[128]{};
    bool ended_{};
    int error_{};
    Entry cache_[14];
    size_t cacheIndex_{}, cacheCount_{};
    uint64_t service();
    bool userId(uint32_t& uid);
    bool validApplication(const char* application);
    char* append(const char* base, const char* application, char* buffer,
                 size_t capacity);
    char* search(bool configuration, uint32_t index, char* buffer,
                 size_t capacity);
};
}  // namespace os
