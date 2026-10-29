#include "host.h"
namespace host {
namespace {
void developmentPackage(const Context& context,const fs::path& destination,
                        const std::string& name,const std::string& headerPrefix,
                        const std::vector<std::string>& requiredHeaders) {
    const auto libraryName=name+".so";
    const auto archive = context.packages / (name+".inpk");
    if (!fs::is_regular_file(archive))
        throw std::runtime_error("missing external SDK package: "+archive.filename().string());
    run({"tools/inpkg", "check", archive.string()});
    const auto metadata = packageMetadata(archive);
    if (metadata.name != name)
        throw std::runtime_error("unexpected GUI package identity");
    const auto manifest =
        capture({"tar", "-xOzf", archive.string(), "package.conf"});
    std::istringstream lines(manifest);
    std::string line, version;
    bool library = false;
    std::set<std::string> headers, outputs;
    while (std::getline(lines, line)) {
        if (line.rfind("version=", 0) == 0) version = line.substr(8);
        if (line.rfind("install=", 0) != 0) continue;
        auto separator = line.find(" -> ", 8);
        if (separator == std::string::npos)
            throw std::runtime_error("invalid GUI install mapping");
        auto member = line.substr(8, separator - 8),
             target = line.substr(separator + 4);
        fs::path output;
        if (target == ("/usr/lib/"+libraryName))
            output = fs::path("lib")/libraryName;
        else if (!headerPrefix.empty() && target.rfind("/usr/include/"+headerPrefix, 0) == 0) {
            output = fs::path("include") /
                     target.substr(std::string("/usr/include/").size());
            headers.insert(output.filename().string());
        } else
            continue;
        const auto checked = inside(destination, output);
        if (!outputs.insert(output.generic_string()).second)
            throw std::runtime_error("duplicate GUI SDK output");
        // The package checker validates members and mappings. Read only
        // selected regular payloads instead of extracting a system tree into
        // the SDK.
        auto data = capture({"tar", "-xOzf", archive.string(), member});
        if (target == ("/usr/lib/"+libraryName)) {
            if (data.size() < 64 ||
                data.substr(0, 4) != std::string("\177ELF", 4) ||
                data[4] != 2 || data[5] != 1 ||
                static_cast<unsigned char>(data[18]) != 62 || data[19])
                throw std::runtime_error(
                    name+" must be little-endian ELF64 x86-64");
            library = true;
        }
        write(checked, data);
        if (target == ("/usr/lib/"+libraryName) && name.rfind("lib",0)!=0)
            write(destination / "lib" / ("lib"+libraryName),data);
    }
    if (!library || version.empty())
        throw std::runtime_error("incomplete development payload: "+name);
    for(const auto& header:requiredHeaders)
        if(!headers.count(header))throw std::runtime_error("missing SDK header: "+name+"/"+header);
    write(destination / (name+".version"), version + "\n");
}
void guiPackage(const Context& context,const fs::path& destination) {
    developmentPackage(context,destination,"libguiw-font","guiw/",{"font.h"});
    developmentPackage(context,destination,"libosgui-c++","osgui-c++/",{"gui.h","protocol.h","session.h"});
    developmentPackage(context,destination,"guiw-language","",{});
}
}  // namespace
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
    if (context.argument == "gui-only") {
        guiPackage(context, destination);
        return;
    }
    // Remove only the retired generated GUI development files when refreshing
    // an existing SDK. Unknown files in that directory are left untouched.
    for (const auto& name :
         {"include/osgui-c++/osgui/font.h", "include/osgui-c++/osgui/gui.h",
          "lib/libosgui-c++.so"}) {
        const auto retired = inside(destination, name);
        if (fs::is_regular_file(retired)) fs::remove(retired);
    }
    auto definitions = table(context.build / "config/layout.conf");
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
    if (context.argument != "base") guiPackage(context, destination);
    std::map<std::string, std::string> hashes;
    for (const auto& entry : fs::recursive_directory_iterator(destination))
        if (entry.is_regular_file() && entry.path().filename() != "sdk.json") {
            auto hash =
                capture({"tools/inpkg", "digest", entry.path().string()});
            if (hash.size() != 65 || hash.back() != '\n' ||
                hash.find_first_not_of("0123456789abcdef") != 64)
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
