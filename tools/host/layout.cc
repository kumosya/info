#include <unistd.h>

#include "host.h"
#include <sys/stat.h>
namespace host {
struct Account {
    std::string home;
    unsigned uid, gid;
};
static std::vector<Account> accounts(const fs::path& root) {
    std::vector<Account> result;
    auto passwd = inside(root, "etc/passwd");
    if (!fs::exists(passwd)) return result;
    std::istringstream input(read(passwd));
    std::string line;
    while (std::getline(input, line)) {
        auto fields = split(line, ':');
        if (fields.size() != 7 || fields[5].empty() || fields[5][0] != '/')
            continue;
        auto home = fields[5].substr(1);
        inside(root, home);
        result.push_back({home, static_cast<unsigned>(std::stoul(fields[2])),
                          static_cast<unsigned>(std::stoul(fields[3]))});
    }
    return result;
}
static void merge(const fs::path& root, const fs::path& source,
                  const fs::path& destination) {
    auto from = inside(root, source), to = inside(root, destination);
    if (!fs::exists(from)) return;
    if (!fs::exists(to)) {
        fs::create_directories(to.parent_path());
        fs::rename(from, to);
        return;
    }
    if (fs::is_directory(from) && fs::is_directory(to)) {
        std::vector<fs::path> children;
        for (const auto& item : fs::directory_iterator(from))
            children.push_back(item.path().filename());
        for (const auto& child : children)
            merge(root, source / child, destination / child);
        if (fs::is_empty(from)) fs::remove(from);
    } else
        std::cerr << "layout: preserved migration conflict: " << from << '\n';
}
void layout(const Context& context) {
    if (context.root.empty() || !fs::is_directory(context.root) ||
        fs::canonical(context.root) == "/")
        throw std::runtime_error("layout requires non-host --root");
    auto root = fs::canonical(context.root);
    auto rows = table(context.build / "config/layout.conf");
    if (context.image.empty()) {
        for (const auto& row : rows)
            if (row[0] == "directory") {
                auto directory = inside(root, row.at(1));
                fs::create_directories(directory);
                fs::permissions(directory, static_cast<fs::perms>(std::stoul(
                                               row.at(2), nullptr, 8)));
            }
        for (const auto& row : rows)
            if (row[0] == "legacy-database") merge(root, row[1], row[2]);
        for (const auto& account : accounts(root)) {
            auto home = inside(root, account.home);
            fs::create_directories(home);
            fs::permissions(
                home, static_cast<fs::perms>(account.uid == 0 ? 0700 : 0755));
            for (const auto& row : rows)
                if (row[0] == "legacy-home")
                    merge(root, fs::path(account.home) / row[1],
                          fs::path(account.home) / row[2]);
            for (const auto& row : rows)
                if (row[0] == "user-directory") {
                    auto directory =
                        inside(root, fs::path(account.home) / row[1]);
                    fs::create_directories(directory);
                    fs::permissions(
                        directory,
                        static_cast<fs::perms>(std::stoul(row[2], nullptr, 8)));
                }
            if (geteuid() == 0) {
                for (const auto& item : fs::recursive_directory_iterator(home))
                    if (!fs::is_symlink(item.path()) &&
                        chown(item.path().c_str(), account.uid, account.gid))
                        throw std::runtime_error("chown user layout failed");
                if (chown(home.c_str(), account.uid, account.gid))
                    throw std::runtime_error("chown home failed");
            }
        }
        return;
    }
    auto users = accounts(root);
    std::map<std::string, unsigned> modes;
    for (const auto& row : rows)
        if (row[0] == "directory" || row[0] == "mode" || row[0] == "copy")
            modes[row[row[0] == "copy" ? 2 : 1]] =
                std::stoul(row[row[0] == "copy" ? 3 : 2], nullptr, 8);
    for (const auto& service : table(context.build / "config/services.conf"))
        if (service[1] == "3") {
            modes["boot/" + service[0] + ".elf"] = 0755;
            modes["sbin/sys/" + service[0] + ".elf"] = 0755;
        }
    // Package declarations are authoritative for payload permissions.
    auto database = inside(root, "var/lib/inpkg");
    if (fs::exists(database))
        for (const auto& file : fs::directory_iterator(database))
            if (file.path().extension() == ".conf") {
                const auto manifest = packageMetadata(file.path(), true);
                for (const auto& [target, executable] : manifest.files)
                    modes[target.substr(1)] = executable ? 0755 : 0644;
            }
    std::vector<fs::path> paths{root};
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) throw std::runtime_error("linked image input");
        paths.push_back(entry.path());
    }
    std::ostringstream batch;
    for (const auto& path : paths) {
        std::string relative =
            path == root ? "" : path.lexically_relative(root).generic_string();
        unsigned uid = 0, gid = 0, mode = fs::is_directory(path) ? 0755 : 0644;
        if (modes.count(relative)) mode = modes.at(relative);
        for (const auto& user : users)
            if (relative == user.home ||
                relative.rfind(user.home + "/", 0) == 0) {
                uid = user.uid;
                gid = user.gid;
                mode =
                    fs::is_directory(path)
                        ? (relative == user.home ? (uid ? 0755 : 0700) : 0700)
                        : 0600;
            }
        std::string target = "/" + relative;
        batch << "set_inode_field " << quote(target) << " uid " << uid
              << "\nset_inode_field " << quote(target) << " gid " << gid
              << "\nset_inode_field " << quote(target) << " mode 0" << std::oct
              << (mode | (fs::is_directory(path) ? S_IFDIR : S_IFREG))
              << std::dec << '\n';
    }
    auto commands = context.image.string() + ".permissions";
    write(commands, batch.str());
    run({"debugfs", "-w", "-f", commands, fs::absolute(context.image).string()},
        "/dev/null");
    fs::remove(commands);
}
void deploy(const Context& context) {
    if (context.root.empty() || !fs::is_directory(context.root) ||
        fs::canonical(context.root) == "/")
        throw std::runtime_error("deploy requires non-host --root");
    auto root = fs::canonical(context.root);
    const auto legacyConsole = inside(root, "etc/console.conf");
    const auto console = inside(root, "etc/console");
    if (fs::exists(legacyConsole) && !fs::exists(console)) host::copy(legacyConsole, console);
    for (const auto& row : table(context.build / "config/layout.conf")) {
        if (row[0] == "copy") {
            auto destination = inside(root, row.at(2));
            host::copy(context.expand(row.at(1)), destination);
            fs::permissions(destination, static_cast<fs::perms>(std::stoul(
                                             row.at(3), nullptr, 8)));
        } else if (row[0] == "tree") {
            for (const auto& entry :
                 fs::recursive_directory_iterator(row.at(1)))
                if (entry.is_regular_file()) {
                    const auto target = inside(root, fs::path(row.at(2)) / entry.path().lexically_relative(row[1]));
                    if (row[2] != "etc" || !fs::exists(target)) host::copy(entry.path(), target);
                }
        } else if (row[0] == "external-runtime") {
            const auto installed = inside(root, fs::path("usr/lib") / row.at(2));
            // deploy runs before and after the package installation. Only the
            // latter pass has external runtime payloads available to stage.
            if (fs::exists(installed))
                host::copy(installed, inside(root, fs::path("boot") / row.at(2)));
        } else if (row[0] == "runtime") {
            auto name = row.at(2);
            host::copy(context.packages / row.at(1) / name,
                       inside(root, fs::path("boot") / name));
        }
    }
    for (const auto& row : table(context.build / "config/services.conf"))
        if (row.at(1) == "3") {
            auto name   = row[0] + ".elf";
            auto source = row[8] == "external"
                              ? inside(root, fs::path("sbin/sys") / name)
                              : context.build / "services" / name;
            if (!fs::exists(source) && row[8] == "external") continue;
            if (row[15] == "boot") {
                host::copy(source, inside(root, fs::path("boot") / name));
                fs::permissions(inside(root, fs::path("boot") / name), static_cast<fs::perms>(0755));
            } else fs::remove(inside(root, fs::path("boot") / name));
            if (row[8] != "external") {
                host::copy(source, inside(root, fs::path("sbin/sys") / name));
                fs::permissions(inside(root, fs::path("sbin/sys") / name), static_cast<fs::perms>(0755));
            }
        }
    for (const auto& row : table(context.build / "config/layout.conf"))
        if (row[0] == "mode")
            fs::permissions(
                inside(root, row[1]),
                static_cast<fs::perms>(std::stoul(row[2], nullptr, 8)));
}
}  // namespace host
