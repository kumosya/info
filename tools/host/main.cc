#include <cerrno>
#include <unistd.h>

#include "host.h"
#include <sys/wait.h>
namespace host {
std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), {}};
}
void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    auto temporary = path.string() + ".tmp." + std::to_string(getpid());
    std::ofstream output(temporary, std::ios::binary);
    output << value;
    output.close();
    if (!output) throw std::runtime_error("cannot write " + temporary);
    fs::rename(temporary, path);
}
std::string quote(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else if (c < 32)
            throw std::runtime_error("control character in configuration");
        else
            result += c;
    }
    return result + '"';
}
std::vector<std::string> split(const std::string& value, char separator) {
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string item;
    while (std::getline(input, item, separator)) result.push_back(item);
    return result;
}
Table table(const fs::path& path) {
    Table rows;
    std::istringstream stream(read(path));
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.find_first_of("\t\0", 0, 2) != std::string::npos)
            throw std::runtime_error("invalid config control byte");
        rows.push_back(split(line, '|'));
    }
    return rows;
}
std::string Context::expand(std::string value) const {
    for (const auto& [key, path] :
         std::map<std::string, fs::path>{{"{build}", build},
                                         {"{packages}", packages},
                                         {"{objects}", objects}}) {
        size_t offset = 0;
        while ((offset = value.find(key, offset)) != std::string::npos) {
            value.replace(offset, key.size(), path.string());
            offset += path.string().size();
        }
    }
    return value;
}
void run(const std::vector<std::string>& arguments, const fs::path& output) {
    std::vector<char*> argv;
    for (const auto& argument : arguments) {
        if (argument.find('\0') != std::string::npos)
            throw std::runtime_error("NUL process argument");
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (!child) {
        if (!output.empty()) {
            FILE* file = freopen(output.c_str(), "w", stdout);
            if (!file) _exit(126);
        }
        execvp(argv[0], argv.data());
        perror(argv[0]);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR) throw std::runtime_error("waitpid failed");
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("command failed: " + arguments[0]);
}
std::string capture(const std::vector<std::string>& arguments) {
    char temporary[] = "/tmp/osinfo-host-output.XXXXXX";
    int descriptor = mkstemp(temporary);
    if (descriptor < 0) throw std::runtime_error("cannot create tool output");
    close(descriptor);
    try {
        run(arguments, temporary);
        auto result = read(temporary);
        fs::remove(temporary);
        return result;
    } catch (...) { fs::remove(temporary); throw; }
}
PackageMetadata packageMetadata(const fs::path& path, bool manifest) {
    std::istringstream output(capture({"tools/inpkg", manifest ? "inspect-manifest" : "inspect", path.string()}));
    PackageMetadata result;
    std::string line;
    while (std::getline(output, line)) {
        auto columns = split(line, '\t');
        if (columns.size() != 2) throw std::runtime_error("invalid inpkg metadata output");
        if (columns[0] == "name") result.name = columns[1];
        else if (columns[0] == "bin" || columns[0] == "file") result.files.emplace_back(columns[1], columns[0] == "bin");
        else throw std::runtime_error("unknown inpkg metadata row");
    }
    if (result.name.empty()) throw std::runtime_error("missing inpkg metadata");
    return result;
}
fs::path inside(const fs::path& root, const fs::path& relative) {
    if (relative.empty() || relative.is_absolute())
        throw std::runtime_error("expected relative target");
    auto base   = fs::canonical(root);
    auto value  = fs::weakly_canonical(base / relative);
    auto suffix = value.lexically_relative(base);
    if (suffix.empty() || suffix.is_absolute() || *suffix.begin() == "..")
        throw std::runtime_error("target escapes root");
    fs::path walked = base;
    for (const auto& part : relative) {
        if (part == ".." || part == ".")
            throw std::runtime_error("invalid path component");
        walked /= part;
        if (fs::is_symlink(walked))
            throw std::runtime_error("symlink target refused: " +
                                     walked.string());
    }
    return base / relative;
}
void copy(const fs::path& source, const fs::path& destination) {
    if (fs::is_symlink(source) || !fs::is_regular_file(source))
        throw std::runtime_error("missing or linked source: " +
                                 source.string());
    fs::create_directories(destination.parent_path());
    auto temporary = destination.string() + ".copy." + std::to_string(getpid());
    try {
        fs::copy_file(source, temporary);
        fs::rename(temporary, destination);
    } catch (...) {
        if (fs::is_regular_file(temporary) && !fs::is_symlink(temporary)) fs::remove(temporary);
        throw;
    }
}
}  // namespace host
int main(int argc, char** argv) {
    try {
        if (argc < 2)
            throw std::runtime_error(
                "usage: osinfo-host "
                "prepare|generate|sdk|layout|deploy|audit|image|input [options]");
        host::Context context;
        for (int i = 2; i < argc; ++i) {
            std::string key = argv[i];
            if (i + 1 == argc) throw std::runtime_error("missing option value");
            std::string value = argv[++i];
            if (key == "--build")
                context.build = value;
            else if (key == "--packages")
                context.packages = value;
            else if (key == "--objects")
                context.objects = value;
            else if (key == "--root")
                context.root = value;
            else if (key == "--output")
                context.output = value;
            else if (key == "--image")
                context.image = value;
            else if (key == "--argument")
                context.argument = value;
            else if (key == "--clicks") {
                size_t used = 0;
                const auto count = std::stoul(value, &used);
                if (used != value.size() || count == 0 || count > 100000)
                    throw std::runtime_error("clicks must be 1..100000");
                context.clicks = static_cast<unsigned>(count);
            }
            else if (key == "--pattern") {
                if (value != "stationary" && value != "windows" && value != "pressure") throw std::runtime_error("pattern must be stationary, windows or pressure");
                context.pattern = value;
            }
            else
                throw std::runtime_error("unknown option " + key);
        }
        std::string command = argv[1];
        if (command == "defaults")
            host::defaults(context);
        else if (command == "prepare")
            host::prepare(context);
        else if (command == "generate")
            host::generate(context);
        else if (command == "sdk")
            host::sdk(context);
        else if (command == "layout")
            host::layout(context);
        else if (command == "deploy")
            host::deploy(context);
        else if (command == "audit")
            host::audit(context);
        else if (command == "image")
            host::image(context);
        else if (command == "input")
            host::input(context);
        else
            throw std::runtime_error("unknown command " + command);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "osinfo-host: " << error.what() << '\n';
        return 1;
    }
}
