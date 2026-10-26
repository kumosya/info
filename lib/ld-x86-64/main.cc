#include "loader-api.h"
#include <os/device.h>
#include <os/elf.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"

namespace {
intptr_t recovery[5];
bool recovering = false;
char failureText[192];
class SourceAllocator {
   public:
    void* allocate(size_t bytes) {
        return allocate_ ? allocate_(bytes)
                         : os::Runtime::allocateMemory(bytes);
    }
    void release(void* source) {
        if (release_)
            release_(source);
        else
            os::Runtime::freeMemory(source);
    }
    void bind(uint64_t allocate, uint64_t release) {
        allocate_ = reinterpret_cast<void* (*)(size_t)>(allocate);
        release_  = reinterpret_cast<bool (*)(void*)>(release);
    }

   private:
    void* (*allocate_)(size_t) = nullptr;
    bool (*release_)(void*)    = nullptr;
};
SourceAllocator sourceAllocator;
class DynamicLinker {
    static constexpr uint32_t imageLimit = 32, programLimit = 128,
                              dependencyLimit = 32;
    static constexpr uint64_t page = 4096, addressLimit = 0x800000000000ULL;
    using Function = void (*)();
    struct Image {
        char path[512]{}, name[256]{};
        os::ElfProgramHeader programs[programLimit]{};
        uint32_t programCount = 0;
        uint64_t bias = 0, begin = 0, end = 0;
        os::ElfDynamic* dynamic = nullptr;
        uint64_t dynamicCount   = 0;
        const char* strings     = nullptr;
        uint64_t stringSize     = 0;
        os::ElfSymbol* symbols  = nullptr;
        uint32_t* hash          = nullptr;
        uint32_t symbolCount    = 0;
        os::ElfRela *rela = nullptr, *plt = nullptr;
        uint64_t relaSize = 0, pltSize = 0;
        uint64_t needed[dependencyLimit]{};
        uint32_t neededCount = 0, dependencies[dependencyLimit]{}, state = 0;
        uint64_t runpath = 0;
        bool hasRunpath = false, global = true;
        uint32_t references = 1;
        uint64_t mappedEnd  = 0;
        Function init = nullptr, fini = nullptr;
        Function *initializers = nullptr, *finalizers = nullptr;
        uint64_t initializerSize = 0, finalizerSize = 0;
    } images_[imageLimit];
    uint32_t imageCount_ = 1, initialized_[imageLimit]{}, initializedCount_ = 0;
    uint64_t* stack_        = nullptr;
    char** environment_     = nullptr;
    bool secure_            = false;
    uint64_t loaderAddress_ = 0, owner_ = 0;
    uint32_t transaction_ = imageLimit;
    void* loadingBuffer_  = nullptr;
    // SVR4 debugger rendezvous. DT_DEBUG points here after the link set is
    // complete.
    struct LinkMap {
        uint64_t bias;
        const char* name;
        os::ElfDynamic* dynamic;
        LinkMap* next;
        LinkMap* previous;
    } maps_[imageLimit]{};
    struct DebugState {
        int version;
        int padding;
        LinkMap* map;
        uint64_t breakpoint;
        int state;
        int padding2;
        uint64_t loader;
    } debug_{};
    static void debuggerBreakpoint() { asm volatile("" : : : "memory"); }
    void publishDebugState(uint64_t loader) {
        for (uint32_t i = 0; i < imageCount_; ++i)
            maps_[i] = {images_[i].bias, images_[i].path, images_[i].dynamic,
                        i + 1 < imageCount_ ? &maps_[i + 1] : nullptr,
                        i ? &maps_[i - 1] : nullptr};
        debug_ = {1, 0, maps_, reinterpret_cast<uint64_t>(debuggerBreakpoint),
                  0, 0, loader};
        auto& main = images_[0];
        for (uint64_t i = 0; i < main.dynamicCount && main.dynamic[i].tag; ++i)
            if (main.dynamic[i].tag == 21 &&
                contains(main, reinterpret_cast<uint64_t>(&main.dynamic[i]),
                         sizeof(os::ElfDynamic), 2))
                if (main.dynamic[i].value !=
                    reinterpret_cast<uint64_t>(&debug_))
                    main.dynamic[i].value = reinterpret_cast<uint64_t>(&debug_);
        debuggerBreakpoint();
    }

