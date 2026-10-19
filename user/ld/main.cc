#include "os/abi.h"
#include "os/elf.h"
#include "os/file.h"
#include "os/task.h"

namespace {
class DynamicLinker {
    static constexpr uint32_t imageLimit = 32, programLimit = 128, dependencyLimit = 32;
    static constexpr uint64_t page = 4096, addressLimit = 0x800000000000ULL;
    using Function = void (*)();
    struct Image {
        char path[512]{}, name[256]{};
        os::ElfProgramHeader programs[programLimit]{};
        uint32_t programCount = 0;
        uint64_t bias = 0, begin = 0, end = 0;
        os::ElfDynamic* dynamic = nullptr;
        uint64_t dynamicCount = 0;
        const char* strings = nullptr;
        uint64_t stringSize = 0;
        os::ElfSymbol* symbols = nullptr;
        uint32_t* hash = nullptr;
        uint32_t symbolCount = 0;
        os::ElfRela *rela = nullptr, *plt = nullptr;
        uint64_t relaSize = 0, pltSize = 0;
        uint64_t needed[dependencyLimit]{};
        uint32_t neededCount = 0, dependencies[dependencyLimit]{}, state = 0;
        uint64_t runpath = 0;
        bool hasRunpath = false;
        Function init = nullptr, fini = nullptr;
        Function *initializers = nullptr, *finalizers = nullptr;
        uint64_t initializerSize = 0, finalizerSize = 0;
    } images_[imageLimit];
    uint32_t imageCount_ = 1, initialized_[imageLimit]{}, initializedCount_ = 0;
    uint64_t* stack_ = nullptr;
    char** environment_ = nullptr;
    bool secure_ = false;

