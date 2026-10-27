#include <cstring>

#include "host.h"
namespace host {
void image(const Context& context) {
    if (context.output.empty())
        throw std::runtime_error("image requires --output stage");
    auto stage = fs::canonical(context.output);
    if (context.argument == "embed-grub") {
        // GRUB's BIOS boot sector points at the embedded diskboot sector.
        // Its final 12-byte blocklist describes the remaining contiguous core.
        // This only writes a newly generated regular image, never a device.
        auto boot = read(stage / "boot.img"), core = read(stage / "core.img");
        if (!fs::is_regular_file(context.image) ||
            fs::is_symlink(context.image) || boot.size() != 512 ||
            core.size() < 1024 || core.size() > 512 * 1024)
            throw std::runtime_error("invalid GRUB image inputs");
        std::fstream disk(context.image,
                          std::ios::binary | std::ios::in | std::ios::out);
        unsigned char mbr[512]{};
        disk.read(reinterpret_cast<char*>(mbr), sizeof(mbr));
        uint32_t first = 0;
        memcpy(&first, mbr + 454, 4);
        const uint32_t sectors = (core.size() + 511) / 512;
        if (!disk || mbr[510] != 0x55 || mbr[511] != 0xaa ||
            first < sectors + 1)
            throw std::runtime_error("no room for embedded GRUB core");
        auto put = [](std::string& data, size_t offset, uint64_t value,
                      unsigned bytes) {
            for (unsigned i = 0; i < bytes; ++i)
                data[offset + i] = static_cast<char>(value >> (i * 8));
        };
        put(boot, 0x5c, 1, 8);
        std::fill(core.begin() + 488, core.begin() + 500, '\0');
        put(core, 500, 2, 8);
        put(core, 508, sectors - 1, 2);
        put(core, 510, 0x820, 2);
        core.resize(sectors * 512, '\0');
        disk.seekp(0);
        disk.write(boot.data(), 440);  // Keep MBR identity/partitions.
        disk.seekp(512);
        disk.write(core.data(), core.size());
        disk.flush();
        if (!disk)
            throw std::runtime_error("cannot embed GRUB into template image");
        return;
    }
    if (context.argument == "prepare") {
        std::ifstream source(context.image, std::ios::binary);
        unsigned char mbr[512];
        source.read(reinterpret_cast<char*>(mbr), 512);
        uint32_t start = 0, sectors = 0;
        memcpy(&start, mbr + 454, 4);
        memcpy(&sectors, mbr + 458, 4);
        uint64_t offset = static_cast<uint64_t>(start) * 512,
                 size   = static_cast<uint64_t>(sectors) * 512;
        if (!source || mbr[510] != 0x55 || mbr[511] != 0xaa || !start ||
            !sectors || offset + size > fs::file_size(context.image))
            throw std::runtime_error("invalid MBR root partition");
        host::copy(context.image, stage / "smoke.img");
        write(stage / "root.ext3", "");
        fs::resize_file(stage / "root.ext3", size);
        write(stage / "partition.info",
              std::to_string(offset) + "|" + std::to_string(size) + "\n");
        write(stage / ".osinfo-stage", "isolated\n");
        return;
    }
    if (!fs::exists(stage / ".osinfo-stage"))
        throw std::runtime_error("missing isolated image marker");
    if (context.argument == "splice") {
        auto values     = split(read(stage / "partition.info"), '|');
        uint64_t offset = std::stoull(values.at(0)),
                 size   = std::stoull(values.at(1));
        if (fs::file_size(stage / "root.ext3") != size ||
            offset + size > fs::file_size(stage / "smoke.img"))
            throw std::runtime_error("partition size mismatch");
        std::fstream disk(stage / "smoke.img",
                          std::ios::binary | std::ios::in | std::ios::out);
        std::ifstream partition(stage / "root.ext3", std::ios::binary);
        disk.seekp(offset);
        char buffer[1024 * 1024];
        while (partition) {
            partition.read(buffer, sizeof(buffer));
            disk.write(buffer, partition.gcount());
        }
        disk.flush();
        if (!disk || !partition.eof())
            throw std::runtime_error("partition splice failed");
        return;
    }
    if (context.argument == "missing-library") {
        auto value       = read(context.image);
        std::string from = "libprobe.so", to = "liblost0.so";
        size_t position = value.find(from);
        if (position == std::string::npos)
            throw std::runtime_error("missing linker test dependency");
        value.replace(position, from.size(), to);
        write(stage / "root/usr/bin/ld-missing", value);
        return;
    }
    throw std::runtime_error("unknown image action");
}
}  // namespace host