    static uint64_t down(uint64_t value) { return value & ~(page - 1); }
    static uint64_t up(uint64_t value) {
        return (value + page - 1) & ~(page - 1);
    }
    static bool range(uint64_t offset, uint64_t size, uint64_t limit) {
        return offset <= limit && size <= limit - offset;
    }
    [[noreturn]] static void fail(const char* reason,
                                  const char* detail = nullptr) {
        if (recovering) {
            copy(failureText, sizeof(failureText), reason);
            if (detail) {
                size_t used = os::Runtime::stringLength(failureText);
                if (used + 3 < sizeof(failureText)) {
                    failureText[used++] = ':';
                    failureText[used++] = ' ';
                    copy(failureText + used, sizeof(failureText) - used,
                         detail);
                }
            }
            __builtin_longjmp(recovery, 1);
        }
        os::Sys().log("[LD] ");
        os::Sys().log(reason);
        if (detail) {
            os::Sys().log(": ");
            os::Sys().log(detail);
        }
        os::Sys().log("\n");
        const uint64_t handle = os::getStandardHandle(2);
        if (handle) {
            os::detail::PipeClient(handle).write("ld-osinfo: ", 11);
            os::detail::PipeClient(handle).write(reason, os::Runtime::stringLength(reason));
            if (detail) {
                os::detail::PipeClient(handle).write(": ", 2);
                os::detail::PipeClient(handle).write(detail,
                                       os::Runtime::stringLength(detail));
            }
            os::detail::PipeClient(handle).write("\n", 1);
        }
        os::exitTask(127);
    }
    static bool contains(const Image& image, uint64_t address, uint64_t size,
                         uint32_t flags = 0) {
        if (!size || address < image.bias) return false;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type != 1 || (segment.flags & flags) != flags) continue;
            const uint64_t start = image.bias + segment.address;
            if (address >= start &&
                range(address - start, size, segment.memorySize))
                return true;
        }
        return false;
    }
    static uint64_t pointer(const Image& image, uint64_t value,
                            uint64_t bytes) {
        if (value > addressLimit - image.bias ||
            !contains(image, image.bias + value, bytes))
            fail("dynamic pointer outside load segments", image.path);
        return image.bias + value;
    }
    static const char* string(const Image& image, uint64_t offset) {
        if (offset >= image.stringSize)
            fail("invalid dynamic string", image.path);
        const char* value = image.strings + offset;
        for (uint64_t i = offset; i < image.stringSize; ++i)
            if (!image.strings[i]) return value;
        fail("unterminated dynamic string", image.path);
    }
    static bool copy(char* target, size_t capacity, const char* source) {
        if (os::Runtime::stringLength(source) >= capacity) return false;
        os::Runtime::copyString(target, source, capacity);
        return true;
    }
    static bool prefix(const char* text, const char* expected, size_t count) {
        for (size_t i = 0; i < count; ++i)
            if (text[i] != expected[i]) return false;
        return true;
    }
    static void dynamic(Image& image) {
        uint64_t hash = 0, strings = 0, symbols = 0, rela = 0, plt = 0;
        uint64_t init = 0, fini = 0, initArray = 0, finiArray = 0;
        bool terminated = false;
        for (uint64_t i = 0; i < image.dynamicCount; ++i) {
            const auto entry = image.dynamic[i];
            if (!entry.tag) {
                terminated = true;
                break;
            }
            switch (entry.tag) {
                case 1:
                    if (image.neededCount == dependencyLimit)
                        fail("too many dependencies", image.path);
                    image.needed[image.neededCount++] = entry.value;
                    break;
                case 2:
                    image.pltSize = entry.value;
                    break;
                case 4:
                    hash = entry.value;
                    break;
                case 5:
                    strings = entry.value;
                    break;
                case 6:
                    symbols = entry.value;
                    break;
                case 7:
                    rela = entry.value;
                    break;
                case 8:
                    image.relaSize = entry.value;
                    break;
                case 9:
                    if (entry.value != sizeof(os::ElfRela))
                        fail("unsupported RELA format", image.path);
                    break;
                case 10:
                    image.stringSize = entry.value;
                    break;
                case 11:
                    if (entry.value != sizeof(os::ElfSymbol))
                        fail("unsupported symbol format", image.path);
                    break;
                case 12:
                    init = entry.value;
                    break;
                case 13:
                    fini = entry.value;
                    break;
                case 15:
                    if (!image.hasRunpath) image.runpath = entry.value;
                    break;
                case 17:
                case 18:
                case 19:
                case 22:
                case 35:
                case 36:
                case 37:
                    fail("unsupported REL/text/RELR relocation", image.path);
                case 20:
                    if (entry.value != 7) fail("PLT must use RELA", image.path);
                    break;
                case 23:
                    plt = entry.value;
                    break;
                case 25:
                    initArray = entry.value;
                    break;
                case 26:
                    finiArray = entry.value;
                    break;
                case 27:
                    image.initializerSize = entry.value;
                    break;
                case 28:
                    image.finalizerSize = entry.value;
                    break;
                case 29:
                    image.runpath    = entry.value;
                    image.hasRunpath = true;
                    break;
                case 0x6ffffffe:
                    if (entry.value)
                        fail("symbol versions are unsupported", image.path);
                    break;
                default:
                    break;
            }
        }
        if (!terminated || !hash || !strings || !symbols || !image.stringSize)
            fail("incomplete dynamic tables (SYSV hash required)", image.path);
        image.strings = reinterpret_cast<const char*>(
            pointer(image, strings, image.stringSize));
        image.hash = reinterpret_cast<uint32_t*>(pointer(image, hash, 8));
        const uint64_t buckets = image.hash[0], chains = image.hash[1];
        if (!buckets || !chains || chains > 65536 || buckets > 65536)
            fail("invalid ELF hash table", image.path);
        pointer(image, hash, (2 + buckets + chains) * 4);
        image.symbolCount = chains;
        image.symbols     = reinterpret_cast<os::ElfSymbol*>(
            pointer(image, symbols, chains * sizeof(os::ElfSymbol)));
        if (image.relaSize % sizeof(os::ElfRela) ||
            image.pltSize % sizeof(os::ElfRela))
            fail("invalid relocation size", image.path);
        if (image.relaSize)
            image.rela = reinterpret_cast<os::ElfRela*>(
                pointer(image, rela, image.relaSize));
        if (image.pltSize)
            image.plt = reinterpret_cast<os::ElfRela*>(
                pointer(image, plt, image.pltSize));
        if (init)
            image.init = reinterpret_cast<Function>(pointer(image, init, 1));
        if (fini)
            image.fini = reinterpret_cast<Function>(pointer(image, fini, 1));
        if (image.initializerSize % 8 || image.finalizerSize % 8)
            fail("invalid constructor array", image.path);
        if (image.initializerSize)
            image.initializers = reinterpret_cast<Function*>(
                pointer(image, initArray, image.initializerSize));
        if (image.finalizerSize)
            image.finalizers = reinterpret_cast<Function*>(
                pointer(image, finiArray, image.finalizerSize));
        if (image.hasRunpath || image.runpath) string(image, image.runpath);
        for (uint32_t i = 0; i < image.neededCount; ++i)
            string(image, image.needed[i]);
    }
    static void segments(Image& image) {
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 7)
                fail("TLS segments are unsupported", image.path);
            if (segment.type == 2) {
                if (image.dynamic || !segment.memorySize ||
                    segment.memorySize % sizeof(os::ElfDynamic))
                    fail("invalid PT_DYNAMIC", image.path);
                image.dynamic = reinterpret_cast<os::ElfDynamic*>(
                    pointer(image, segment.address, segment.memorySize));
                image.dynamicCount =
                    segment.memorySize / sizeof(os::ElfDynamic);
            }
        }
        if (!image.dynamic) fail("missing PT_DYNAMIC", image.path);
        dynamic(image);
    }
    static void* readFile(const char* path, uint64_t& bytes) {
        os::File file;
        if (file.open(path, O_RDONLY) || file.refresh()) return nullptr;
        bytes = file.getSize();
        if (file.getType() != os::File::Type::File || !bytes ||
            bytes > 32 * 1024 * 1024)
            return nullptr;
        void* buffer = sourceAllocator.allocate(bytes);
        if (!buffer) return nullptr;
        uint64_t offset = 0;
        while (offset < bytes) {
            const auto count = file.read(static_cast<uint8_t*>(buffer) + offset,
                                         bytes - offset);
            if (count <= 0) {
                sourceAllocator.release(buffer);
                return nullptr;
            }
            offset += count;
        }
        return buffer;
    }
    static bool candidate(char* path, size_t capacity, const char* directory,
                          size_t length, const char* name) {
        const size_t suffix = os::Runtime::stringLength(name);
        if (!length || directory[0] != '/' || length + suffix + 2 > capacity)
            return false;
        os::Runtime::copyBytes(path, directory, length);
        path[length] = '/';
        os::Runtime::copyBytes(path + length + 1, name, suffix + 1);
        os::File file;
        return file.refresh(path) == 0 &&
               file.getType() == os::File::Type::File;
    }
    static bool search(char* path, const char* entries, const char* name,
                       const Image* origin = nullptr) {
        if (!entries) return false;
        while (*entries) {
            const char* end = entries;
            while (*end && *end != ':') ++end;
            char directory[512];
            size_t count = 0;
            for (const char* cursor = entries; cursor < end;) {
                size_t token = 0;
                if (end - cursor >= 7 && prefix(cursor, "$ORIGIN", 7))
                    token = 7;
                if (end - cursor >= 9 && prefix(cursor, "${ORIGIN}", 9))
                    token = 9;
                if (token && origin) {
                    size_t length = os::Runtime::stringLength(origin->path);
                    while (length > 1 && origin->path[length - 1] != '/')
                        --length;
                    if (length > 1) --length;
                    if (length >= sizeof(directory) - count)
                        fail("RUNPATH too long", origin->path);
                    os::Runtime::copyBytes(directory + count, origin->path,
                                           length);
                    count += length;
                    cursor += token;
                } else {
                    if (count + 1 == sizeof(directory))
                        fail("library search path too long");
                    directory[count++] = *cursor++;
                }
            }
            directory[count] = 0;
            if (candidate(path, 512, directory, count, name)) return true;
            entries = *end ? end + 1 : end;
        }
        return false;
    }
    void locate(const Image& requester, const char* name, char* path) {
        if (!*name || os::Runtime::stringLength(name) >= 256)
            fail("invalid dependency name", requester.path);
        for (const char* p = name; *p; ++p)
            if (*p == '/') {
                if (*name != '/' || !copy(path, 512, name))
                    fail("dependency path must be absolute", name);
                return;
            }
        if ((!secure_ &&
             search(path, os::Runtime::environmentValue("LD_LIBRARY_PATH"),
                    name)) ||
            (!secure_ && (requester.hasRunpath || requester.runpath) &&
             search(path, string(requester, requester.runpath), name,
                    &requester)) ||
            search(path, "/lib:/usr/lib:/usr/local/lib", name))
            return;
        fail("library not found", name);
    }
    void load(Image& image) {
        uint64_t bytes = 0;
        void* source   = readFile(image.path, bytes);
        if (!source) fail("cannot read library", image.path);
        loadingBuffer_ = source;
        char interpreter[256];
        os::Executable executable;
        const int validation = executable.open(source, bytes);
        const int parsed     = validation ? validation
                                          : executable.getInterpreter(
                                            interpreter, sizeof(interpreter));
        const auto& header   = *static_cast<os::ElfHeader*>(source);
        if (parsed != 0 || header.type != 3 ||
            header.programCount > programLimit)
            fail("dependency must be ELF64 ET_DYN", image.path);
        image.programCount = header.programCount;
        os::Runtime::copyBytes(
            image.programs,
            static_cast<uint8_t*>(source) + header.programOffset,
            image.programCount * sizeof(os::ElfProgramHeader));
        uint64_t minimum = UINT64_MAX, maximum = 0, total = 0;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type != 1 || !segment.memorySize) continue;
            if (segment.fileSize > segment.memorySize ||
                !range(segment.offset, segment.fileSize, bytes) ||
                !range(segment.address, segment.memorySize, addressLimit) ||
                (segment.flags & ~7U) || (segment.flags & 3) == 3 ||
                (segment.alignment > 1 &&
                 ((segment.alignment & (segment.alignment - 1)) ||
                  (segment.address % segment.alignment !=
                   segment.offset % segment.alignment))))
                fail("invalid library segment", image.path);
            if (down(segment.address) < minimum)
                minimum = down(segment.address);
            if (up(segment.address + segment.memorySize) > maximum)
                maximum = up(segment.address + segment.memorySize);
            total += up(segment.memorySize);
            for (uint32_t j = 0; j < i; ++j) {
                const auto& other = image.programs[j];
                if (other.type == 1 && other.memorySize &&
                    segment.address < other.address + other.memorySize &&
                    other.address < segment.address + segment.memorySize)
                    fail("overlapping library segments", image.path);
            }
        }
        if (minimum == UINT64_MAX || maximum <= minimum ||
            total > 256 * 1024 * 1024 || maximum - minimum > 256 * 1024 * 1024)
            fail("library address range too large", image.path);
        image.begin = 0x1000000000ULL + (&image - images_) * 0x40000000ULL;
        image.bias  = image.begin - minimum;
        image.end   = image.bias + maximum;
        // Map each page once, writable and NX during relocation. No RWX stage.
        for (uint64_t address = minimum; address < maximum; address += page) {
            uint32_t flags = pageFlags(image, address);
            if (!flags) continue;
            if ((flags & 3) == 3) fail("library page violates W^X", image.path);
            if (os::mapMemory(image.bias + address, page, 1) !=
                image.bias + address)
                fail("library mapping failed", image.path);
            image.mappedEnd = image.bias + address + page;
        }
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 1 && segment.fileSize)
                os::Runtime::copyBytes(
                    reinterpret_cast<void*>(image.bias + segment.address),
                    static_cast<uint8_t*>(source) + segment.offset,
                    segment.fileSize);
        }
        sourceAllocator.release(source);
        loadingBuffer_ = nullptr;
        segments(image);
    }
    static uint32_t pageFlags(const Image& image, uint64_t address) {
        uint32_t flags = 0;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 1 && segment.memorySize &&
                address < segment.address + segment.memorySize &&
                segment.address < address + page)
                flags |= segment.flags | 4;
        }
        return flags;
    }
    static uint32_t hash(const char* name) {
        uint32_t value = 0;
        while (*name) {
            value               = (value << 4) + static_cast<uint8_t>(*name++);
            const uint32_t high = value & 0xf0000000U;
            value ^= high >> 24;
            value &= ~high;
        }
        return value;
    }
    bool lookupIn(const Image& image, const char* name,
                  uint64_t& address) const {
        uint32_t index = image.hash[2 + hash(name) % image.hash[0]];
        for (uint32_t seen = 0; index; ++seen) {
            if (seen >= image.symbolCount || index >= image.symbolCount)
                fail("corrupt symbol hash chain", image.path);
            const auto symbol     = image.symbols[index];
            const uint8_t binding = symbol.info >> 4;
            if (symbol.section &&
                (binding == 1 || binding == 2 || binding == 10) &&
                ((symbol.visibility & 3) == 0 ||
                 (symbol.visibility & 3) == 3) &&
                os::Runtime::stringsEqual(string(image, symbol.name), name)) {
                if ((symbol.info & 15) == 6 || (symbol.info & 15) == 10)
                    fail("TLS/IFUNC symbol unsupported", name);
                address = symbol.section == 0xfff1 ? symbol.value
                                                   : image.bias + symbol.value;
                return true;
            }
            index = image.hash[2 + image.hash[0] + index];
        }
        return false;
    }
    bool lookup(const char* name, uint64_t& address,
                bool skipMain = false) const {
        for (uint32_t i = skipMain ? 1 : 0; i < imageCount_; ++i)
            if ((images_[i].global || i >= transaction_) &&
                lookupIn(images_[i], name, address))
                return true;
        return false;
    }
    void dependencies(uint32_t first) {
        for (uint32_t i = first; i < imageCount_; ++i) {
            auto& image = images_[i];
            for (uint32_t n = 0; n < image.neededCount; ++n) {
                const char* name = string(image, image.needed[n]);
                char path[512];
                locate(image, name, path);
                uint32_t found = 1;
                for (; found < imageCount_; ++found)
                    if (os::Runtime::stringsEqual(images_[found].path, path))
                        break;
                if (found == imageCount_) {
                    if (imageCount_ == imageLimit)
                        fail("too many loaded libraries", name);
                    auto& dependency  = images_[imageCount_++];
                    dependency.global = first == 0;
                    copy(dependency.name, sizeof(dependency.name), name);
                    copy(dependency.path, sizeof(dependency.path), path);
                    load(dependency);
                }
                image.dependencies[n] = found;
            }
        }
    }
    void rollback(uint32_t first) {
        if (loadingBuffer_) {
            sourceAllocator.release(loadingBuffer_);
            loadingBuffer_ = nullptr;
        }
        for (uint32_t i = first; i < imageCount_; ++i) {
            auto& image = images_[i];
            for (uint64_t address = image.begin; address < image.mappedEnd;
                 address += page)
                if (pageFlags(image, address - image.bias))
                    os::unmapMemory(address, page);
            image = Image{};
        }
        imageCount_  = first;
        transaction_ = imageLimit;
    }
    bool acquire(char* error, size_t size) {
        const uint64_t thread = os::detail::TaskControl().getThreadId();
        while (true) {
            uint64_t expected = 0;
            if (__atomic_compare_exchange_n(&owner_, &expected, thread, false,
                                            __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
                return true;
            if (expected == thread) {
                copy(error, size, "recursive dynamic loading is unsupported");
                return false;
            }
            os::yield();
        }
    }
    void release() { __atomic_store_n(&owner_, 0, __ATOMIC_RELEASE); }
    bool scope(uint32_t root, const char* name, uint64_t& value) const {
        uint32_t queue[imageLimit] = {root}, count = 1;
        for (uint32_t cursor = 0; cursor < count; ++cursor) {
            const auto& image = images_[queue[cursor]];
            if (lookupIn(image, name, value)) return true;
            for (uint32_t n = 0; n < image.neededCount; ++n) {
                const auto dependency = image.dependencies[n];
                bool seen             = false;
                for (uint32_t k = 0; k < count; ++k)
                    if (queue[k] == dependency) seen = true;
                if (!seen) queue[count++] = dependency;
            }
        }
        return false;
    }
    static void* open(const char*, int, char*, size_t);
    static void* findSymbol(void*, const char*, const void*, char*, size_t);
    static int close(void*, char*, size_t);
    void* openModule(const char* path, int flags, char* error, size_t size) {
        if ((flags & ~(1 | 2 | 4 | 256 | 4096)) || !(flags & 3) ||
            (flags & 3) == 3) {
            copy(error, size, "unsupported dlopen flags");
            return nullptr;
        }
        if (!acquire(error, size)) return nullptr;
        const uint32_t first = imageCount_;
        if (__builtin_setjmp(recovery)) {
            recovering = false;
            rollback(first);
            copy(error, size, failureText);
            release();
            return nullptr;
        }
        recovering = true;
        if (!path) {
            ++images_[0].references;
            recovering = false;
            release();
            return reinterpret_cast<void*>(1);
        }
        char resolved[512];
        locate(images_[0], path, resolved);
        uint32_t index = 1;
        for (; index < imageCount_; ++index)
            if (os::Runtime::stringsEqual(images_[index].path, resolved)) break;
        if (index == imageCount_) {
            if (flags & 4) fail("library is not loaded", path);
            if (imageCount_ == imageLimit)
                fail("too many loaded libraries", path);
            auto& image  = images_[imageCount_++];
            image.global = false;
            copy(image.path, sizeof(image.path), resolved);
            copy(image.name, sizeof(image.name), path);
            transaction_ = first;
            load(image);
            dependencies(first);
            for (uint32_t i = first; i < imageCount_; ++i) {
                relocate(images_[i], images_[i].rela, images_[i].relaSize);
                relocate(images_[i], images_[i].plt, images_[i].pltSize);
                protect(images_[i]);
            }
        } else
            ++images_[index].references;
        if (flags & 256) {
            // NODELETE keeps global symbol providers resident for existing
            // relocations.
            bool selected[imageLimit]{};
            selected[index] = true;
            for (uint32_t pass = 0; pass < imageCount_; ++pass)
                for (uint32_t i = 0; i < imageCount_; ++i)
                    if (selected[i])
                        for (uint32_t n = 0; n < images_[i].neededCount; ++n)
                            selected[images_[i].dependencies[n]] = true;
            for (uint32_t i = 0; i < imageCount_; ++i)
                if (selected[i]) images_[i].global = true;
        }
        recovering   = false;
        transaction_ = imageLimit;
        init(index);
        publishDebugState(loaderAddress_);
        for (uint32_t i = first; i < imageCount_; ++i)
            for (uint32_t p = 0; p < images_[i].programCount; ++p) {
                const auto& segment = images_[i].programs[p];
                if (segment.type == 1 && segment.fileSize)
                    os::detail::TaskControl().recordImage(
                        images_[i].path,
                        images_[i].bias + down(segment.address),
                        images_[i].bias +
                            up(segment.address + segment.fileSize),
                        down(segment.offset));
            }
        release();
        return reinterpret_cast<void*>(uint64_t(index) + 1);
    }
    void* symbolModule(void* handle, const char* name, const void* caller,
                       char* error, size_t size) {
        if (!name || !*name) {
            copy(error, size, "invalid dynamic symbol name");
            return nullptr;
        }
        if (!acquire(error, size)) return nullptr;
        uint64_t value       = 0;
        bool found           = false;
        const uint64_t index = reinterpret_cast<uint64_t>(handle) - 1;
        if (!handle)
            found = lookup(name, value);
        else if (handle == reinterpret_cast<void*>(UINT64_MAX)) {
            bool after = false;
            for (uint32_t i = 0; i < imageCount_; ++i) {
                if (after && images_[i].global &&
                    lookupIn(images_[i], name, value)) {
                    found = true;
                    break;
                }
                if (contains(images_[i], reinterpret_cast<uint64_t>(caller), 1))
                    after = true;
            }
        } else if (index < imageCount_ && images_[index].references)
            found = scope(index, name, value);
        else
            copy(error, size, "invalid dynamic library handle");
        if (!found && !*error) copy(error, size, "dynamic symbol not found");
        release();
        return reinterpret_cast<void*>(value);
    }
    int closeModule(void* handle, char* error, size_t size) {
        if (!acquire(error, size)) return -1;
        const uint64_t index = reinterpret_cast<uint64_t>(handle) - 1;
        int result           = -1;
        if (index < imageCount_ && images_[index].references) {
            --images_[index].references;
            result = 0;
        } else
            copy(error, size, "invalid dynamic library handle");
        release();
        return result;
    }
    uint64_t symbol(const Image& image, uint32_t index) const {
        if (!index) return 0;
        if (index >= image.symbolCount)
            fail("invalid relocation symbol", image.path);
        const auto entry = image.symbols[index];
        if ((entry.info >> 4) == 0 || (entry.visibility & 3))
            return entry.section == 0xfff1 ? entry.value
                                           : image.bias + entry.value;
        uint64_t address = 0;
        if (lookup(string(image, entry.name), address)) return address;
        if ((entry.info >> 4) == 2) return 0;
        fail("undefined symbol", string(image, entry.name));
    }
    void relocate(Image& image, os::ElfRela* table, uint64_t bytes) {
        for (uint64_t i = 0; i < bytes / sizeof(os::ElfRela); ++i) {
            const auto relocation = table[i];
            const uint32_t type   = relocation.info,
                           index  = relocation.info >> 32;
            if (!type) continue;
            const uint64_t target = pointer(image, relocation.offset, 8);
            if (!contains(image, target, 8, 2))
                fail("text relocation rejected", image.path);
            uint64_t value = 0;
            if (type == 8)
                value = image.bias + relocation.addend;
            else if (type == 1 || type == 6 || type == 7)
                value =
                    symbol(image, index) + (type == 1 ? relocation.addend : 0);
            else
                fail("unsupported x86-64 relocation", image.path);
            os::Runtime::copyBytes(reinterpret_cast<void*>(target), &value,
                                   sizeof(value));
        }
    }
    static void protect(Image& image) {
        for (uint64_t address = image.begin; address < image.end;
             address += page) {
            const uint32_t flags = pageFlags(image, address - image.bias);
            if (!flags) continue;
            const uint64_t protection =
                ((flags & 2) ? 1 : 0) | ((flags & 1) ? 2 : 0);
            if (!os::protectMemory(address, page, protection))
                fail("final segment protection failed", image.path);
        }
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 0x6474e552 && segment.memorySize) {
                const uint64_t begin = down(image.bias + segment.address),
                               end   = down(image.bias + segment.address +
                                            segment.memorySize);
                if (end > begin && !os::protectMemory(begin, end - begin, 0))
                    fail("RELRO protection failed", image.path);
            }
        }
    }
    void invoke(Function function) const {
        if (!function || reinterpret_cast<uint64_t>(function) == UINT64_MAX)
            return;
        for (uint32_t i = 0; i < imageCount_; ++i)
            if (contains(images_[i], reinterpret_cast<uint64_t>(function), 1,
                         1)) {
                function();
                return;
            }
        fail("initializer outside executable segments");
    }
    void init(uint32_t index) {
        Image& image = images_[index];
        if (image.state) return;
        image.state = 1;
        for (uint32_t i = 0; i < image.neededCount; ++i)
            init(image.dependencies[i]);
        if (index) {
            invoke(image.init);
            for (uint64_t i = 0; i < image.initializerSize / 8; ++i)
                invoke(image.initializers[i]);
            initialized_[initializedCount_++] = index;
        }
        image.state = 2;
    }
    void finalize() {
        while (initializedCount_) {
            const auto& image = images_[initialized_[--initializedCount_]];
            for (uint64_t i = image.finalizerSize / 8; i; --i)
                invoke(image.finalizers[i - 1]);
            invoke(image.fini);
        }
    }
    static void finalizer();

   public:
    [[noreturn]] void start(uint64_t* stack) {
        stack_              = stack;
        const uint64_t argc = stack[0];
        if (!argc || argc > 64) fail("invalid initial stack");
        auto** argv  = reinterpret_cast<char**>(stack + 1);
        environment_ = argv + argc + 1;
        auto** end   = environment_;
        for (uint32_t i = 0; *end; ++i, ++end)
            if (i == 64) fail("invalid environment");
        uint64_t* auxiliary = reinterpret_cast<uint64_t*>(end + 1);
        uint64_t entry = 0, phdr = 0, phnum = 0, loader = 0;
        const char* executableName = argv[0];
        for (uint32_t i = 0; auxiliary[i * 2]; ++i) {
            if (i == 32) fail("invalid auxiliary vector");
            const uint64_t tag = auxiliary[i * 2], value = auxiliary[i * 2 + 1];
            if (tag == 3) phdr = value;
            if (tag == 5) phnum = value;
            if (tag == 9) entry = value;
            if (tag == 7) loader = value;
            if (tag == 31)
                executableName = reinterpret_cast<const char*>(value);
            if (tag == 0x60000002) images_[0].bias = value;
        }
        if (!entry || !phdr || !phnum || phnum > programLimit)
            fail("missing application auxiliary data");
        os::Runtime::runtimeInit(0, nullptr, environment_);
        os::wire::TaskInfo task{};
        if (os::detail::TaskQuery().query(os::detail::TaskControl().getId(), task))
            fail("cannot query process credentials");
        secure_ =
            task.uid != task.effectiveUid || task.gid != task.effectiveGid;
        Image& main = images_[0];
        if (!copy(main.path, sizeof(main.path), executableName))
            fail("program path too long");
        // The kernel supplies headers in immutable metadata pages,
        // independently of whether the executable maps its ELF header in a
        // PT_LOAD segment.
        main.programCount = phnum;
        os::Runtime::copyBytes(main.programs, reinterpret_cast<void*>(phdr),
                               phnum * sizeof(os::ElfProgramHeader));
        main.begin = UINT64_MAX;
        for (uint32_t i = 0; i < main.programCount; ++i) {
            const auto& segment = main.programs[i];
            if (segment.type != 1 || !segment.memorySize) continue;
            if (main.bias + down(segment.address) < main.begin)
                main.begin = main.bias + down(segment.address);
            if (main.bias + up(segment.address + segment.memorySize) > main.end)
                main.end = main.bias + up(segment.address + segment.memorySize);
        }
        if (!contains(main, entry, 1, 1))
            fail("application entry is not executable");
        os::detail::TaskControl().recordAuxiliary(phdr, phnum, entry, loader);
        segments(main);
        dependencies(0);
        for (uint32_t i = 0; i < imageCount_; ++i) {
            relocate(images_[i], images_[i].rela, images_[i].relaSize);
            relocate(images_[i], images_[i].plt, images_[i].pltSize);
        }
        loaderAddress_ = loader;
        publishDebugState(loader);
        for (uint32_t i = 0; i < imageCount_; ++i) {
            protect(images_[i]);
            for (uint32_t p = 0; p < images_[i].programCount; ++p) {
                const auto& segment = images_[i].programs[p];
                if (segment.type == 1 && segment.fileSize)
                    os::detail::TaskControl().recordImage(
                        images_[i].path,
                        images_[i].bias + down(segment.address),
                        images_[i].bias +
                            up(segment.address + segment.fileSize),
                        down(segment.offset));
            }
        }
        // Replace one reserved custom auxiliary tag with the shared-library
        // finalizer. Main startup still owns the executable's init/fini arrays.
        for (uint32_t i = 0; auxiliary[i * 2]; ++i)
            if (auxiliary[i * 2] == 0x60000002) {
                auxiliary[i * 2]     = 0x60000003;
                auxiliary[i * 2 + 1] = reinterpret_cast<uint64_t>(finalizer);
                break;
            }
        // Shared-library constructors may allocate, inspect environ, or use
        // inherited descriptors. Initialize those facilities before invoking
        // any constructors; main startup must preserve their resulting state.
        uint64_t prepare = 0;
        if (lookup("_ZN2os7Runtime7prepareEPPc", prepare))
            reinterpret_cast<void (*)(char**)>(prepare)(environment_);
        uint64_t setEnvironment = 0;
        if (lookup("__libc_set_environ", setEnvironment))
            reinterpret_cast<void (*)(char**)>(setEnvironment)(environment_);
        uint64_t importState = 0;
        if (lookup("__libc_import_exec_state", importState))
            for (uint32_t i = 0; auxiliary[i * 2]; ++i)
                if (auxiliary[i * 2] == 0x60000001 && auxiliary[i * 2 + 1]) {
                    reinterpret_cast<void (*)(const void*)>(importState)(
                        reinterpret_cast<const void*>(auxiliary[i * 2 + 1]));
                    auxiliary[i * 2 + 1] = 0;
                    break;
                }
        uint64_t allocate = 0, free = 0;
        if (lookup("__osinfo_runtime_allocate", allocate) &&
            lookup("__osinfo_runtime_free", free)) {
            sourceAllocator.bind(allocate, free);
        }
        uint64_t bindLoader              = 0;
        static const OsDynamicLoader api = {1, sizeof(OsDynamicLoader), open,
                                            findSymbol, close};
        if (lookup("__osinfo_bind_loader", bindLoader))
            reinterpret_cast<void (*)(const OsDynamicLoader*)>(bindLoader)(
                &api);
        init(0);
        asm volatile("mov %0, %%rsp; jmp *%1" ::"r"(stack_), "r"(entry)
                     : "memory");
        __builtin_unreachable();
    }
};
DynamicLinker linker;
void* DynamicLinker::open(const char* p, int f, char* e, size_t s) {
    return linker.openModule(p, f, e, s);
}
void* DynamicLinker::findSymbol(void* h, const char* n, const void* c, char* e,
                                size_t s) {
    return linker.symbolModule(h, n, c, e, s);
}
int DynamicLinker::close(void* h, char* e, size_t s) {
    return linker.closeModule(h, e, s);
}
void DynamicLinker::finalizer() { linker.finalize(); }
}  // namespace

extern "C" [[noreturn]] void osinfoLoaderStart(uint64_t* stack) {
    linker.start(stack);
}
