#pragma once

#include <stddef.h>
#include <stdint.h>

namespace os {
enum class DirectoryScope { User, System };
enum class DirectoryKind { Data, State, Config, Cache, Runtime, Log };

class Directories {
   public:
    // Returns the supplied buffer, or nullptr on invalid input/insufficient space.
    // Queries never create directories. The caller owns the returned storage.
    static char* path(DirectoryKind kind, DirectoryScope scope, char* buffer,
                      size_t capacity, const char* application = nullptr);
    static char* home(char* buffer, size_t capacity);
    static char* data(DirectoryScope scope, char* buffer, size_t capacity,
                      const char* application = nullptr) {
        return path(DirectoryKind::Data, scope, buffer, capacity, application);
    }
    static char* state(DirectoryScope scope, char* buffer, size_t capacity,
                       const char* application = nullptr) {
        return path(DirectoryKind::State, scope, buffer, capacity, application);
    }
    static char* config(DirectoryScope scope, char* buffer, size_t capacity,
                        const char* application = nullptr) {
        return path(DirectoryKind::Config, scope, buffer, capacity, application);
    }
    static char* cache(DirectoryScope scope, char* buffer, size_t capacity,
                       const char* application = nullptr) {
        return path(DirectoryKind::Cache, scope, buffer, capacity, application);
    }
    static char* runtime(DirectoryScope scope, char* buffer, size_t capacity,
                         const char* application = nullptr) {
        return path(DirectoryKind::Runtime, scope, buffer, capacity, application);
    }
    static char* log(DirectoryScope scope, char* buffer, size_t capacity,
                     const char* application = nullptr) {
        return path(DirectoryKind::Log, scope, buffer, capacity, application);
    }
    // Index zero is the user directory, followed by the valid absolute entries
    // in XDG_DATA_DIRS/XDG_CONFIG_DIRS, in their original order.
    static char* dataSearch(uint32_t index, char* buffer, size_t capacity,
                            const char* application = nullptr);
    static char* configSearch(uint32_t index, char* buffer, size_t capacity,
                              const char* application = nullptr);
   private:
    static bool userId(uint32_t& uid);
    static bool validApplication(const char* application);
    static char* append(const char* base, const char* application,
                        char* buffer, size_t capacity);
    static char* search(bool configuration, uint32_t index, char* buffer,
                        size_t capacity, const char* application);
};
}  // namespace os
