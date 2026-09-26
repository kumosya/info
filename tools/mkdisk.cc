#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr uint32_t sectorSize = 512;
constexpr uint64_t diskSectors = 32768;
constexpr uint64_t primaryHeaderSector = 1;
constexpr uint64_t primaryEntriesSector = 2;
constexpr uint64_t backupEntriesSector = diskSectors - 33;
constexpr uint64_t backupHeaderSector = diskSectors - 1;
constexpr uint64_t firstUsableSector = 34;
constexpr uint64_t lastUsableSector = diskSectors - 34;
constexpr uint64_t partitionStartSector = 2048;
constexpr uint64_t partitionEndSector = lastUsableSector;
constexpr uint32_t gptEntryCount = 128;
constexpr uint32_t gptEntrySize = 128;

#pragma pack(push, 1)
struct GptHeader {
    char signature[8];
    uint32_t revision;
    uint32_t headerSize;
    uint32_t headerCrc;
    uint32_t reserved;
    uint64_t currentSector;
    uint64_t backupSector;
    uint64_t firstUsableSector;
    uint64_t lastUsableSector;
    uint8_t diskGuid[16];
    uint64_t entriesSector;
    uint32_t entryCount;
    uint32_t entrySize;
    uint32_t entriesCrc;
};

struct GptEntry {
    uint8_t typeGuid[16];
    uint8_t uniqueGuid[16];
    uint64_t firstSector;
    uint64_t lastSector;
    uint64_t attributes;
    uint16_t name[36];
};
#pragma pack(pop)

static_assert(sizeof(GptHeader) == 92);
static_assert(sizeof(GptEntry) == gptEntrySize);

uint32_t crc32(const void* data, size_t size) {
    uint32_t crc = 0xffffffffU;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^
                  (0xedb88320U & (0U - static_cast<uint32_t>(crc & 1U)));
        }
    }
    return crc ^ 0xffffffffU;
}

void fillGuid(uint8_t (&guid)[16], uint8_t seed) {
    for (uint32_t index = 0; index < sizeof(guid); ++index) {
        guid[index] = static_cast<uint8_t>(seed + index * 17);
    }
}

GptHeader makeHeader(uint64_t current, uint64_t backup, uint64_t entries,
                     uint32_t entriesCrc) {
    GptHeader header{};
    std::memcpy(header.signature, "EFI PART", 8);
    header.revision = 0x00010000;
    header.headerSize = sizeof(header);
    header.currentSector = current;
    header.backupSector = backup;
    header.firstUsableSector = firstUsableSector;
    header.lastUsableSector = lastUsableSector;
    fillGuid(header.diskGuid, 0x31);
    header.entriesSector = entries;
    header.entryCount = gptEntryCount;
    header.entrySize = gptEntrySize;
    header.entriesCrc = entriesCrc;
    header.headerCrc = crc32(&header, header.headerSize);
    return header;
}

void writeAt(std::fstream& image, uint64_t offset, const void* data,
             size_t size) {
    image.seekp(static_cast<std::streamoff>(offset));
    image.write(static_cast<const char*>(data),
                static_cast<std::streamsize>(size));
    if (!image) throw std::runtime_error("unable to write disk image");
}

std::string shellQuote(const std::filesystem::path& path) {
    std::string result = "'";
    for (char value : path.string()) {
        result += value == '\'' ? "'\\''" : std::string(1, value);
    }
    return result + "'";
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("unable to write " + path.string());
}

void copyFile(const std::filesystem::path& source,
              const std::filesystem::path& destination) {
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing);
}

void buildRootTree(const std::filesystem::path& root, int argc, char** argv,
                   int serviceMarker, int libraryMarker) {
    std::filesystem::remove_all(root);
    for (const char* directory : {"bin", "dev", "etc", "home", "lib", "tmp",
                                  "system", "usr", "usr/bin", "system/bin",
                                  "system/services"}) {
        std::filesystem::create_directories(root / directory);
    }
    for (int index = 2; index < serviceMarker; ++index) {
        const std::filesystem::path source = argv[index];
        copyFile(source, root / "bin" / source.filename());
    }
    for (int index = serviceMarker + 1; index < libraryMarker; ++index) {
        const std::filesystem::path source = argv[index];
        copyFile(source, root / "system/services" / source.filename());
    }
    for (int index = libraryMarker + 1; index < argc; ++index) {
        const std::filesystem::path source = argv[index];
        copyFile(source, root / "lib" / source.filename());
    }
    writeText(root / "home/welcome.txt", "osinfo userland\n");
    writeText(root / "etc/console.conf",
              "defaultTerminal=tty1\nserialConsole=off\nserialLog=on\n"
              "serialDevice=com1\n");
    writeText(root / "etc/fstab",
              "# device mount-point type options\n"
              "/dev/vda1 / ext3 defaults\n");
}