    static uint64_t down(uint64_t value) { return value & ~(page - 1); }
    static uint64_t up(uint64_t value) { return (value + page - 1) & ~(page - 1); }
    static bool range(uint64_t offset, uint64_t size, uint64_t limit) {
        return offset <= limit && size <= limit - offset;
    }
    [[noreturn]] static void fail(const char* reason, const char* detail = nullptr) {
        os::System::log("[LD] "); os::System::log(reason);
        if (detail) { os::System::log(": "); os::System::log(detail); }
        os::System::log("\n");
        const uint64_t handle = os::getStandardHandle(2);
        if (handle) {
            os::Pipe::writePipe(handle, "ld-osinfo: ", 11);
            os::Pipe::writePipe(handle, reason, os::Runtime::stringLength(reason));
            if (detail) { os::Pipe::writePipe(handle, ": ", 2); os::Pipe::writePipe(handle, detail, os::Runtime::stringLength(detail)); }
            os::Pipe::writePipe(handle, "\n", 1);
        }
        os::exitTask(127);
    }
    static bool contains(const Image& image, uint64_t address, uint64_t size, uint32_t flags = 0) {
        if (!size || address < image.bias) return false;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type != 1 || (segment.flags & flags) != flags) continue;
            const uint64_t start = image.bias + segment.address;
            if (address >= start && range(address - start, size, segment.memorySize)) return true;
        }
        return false;
    }
    static uint64_t pointer(const Image& image, uint64_t value, uint64_t bytes) {
        if (value > addressLimit - image.bias || !contains(image, image.bias + value, bytes))
            fail("dynamic pointer outside load segments", image.path);
        return image.bias + value;
    }
    static const char* string(const Image& image, uint64_t offset) {
        if (offset >= image.stringSize) fail("invalid dynamic string", image.path);
        const char* value = image.strings + offset;
        for (uint64_t i = offset; i < image.stringSize; ++i) if (!image.strings[i]) return value;
        fail("unterminated dynamic string", image.path);
    }
    static bool copy(char* target, size_t capacity, const char* source) {
        if (os::Runtime::stringLength(source) >= capacity) return false;
        os::Runtime::copyString(target, source, capacity); return true;
    }
    static bool prefix(const char* text, const char* expected, size_t count) {
        for (size_t i = 0; i < count; ++i) if (text[i] != expected[i]) return false;
        return true;
    }
    static void dynamic(Image& image) {
        uint64_t hash = 0, strings = 0, symbols = 0, rela = 0, plt = 0;
        uint64_t init = 0, fini = 0, initArray = 0, finiArray = 0;
        bool terminated = false;
        for (uint64_t i = 0; i < image.dynamicCount; ++i) {
            const auto entry = image.dynamic[i];
            if (!entry.tag) { terminated = true; break; }
            switch (entry.tag) {
                case 1:
                    if (image.neededCount == dependencyLimit) fail("too many dependencies", image.path);
                    image.needed[image.neededCount++] = entry.value; break;
                case 2: image.pltSize = entry.value; break;
                case 4: hash = entry.value; break;
                case 5: strings = entry.value; break;
                case 6: symbols = entry.value; break;
                case 7: rela = entry.value; break;
                case 8: image.relaSize = entry.value; break;
                case 9: if (entry.value != sizeof(os::ElfRela)) fail("unsupported RELA format", image.path); break;
                case 10: image.stringSize = entry.value; break;
                case 11: if (entry.value != sizeof(os::ElfSymbol)) fail("unsupported symbol format", image.path); break;
                case 12: init = entry.value; break;
                case 13: fini = entry.value; break;
                case 15: if (!image.hasRunpath) image.runpath = entry.value; break;
                case 17: case 18: case 19: case 22: case 35: case 36: case 37:
                    fail("unsupported REL/text/RELR relocation", image.path);
                case 20: if (entry.value != 7) fail("PLT must use RELA", image.path); break;
                case 23: plt = entry.value; break;
                case 25: initArray = entry.value; break;
                case 26: finiArray = entry.value; break;
                case 27: image.initializerSize = entry.value; break;
                case 28: image.finalizerSize = entry.value; break;
                case 29: image.runpath = entry.value; image.hasRunpath = true; break;
                case 0x6ffffffe: if (entry.value) fail("symbol versions are unsupported", image.path); break;
                default: break;
            }
        }
        if (!terminated || !hash || !strings || !symbols || !image.stringSize)
            fail("incomplete dynamic tables (SYSV hash required)", image.path);
        image.strings = reinterpret_cast<const char*>(pointer(image, strings, image.stringSize));
        image.hash = reinterpret_cast<uint32_t*>(pointer(image, hash, 8));
        const uint64_t buckets = image.hash[0], chains = image.hash[1];
        if (!buckets || !chains || chains > 65536 || buckets > 65536)
            fail("invalid ELF hash table", image.path);
        pointer(image, hash, (2 + buckets + chains) * 4);
        image.symbolCount = chains;
        image.symbols = reinterpret_cast<os::ElfSymbol*>(pointer(image, symbols, chains * sizeof(os::ElfSymbol)));
        if (image.relaSize % sizeof(os::ElfRela) || image.pltSize % sizeof(os::ElfRela)) fail("invalid relocation size", image.path);
        if (image.relaSize) image.rela = reinterpret_cast<os::ElfRela*>(pointer(image, rela, image.relaSize));
        if (image.pltSize) image.plt = reinterpret_cast<os::ElfRela*>(pointer(image, plt, image.pltSize));
        if (init) image.init = reinterpret_cast<Function>(pointer(image, init, 1));
        if (fini) image.fini = reinterpret_cast<Function>(pointer(image, fini, 1));
        if (image.initializerSize % 8 || image.finalizerSize % 8) fail("invalid constructor array", image.path);
        if (image.initializerSize) image.initializers = reinterpret_cast<Function*>(pointer(image, initArray, image.initializerSize));
        if (image.finalizerSize) image.finalizers = reinterpret_cast<Function*>(pointer(image, finiArray, image.finalizerSize));
        if (image.hasRunpath || image.runpath) string(image, image.runpath);
        for (uint32_t i = 0; i < image.neededCount; ++i) string(image, image.needed[i]);
    }
    static void segments(Image& image) {
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 7) fail("TLS segments are unsupported", image.path);
            if (segment.type == 2) {
                if (image.dynamic || !segment.memorySize || segment.memorySize % sizeof(os::ElfDynamic))
                    fail("invalid PT_DYNAMIC", image.path);
                image.dynamic = reinterpret_cast<os::ElfDynamic*>(pointer(image, segment.address, segment.memorySize));
                image.dynamicCount = segment.memorySize / sizeof(os::ElfDynamic);
            }
        }
        if (!image.dynamic) fail("missing PT_DYNAMIC", image.path);
        dynamic(image);
    }
    static void* readFile(const char* path, uint64_t& bytes) {
        os::File file;
        if (file.open(path, O_RDONLY) || file.refresh()) return nullptr;
        bytes = file.getSize();
        if (file.getType() != os::FileType::File || !bytes || bytes > 32 * 1024 * 1024) return nullptr;
        void* buffer = os::Memory::allocateMemory(bytes);
        if (!buffer) fail("out of memory", path);
        uint64_t offset = 0;
        while (offset < bytes) {
            const auto count = file.read(static_cast<uint8_t*>(buffer) + offset, bytes - offset);
            if (count <= 0) { os::Memory::freeMemory(buffer); return nullptr; }
            offset += count;
        }
        return buffer;
    }
    static bool candidate(char* path, size_t capacity, const char* directory, size_t length, const char* name) {
        const size_t suffix = os::Runtime::stringLength(name);
        if (!length || directory[0] != '/' || length + suffix + 2 > capacity) return false;
        os::Runtime::copyBytes(path, directory, length);
        path[length] = '/'; os::Runtime::copyBytes(path + length + 1, name, suffix + 1);
        os::File file;
        return file.refresh(path) == 0 && file.getType() == os::FileType::File;
    }
    static bool search(char* path, const char* entries, const char* name, const Image* origin = nullptr) {
        if (!entries) return false;
        while (*entries) {
            const char* end = entries;
            while (*end && *end != ':') ++end;
            char directory[512]; size_t count = 0;
            for (const char* cursor = entries; cursor < end; ) {
                size_t token = 0;
                if (end - cursor >= 7 && prefix(cursor, "$ORIGIN", 7)) token = 7;
                if (end - cursor >= 9 && prefix(cursor, "${ORIGIN}", 9)) token = 9;
                if (token && origin) {
                    size_t length = os::Runtime::stringLength(origin->path);
                    while (length > 1 && origin->path[length - 1] != '/') --length;
                    if (length > 1) --length;
                    if (length >= sizeof(directory) - count) fail("RUNPATH too long", origin->path);
                    os::Runtime::copyBytes(directory + count, origin->path, length); count += length; cursor += token;
                } else {
                    if (count + 1 == sizeof(directory)) fail("library search path too long");
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
        if (!*name || os::Runtime::stringLength(name) >= 256) fail("invalid dependency name", requester.path);
        for (const char* p = name; *p; ++p) if (*p == '/') {
            if (*name != '/' || !copy(path, 512, name)) fail("dependency path must be absolute", name);
            return;
        }
        if ((!secure_ && search(path, os::Runtime::environmentValue("LD_LIBRARY_PATH"), name)) ||
            (!secure_ && (requester.hasRunpath || requester.runpath) &&
                search(path, string(requester, requester.runpath), name, &requester)) ||
            search(path, "/lib:/usr/lib:/usr/local/lib", name)) return;
        fail("library not found", name);
    }
    void load(Image& image) {
        uint64_t bytes = 0;
        void* source = readFile(image.path, bytes);
        if (!source) fail("cannot read library", image.path);
        char interpreter[256];
        const int parsed = os::Executable::interpreter(source, bytes, interpreter, sizeof(interpreter));
        const auto& header = *static_cast<os::ElfHeader*>(source);
        if (parsed != 0 || header.type != 3) fail("dependency must be ELF64 ET_DYN", image.path);
        image.programCount = header.programCount;
        os::Runtime::copyBytes(image.programs, static_cast<uint8_t*>(source) + header.programOffset,
                              image.programCount * sizeof(os::ElfProgramHeader));
        uint64_t minimum = UINT64_MAX, maximum = 0, total = 0;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type != 1 || !segment.memorySize) continue;
            if (segment.fileSize > segment.memorySize || !range(segment.offset, segment.fileSize, bytes) ||
                !range(segment.address, segment.memorySize, addressLimit) || (segment.flags & ~7U) ||
                (segment.flags & 3) == 3 ||
                (segment.alignment > 1 && ((segment.alignment & (segment.alignment - 1)) ||
                  (segment.address % segment.alignment != segment.offset % segment.alignment))))
                fail("invalid library segment", image.path);
            if (down(segment.address) < minimum) minimum = down(segment.address);
            if (up(segment.address + segment.memorySize) > maximum) maximum = up(segment.address + segment.memorySize);
            total += up(segment.memorySize);
            for (uint32_t j = 0; j < i; ++j) {
                const auto& other = image.programs[j];
                if (other.type == 1 && other.memorySize && segment.address < other.address + other.memorySize &&
                    other.address < segment.address + segment.memorySize) fail("overlapping library segments", image.path);
            }
        }
        if (minimum == UINT64_MAX || maximum <= minimum || total > 256 * 1024 * 1024 || maximum - minimum > 256 * 1024 * 1024)
            fail("library address range too large", image.path);
        image.begin = 0x1000000000ULL + (&image - images_) * 0x40000000ULL;
        image.bias = image.begin - minimum; image.end = image.bias + maximum;
        // Map each page once, writable and NX during relocation. No RWX stage.
        for (uint64_t address = minimum; address < maximum; address += page) {
            uint32_t flags = pageFlags(image, address);
            if (!flags) continue;
            if ((flags & 3) == 3) fail("library page violates W^X", image.path);
            if (os::mapMemory(image.bias + address, page, 1) != image.bias + address)
                fail("library mapping failed", image.path);
        }
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 1 && segment.fileSize)
                os::Runtime::copyBytes(reinterpret_cast<void*>(image.bias + segment.address),
                    static_cast<uint8_t*>(source) + segment.offset, segment.fileSize);
        }
        os::Memory::freeMemory(source);
        segments(image);
    }
    static uint32_t pageFlags(const Image& image, uint64_t address) {
        uint32_t flags = 0;
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 1 && segment.memorySize && address < segment.address + segment.memorySize &&
                segment.address < address + page) flags |= segment.flags | 4;
        }
        return flags;
    }
    static uint32_t hash(const char* name) {
        uint32_t value = 0;
        while (*name) {
            value = (value << 4) + static_cast<uint8_t>(*name++);
            const uint32_t high = value & 0xf0000000U;
            value ^= high >> 24; value &= ~high;
        }
        return value;
    }
    bool lookup(const char* name, uint64_t& address, bool skipMain = false) const {
        const uint32_t value = hash(name);
        for (uint32_t i = skipMain ? 1 : 0; i < imageCount_; ++i) {
            const auto& image = images_[i];
            uint32_t index = image.hash[2 + value % image.hash[0]];
            for (uint32_t seen = 0; index; ++seen) {
                if (seen >= image.symbolCount || index >= image.symbolCount) fail("corrupt symbol hash chain", image.path);
                const auto symbol = image.symbols[index];
                const uint8_t binding = symbol.info >> 4;
                if (symbol.section && (binding == 1 || binding == 2 || binding == 10) &&
                    ((symbol.visibility & 3) == 0 || (symbol.visibility & 3) == 3) &&
                    os::Runtime::stringsEqual(string(image, symbol.name), name)) {
                    if ((symbol.info & 15) == 6 || (symbol.info & 15) == 10) fail("TLS/IFUNC symbol unsupported", name);
                    address = symbol.section == 0xfff1 ? symbol.value : image.bias + symbol.value; return true;
                }
                index = image.hash[2 + image.hash[0] + index];
            }
        }
        return false;
    }
    uint64_t symbol(const Image& image, uint32_t index) const {
        if (!index) return 0;
        if (index >= image.symbolCount) fail("invalid relocation symbol", image.path);
        const auto entry = image.symbols[index];
        if ((entry.info >> 4) == 0 || (entry.visibility & 3))
            return entry.section == 0xfff1 ? entry.value : image.bias + entry.value;
        uint64_t address = 0;
        if (lookup(string(image, entry.name), address)) return address;
        if ((entry.info >> 4) == 2) return 0;
        fail("undefined symbol", string(image, entry.name));
    }
    void relocate(Image& image, os::ElfRela* table, uint64_t bytes) {
        for (uint64_t i = 0; i < bytes / sizeof(os::ElfRela); ++i) {
            const auto relocation = table[i];
            const uint32_t type = relocation.info, index = relocation.info >> 32;
            if (!type) continue;
            const uint64_t target = pointer(image, relocation.offset, 8);
            if (!contains(image, target, 8, 2)) fail("text relocation rejected", image.path);
            uint64_t value = 0;
            if (type == 8) value = image.bias + relocation.addend;
            else if (type == 1 || type == 6 || type == 7)
                value = symbol(image, index) + (type == 1 ? relocation.addend : 0);
            else fail("unsupported x86-64 relocation", image.path);
            os::Runtime::copyBytes(reinterpret_cast<void*>(target), &value, sizeof(value));
        }
    }
    static void protect(Image& image) {
        for (uint64_t address = image.begin; address < image.end; address += page) {
            const uint32_t flags = pageFlags(image, address - image.bias);
            if (!flags) continue;
            const uint64_t protection = ((flags & 2) ? 1 : 0) | ((flags & 1) ? 2 : 0);
            if (!os::protectMemory(address, page, protection)) fail("final segment protection failed", image.path);
        }
        for (uint32_t i = 0; i < image.programCount; ++i) {
            const auto& segment = image.programs[i];
            if (segment.type == 0x6474e552 && segment.memorySize) {
                const uint64_t begin = down(image.bias + segment.address), end = down(image.bias + segment.address + segment.memorySize);
                if (end > begin && !os::protectMemory(begin, end - begin, 0)) fail("RELRO protection failed", image.path);
            }
        }
    }
    void invoke(Function function) const {
        if (!function || reinterpret_cast<uint64_t>(function) == UINT64_MAX) return;
        for (uint32_t i = 0; i < imageCount_; ++i)
            if (contains(images_[i], reinterpret_cast<uint64_t>(function), 1, 1)) { function(); return; }
        fail("initializer outside executable segments");
    }
    void initialize(uint32_t index) {
        Image& image = images_[index];
        if (image.state) return;
        image.state = 1;
        for (uint32_t i = 0; i < image.neededCount; ++i) initialize(image.dependencies[i]);
        if (index) {
            invoke(image.init);
            for (uint64_t i = 0; i < image.initializerSize / 8; ++i) invoke(image.initializers[i]);
            initialized_[initializedCount_++] = index;
        }
        image.state = 2;
    }
    void finalize() {
        while (initializedCount_) {
            const auto& image = images_[initialized_[--initializedCount_]];
            for (uint64_t i = image.finalizerSize / 8; i; --i) invoke(image.finalizers[i - 1]);
            invoke(image.fini);
        }
    }
    static void finalizer();
   public:
    [[noreturn]] void start(uint64_t* stack) {
        stack_ = stack;
        const uint64_t argc = stack[0];
        if (!argc || argc > 64) fail("invalid initial stack");
        auto** argv = reinterpret_cast<char**>(stack + 1);
        environment_ = argv + argc + 1;
        auto** end = environment_;
        for (uint32_t i = 0; *end; ++i, ++end) if (i == 64) fail("invalid environment");
        uint64_t* auxiliary = reinterpret_cast<uint64_t*>(end + 1);
        uint64_t entry = 0, phdr = 0, phnum = 0;
        const char* executableName = argv[0];
        for (uint32_t i = 0; auxiliary[i * 2]; ++i) {
            if (i == 32) fail("invalid auxiliary vector");
            const uint64_t tag = auxiliary[i * 2], value = auxiliary[i * 2 + 1];
            if (tag == 3) phdr = value;
            if (tag == 5) phnum = value;
            if (tag == 9) entry = value;
            if (tag == 31) executableName = reinterpret_cast<const char*>(value);
            if (tag == 0x60000002) images_[0].bias = value;
        }
        if (!entry || !phdr || !phnum || phnum > programLimit) fail("missing application auxiliary data");
        os::Runtime::runtimeInit(0, nullptr, environment_);
        os::TaskInfo task{};
        if (os::Process::queryTask(os::Process::getTaskId(), task)) fail("cannot query process credentials");
        secure_ = task.uid != task.effectiveUid || task.gid != task.effectiveGid;
        Image& main = images_[0];
        if (!copy(main.path, sizeof(main.path), executableName)) fail("program path too long");
        // The kernel supplies headers in immutable metadata pages, independently
        // of whether the executable maps its ELF header in a PT_LOAD segment.
        main.programCount = phnum;
        os::Runtime::copyBytes(main.programs, reinterpret_cast<void*>(phdr), phnum * sizeof(os::ElfProgramHeader));
        main.begin = UINT64_MAX;
        for (uint32_t i = 0; i < main.programCount; ++i) {
            const auto& segment = main.programs[i];
            if (segment.type != 1 || !segment.memorySize) continue;
            if (main.bias + down(segment.address) < main.begin) main.begin = main.bias + down(segment.address);
            if (main.bias + up(segment.address + segment.memorySize) > main.end) main.end = main.bias + up(segment.address + segment.memorySize);
        }
        if (!contains(main, entry, 1, 1)) fail("application entry is not executable");
        segments(main);
        // Breadth first dependency loading gives the executable and its direct
        // dependencies precedence during global symbol lookup, with cycles deduplicated.
        for (uint32_t i = 0; i < imageCount_; ++i) {
            auto& image = images_[i];
            for (uint32_t n = 0; n < image.neededCount; ++n) {
                const char* name = string(image, image.needed[n]);
                uint32_t found = 1;
                for (; found < imageCount_; ++found) if (os::Runtime::stringsEqual(images_[found].name, name)) break;
                if (found == imageCount_) {
                    if (imageCount_ == imageLimit) fail("too many loaded libraries", name);
                    auto& dependency = images_[imageCount_++];
                    copy(dependency.name, sizeof(dependency.name), name);
                    locate(image, name, dependency.path);
                    load(dependency);
                }
                image.dependencies[n] = found;
            }
        }
        for (uint32_t i = 0; i < imageCount_; ++i) {
            relocate(images_[i], images_[i].rela, images_[i].relaSize);
            relocate(images_[i], images_[i].plt, images_[i].pltSize);
        }
        for (uint32_t i = 0; i < imageCount_; ++i) protect(images_[i]);
        // Replace one reserved custom auxiliary tag with the shared-library
        // finalizer. Main startup still owns the executable's init/fini arrays.
        for (uint32_t i = 0; auxiliary[i * 2]; ++i)
            if (auxiliary[i * 2] == 0x60000002) {
                auxiliary[i * 2] = 0x60000003;
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
        initialize(0);
        asm volatile("mov %0, %%rsp; jmp *%1" :: "r"(stack_), "r"(entry) : "memory");
        __builtin_unreachable();
    }
};
DynamicLinker linker;
void DynamicLinker::finalizer() { linker.finalize(); }
}  // namespace

extern "C" [[noreturn]] void osinfoLoaderStart(uint64_t* stack) { linker.start(stack); }
