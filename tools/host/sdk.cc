#include "host.h"
namespace host {
void sdk(const Context& context) {
    if (context.output.empty())
        throw std::runtime_error("sdk requires --output");
    auto destination = fs::weakly_canonical(context.output),
         repository  = fs::canonical(context.repository);
    auto suffix      = repository.lexically_relative(destination);
    if (destination == repository ||
        (!suffix.empty() && *suffix.begin() != ".."))
        throw std::runtime_error(
            "SDK cannot replace source root or its ancestors");
    fs::create_directories(destination);
    auto definitions = table("config/layout.conf");
    for (const auto& row : definitions) {
        if (row[0] == "sdk-tree") {
            auto target = destination / "include" / row.at(2);
            for (const auto& entry :
                 fs::recursive_directory_iterator(row.at(1)))
                if (entry.is_regular_file() && !entry.is_symlink())
                    host::copy(
                        entry.path(),
                        target / entry.path().lexically_relative(row[1]));
                else if (entry.is_symlink())
                    throw std::runtime_error("linked SDK header");
        } else if (row[0] == "sdk-file")
            host::copy(context.expand(row.at(1)), destination / row.at(2));
        else if (row[0] == "runtime")
            host::copy(context.packages / row.at(1) / row.at(2),
                       destination / "lib" / row[2]);
    }
    std::map<std::string, std::string> hashes;
    for (const auto& entry : fs::recursive_directory_iterator(destination))
        if (entry.is_regular_file() && entry.path().filename() != "sdk.json") {
            
            auto hash = capture({"tools/inpkg", "digest", entry.path().string()});
            if (hash.size() != 65 || hash.back() != '\n' || hash.find_first_not_of("0123456789abcdef") != 64)
                throw std::runtime_error("invalid SDK digest output");
            hash.pop_back();
            hashes[entry.path()
                       .lexically_relative(destination)
                       .generic_string()] = hash;
        }
    std::ostringstream metadata;
    metadata << "{\n  \"format\": 1,\n  \"target\": \"osinfo-x86-64\",\n  "
                "\"syscall_count\": 19,\n  \"interpreter\": "
                "\"/lib/ld-osinfo-x86-64.so\",\n  \"files\": {\n";
    bool first = true;
    for (const auto& [name, hash] : hashes) {
        if (!first) metadata << ",\n";
        first = false;
        metadata << "    " << quote(name) << ": " << quote(hash);
    }
    metadata << "\n  }\n}\n";
    write(destination / "sdk.json", metadata.str());
    std::cout << "SDK exported: " << destination << " (" << hashes.size()
              << " files)\n";
}
}  // namespace host
