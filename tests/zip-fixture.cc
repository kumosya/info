#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// A stored, single-member ZIP fixture, including deliberately invalid names.
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream input(argv[3], std::ios::binary);
    if (!input) return 1;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {}), output;
    const std::string name = argv[2];
    uint32_t crc = UINT32_MAX;
    for (auto byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    crc ^= UINT32_MAX;
    auto integer = [&](uint32_t value, unsigned size) { for (unsigned i = 0; i < size; ++i) output.push_back(value >> (i * 8)); };
    auto text = [&] { for (char c : name) output.push_back(c); };
    integer(0x04034b50, 4); integer(20, 2); integer(0, 2); integer(0, 2); integer(0, 4);
    integer(crc, 4); integer(data.size(), 4); integer(data.size(), 4); integer(name.size(), 2); integer(0, 2); text();
    output.insert(output.end(), data.begin(), data.end());
    const uint32_t directory = output.size();
    integer(0x02014b50, 4); integer(0x0314, 2); integer(20, 2); integer(0, 2); integer(0, 2); integer(0, 4);
    integer(crc, 4); integer(data.size(), 4); integer(data.size(), 4); integer(name.size(), 2);
    integer(0, 2); integer(0, 2); integer(0, 2); integer(0, 2); integer(0100644U << 16, 4); integer(0, 4); text();
    const uint32_t size = output.size() - directory;
    integer(0x06054b50, 4); integer(0, 2); integer(0, 2); integer(1, 2); integer(1, 2); integer(size, 4); integer(directory, 4); integer(0, 2);
    return std::ofstream(argv[1], std::ios::binary).write(reinterpret_cast<const char*>(output.data()), output.size()) ? 0 : 1;
}