void formatExt3(const std::filesystem::path& partition,
                const std::filesystem::path& root) {
    const uint64_t partitionBytes =
        (partitionEndSector - partitionStartSector + 1) * sectorSize;
    std::ofstream output(partition, std::ios::binary | std::ios::trunc);
    output.seekp(static_cast<std::streamoff>(partitionBytes - 1));
    output.put('\0');
    output.close();
    const std::string command =
        "mke2fs -q -F -t ext3 -b 4096 -I 128 -m 0 "
        "-O has_journal,filetype,sparse_super,large_file,^dir_index -d " +
        shellQuote(root) + " " + shellQuote(partition);
    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("mke2fs failed");
    }
}

void installPartition(std::fstream& image,
                      const std::filesystem::path& partition) {
    std::ifstream input(partition, std::ios::binary);
    if (!input) throw std::runtime_error("unable to open ext3 partition");
    image.seekp(static_cast<std::streamoff>(partitionStartSector * sectorSize));
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const std::streamsize count = input.gcount();
        if (count != 0) image.write(buffer.data(), count);
    }
    if (!image) throw std::runtime_error("unable to install ext3 partition");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: mkdisk IMAGE APPLICATION... "
                     "[--service SERVICE...] [--lib LIBRARY...]\n";
        return 1;
    }
    try {
        const std::filesystem::path imagePath = argv[1];
        int serviceMarker = argc;
        int libraryMarker = argc;
        for (int index = 2; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--service" && serviceMarker == argc) {
                serviceMarker = index;
            } else if (argument == "--lib" && libraryMarker == argc) {
                libraryMarker = index;
            }
        }
        if (serviceMarker >= libraryMarker) {
            throw std::runtime_error("--service must precede --lib");
        }
        const std::filesystem::path temporaryRoot = imagePath.string() + ".root";
        const std::filesystem::path partition = imagePath.string() + ".ext3";
        buildRootTree(temporaryRoot, argc, argv, serviceMarker, libraryMarker);
        formatExt3(partition, temporaryRoot);

        std::fstream image(imagePath,
                           std::ios::binary | std::ios::in | std::ios::out |
                               std::ios::trunc);
        image.seekp(static_cast<std::streamoff>(diskSectors * sectorSize - 1));
        image.put('\0');
        std::array<GptEntry, gptEntryCount> entries{};
        constexpr uint8_t linuxFilesystemGuid[16] = {
            0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
            0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
        std::memcpy(entries[0].typeGuid, linuxFilesystemGuid, 16);
        fillGuid(entries[0].uniqueGuid, 0x61);
        entries[0].firstSector = partitionStartSector;
        entries[0].lastSector = partitionEndSector;
        constexpr char16_t partitionName[] = u"osinfo-root";
        std::memcpy(entries[0].name, partitionName, sizeof(partitionName));
        const uint32_t entriesCrc = crc32(entries.data(), sizeof(entries));
        const GptHeader primary = makeHeader(primaryHeaderSector,
                                             backupHeaderSector,
                                             primaryEntriesSector, entriesCrc);
        const GptHeader backup = makeHeader(backupHeaderSector,
                                            primaryHeaderSector,
                                            backupEntriesSector, entriesCrc);
        writeAt(image, primaryHeaderSector * sectorSize, &primary,
                sizeof(primary));
        writeAt(image, primaryEntriesSector * sectorSize, entries.data(),
                sizeof(entries));
        writeAt(image, backupEntriesSector * sectorSize, entries.data(),
                sizeof(entries));
        writeAt(image, backupHeaderSector * sectorSize, &backup, sizeof(backup));
        installPartition(image, partition);
        image.flush();
        std::filesystem::remove_all(temporaryRoot);
        std::filesystem::remove(partition);
        std::cout << "created " << imagePath << " with an ext3 root filesystem\n";
    } catch (const std::exception& error) {
        std::cerr << "mkdisk: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
