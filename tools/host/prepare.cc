#include <cstdint>
#include <unistd.h>
#include <sys/stat.h>

#include "host.h"

namespace host {
namespace {
std::string extension(const fs::path& path) {
    auto value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}
void makePath(const fs::path& path) {
    const auto value = path.generic_string();
    if (value.empty() || value.find_first_of(" \t\r\n#$:|\\") != std::string::npos)
        throw std::runtime_error("unsupported build path: " + value);
}
// Read central-directory metadata before invoking unzip. Only regular files
// and directories with bounded, relative paths are accepted.
void unpack(const fs::path& archive, const fs::path& root, const fs::path& build) {
    if (fs::file_size(archive) > 256 * 1024 * 1024)
        throw std::runtime_error("ZIP exceeds size limit");
    auto data = read(archive);
    auto integer = [&](size_t offset, unsigned count) -> uint32_t {
        if (offset > data.size() || count > data.size() - offset)
            throw std::runtime_error("truncated ZIP");
        uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i)
            value |= uint32_t(static_cast<unsigned char>(data[offset + i])) << (8 * i);
        return value;
    };
    size_t end = data.size();
    bool found = false;
    if (end >= 22) for (size_t i = end - 22;; --i) {
        if (integer(i, 4) == 0x06054b50 && i + 22 + integer(i + 20, 2) == end) {
            end = i; found = true; break;
        }
        if (!i || data.size() - i > 65557) break;
    }
    if (!found || integer(end + 4, 2) || integer(end + 6, 2) ||
        integer(end + 8, 2) != integer(end + 10, 2))
        throw std::runtime_error("invalid or multipart ZIP");
    size_t cursor = integer(end + 16, 4), size = integer(end + 12, 4);
    if (cursor > end || size != end - cursor)
        throw std::runtime_error("unsupported ZIP directory");
    std::set<std::string> paths;
    uint64_t total = 0;
    for (unsigned i = 0; i < integer(end + 10, 2); ++i) {
        if (integer(cursor, 4) != 0x02014b50 || integer(cursor + 8, 2) & 1 ||
            integer(cursor + 34, 2))
            throw std::runtime_error("unsupported ZIP member");
        size_t length = integer(cursor + 28, 2);
        size_t next = cursor + 46 + length + integer(cursor + 30, 2) + integer(cursor + 32, 2);
        if (next > end || cursor + 46 > end)
            throw std::runtime_error("truncated ZIP directory");
        std::string name = data.substr(cursor + 46, length);
        bool directory = !name.empty() && name.back() == '/';
        if (directory) name.pop_back();
        unsigned type = (integer(cursor + 38, 4) >> 16) & S_IFMT;
        total += integer(cursor + 24, 4);
        if (name.empty() || name.find_first_of(std::string("\0\r\n\t\\", 5)) != std::string::npos ||
            !paths.insert(name).second || (type && type != static_cast<unsigned>(directory ? S_IFDIR : S_IFREG)) ||
            total > 512 * 1024 * 1024 || extension(name) == ".zip")
            throw std::runtime_error("unsafe ZIP member: " + name);
        inside(root, name);
        cursor = next;
    }
    if (cursor != end) throw std::runtime_error("invalid ZIP entry count");
    fs::create_directories(build);
    std::string pattern = (build / ".unpack-XXXXXX").string();
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back(0);
    if (!mkdtemp(temporary.data())) throw std::runtime_error("cannot create ZIP stage");
    const fs::path stage = fs::absolute(temporary.data());
    try {
        const std::string unzip = fs::is_regular_file("tools/unzip") ? "tools/unzip" : "unzip";
        run({unzip, "-q", "-o", fs::absolute(archive).string(), "-d", stage.string()});
        for (const auto& item : fs::recursive_directory_iterator(stage)) {
            if (item.is_symlink() || (!item.is_directory() && !item.is_regular_file()))
                throw std::runtime_error("unsafe extracted ZIP member");
            auto destination = inside(root, item.path().lexically_relative(stage));
            if (item.is_regular_file()) host::copy(item.path(), destination);
            else fs::create_directories(destination);
        }
        fs::remove_all(stage);
    } catch (...) { fs::remove_all(stage); throw; }
}
}
void prepare(const Context& context) {
    makePath(context.packages); makePath(context.build); makePath(context.objects);
    if (!fs::is_directory(context.packages) || fs::is_symlink(context.packages))
        throw std::runtime_error("package directory is missing or linked");
    std::vector<fs::path> archives, inputs;
    for (const auto& item : fs::recursive_directory_iterator(context.packages)) {
        if (item.is_symlink()) throw std::runtime_error("linked package input");
        if (item.is_regular_file() && extension(item.path()) == ".zip")
            archives.push_back(item.path());
    }
    std::sort(archives.begin(), archives.end());
    for (const auto& archive : archives) unpack(archive, context.packages, context.build);
    std::set<std::string> runtime, identities, destinations, provided;
    std::ostringstream catalog, dependencies;
    dependencies << "PACKAGE_INPUTS :=";
    for (const auto& archive : archives) { makePath(archive); dependencies << ' ' << archive.generic_string(); }
    for (const auto& row : table("config/layout.conf")) if (row[0] == "runtime" && runtime.insert(row[1]).second) {
        auto path = fs::path("lib") / (row[1] + ".conf");
        const auto manifest = packageMetadata(path, true);
        catalog << "manifest|" << path.generic_string() << '\n';
        for (const auto& [target, executable] : manifest.files) { (void)executable; destinations.insert(target); }
    }
    for (const auto& item : fs::recursive_directory_iterator(context.packages))
        if (item.is_regular_file() && extension(item.path()) == ".inpk") inputs.push_back(item.path());
    std::sort(inputs.begin(), inputs.end());
    for (const auto& input : inputs) {
        makePath(input);
        const auto package = packageMetadata(input);
        std::string name = package.name;
        
        if (!identities.insert(name).second) throw std::runtime_error("duplicate package: " + name);
        if (runtime.count(name)) continue;
        catalog << "archive|" << input.generic_string() << '\n';
        dependencies << ' ' << input.generic_string();
        for (const auto& [target, executable] : package.files) {
            (void)executable;
            if (!destinations.insert(target).second)
                throw std::runtime_error("package path conflict: " + target);
            provided.insert(target);
        }
    }
    if (context.argument != "base") {
        for (const auto& row : table("config/layout.conf"))
            if (row[0] == "external-runtime" &&
                !provided.count("/usr/lib/" + row.at(2)))
                throw std::runtime_error("missing external runtime package: " + row.at(1) + ".inpk (use --bootstrap-sdk to build a base SDK first)");
        for (const auto& service : table("config/services.conf"))
            if (service[8] == "external" && !provided.count("/sbin/sys/" + service[0] + ".elf"))
                throw std::runtime_error("missing service package: " + service[0]);
        std::set<std::string> sessions;
        for (const auto& app : table("script/etc/sessions")) {
            if (app.size() != 3 || (app[0] != "terminal" && app[0] != "login") ||
                !sessions.insert(app[0]).second)
                throw std::runtime_error("invalid session defaults");
            if (!provided.count(app[1]))
                throw std::runtime_error("missing session application package: " + app[1]);
        }
        if (sessions.size() != 2) throw std::runtime_error("incomplete session defaults");
    }
    dependencies << '\n';
    write(context.build / "packages.conf", catalog.str());
    write(context.build / "package-inputs.mk", dependencies.str());
    run({"tools/inpkg", "catalog", "--config", (context.build / "packages.conf").string(),
         "--package-dir", context.packages.string(), "--makefile", (context.build / "packages.mk").string()});
    std::cout << "Configured " << identities.size() << " release packages and " << runtime.size() << " OS runtime packages\n";
}
}  // namespace host
