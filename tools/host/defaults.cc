#include <iomanip>

#include "host.h"
#include "qmp_json.h"

namespace host {
namespace {
using Json = host_json::Json;
class ConfigReader {
    fs::path path_;

   public:
    explicit ConfigReader(const fs::path& path) : path_(path) {}
    Json load() const {
        try {
            return Json::parse(read(path_));
        } catch (const std::exception& error) {
            throw std::runtime_error(path_.string() + ": " + error.what());
        }
    }
    void keys(const Json& value,
              const std::vector<std::string>& expected) const {
        for (const auto& [key, item] : value.object()) {
            (void)item;
            if (std::find(expected.begin(), expected.end(), key) ==
                expected.end())
                fail("unknown field: " + key);
        }
    }
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(path_.string() + ": " + message);
    }
    std::string cell(const Json& value) const {
        std::string result;
        if (const auto* text = std::get_if<std::string>(&value.value))
            result = *text;
        else if (const auto* number = std::get_if<double>(&value.value)) {
            if (*number < 0 || *number > 9007199254740991.0 ||
                std::floor(*number) != *number)
                fail("expected a nonnegative integer");
            std::ostringstream output;
            output << std::fixed << std::setprecision(0) << *number;
            result = output.str();
        } else
            fail("expected a string or nonnegative integer");
        if (result.empty() ||
            result.find_first_of("|\r\n\t", 0, 5) != std::string::npos ||
            result.find('\0') != std::string::npos)
            fail("empty field or reserved separator/control character");
        return result;
    }
    std::string text(const Json& value) const {
        if (!std::holds_alternative<std::string>(value.value))
            fail("expected a string");
        return cell(value);
    }
    void version(const Json& root, std::vector<std::string> expected) const {
        expected.push_back("version");
        keys(root, expected);
        const auto* version = std::get_if<double>(&root.at("version").value);
        if (!version || *version != 1)
            fail("unsupported version; expected integer 1");
    }
    void row(std::ostringstream& output, const Row& cells) const {
        for (size_t i = 0; i < cells.size(); ++i) {
            if (i) output << '|';
            output << cells[i];
        }
        output << '\n';
    }
};
std::string services(const fs::path& path) {
    ConfigReader reader(path);
    const auto root = reader.load();
    reader.version(root, {"services"});
    const std::vector<std::string> fields = {
        "name",         "ring",   "source",     "program",   "id",
        "role",         "roleId", "capability", "origin",    "ready",
        "dependencies", "order",  "process",    "libraries", "traversal",
        "loading",      "owner"};
    std::ostringstream output;
    output << "# Generated from script/config/services.json; edit the JSON "
              "source.\n";
    for (const auto& service : root.at("services").array()) {
        reader.keys(service, fields);
        Row cells;
        for (const auto& field : fields)
            cells.push_back(field == "ring" || field == "id" ||
                                    field == "roleId" || field == "order"
                                ? reader.cell(service.at(field))
                                : reader.text(service.at(field)));
        reader.row(output, cells);
    }
    if (root.at("services").array().empty())
        reader.fail("services must not be empty");
    return output.str();
}
std::string layout(const fs::path& path) {
    ConfigReader reader(path);
    const auto root = reader.load();
    reader.version(root, {"entries"});
    const std::map<std::string, std::vector<std::string>> schemas = {
        {"directory", {"path", "mode"}},
        {"user-directory", {"path", "mode"}},
        {"copy", {"source", "target", "mode"}},
        {"tree", {"source", "target"}},
        {"mode", {"path", "mode"}},
        {"runtime", {"package", "library"}},
        {"external-runtime", {"package", "library"}},
        {"sdk-tree", {"source", "target"}},
        {"sdk-file", {"source", "target"}},
        {"legacy-database", {"source", "target"}},
        {"legacy-home", {"source", "target"}}};
    std::ostringstream output;
    output << "# Generated from script/config/layout.json; edit the JSON "
              "source.\n";
    for (const auto& entry : root.at("entries").array()) {
        const auto type  = reader.text(entry.at("type"));
        const auto found = schemas.find(type);
        if (found == schemas.end()) reader.fail("unknown layout type: " + type);
        auto fields = found->second;
        fields.push_back("type");
        reader.keys(entry, fields);
        Row cells{type};
        for (const auto& field : found->second) {
            const auto value = reader.text(entry.at(field));
            if (field == "mode" &&
                (value.size() < 3 || value.size() > 4 ||
                 !std::all_of(value.begin(), value.end(),
                              [](char c) { return c >= '0' && c <= '7'; })))
                reader.fail("mode must be an octal permission string");
            cells.push_back(value);
        }
        reader.row(output, cells);
    }
    if (root.at("entries").array().empty())
        reader.fail("entries must not be empty");
    return output.str();
}
std::string checks(const fs::path& path) {
    ConfigReader reader(path);
    const auto root = reader.load();
    reader.version(root, {"project", "memoryRoots", "memoryHeaders",
                          "forbiddenMemoryHeaders", "auditRoots", "syscalls",
                          "runtimeEntries"});
    std::ostringstream output;
    output << "# Generated from script/config/checks.json; edit the JSON "
              "source.\n";
    reader.row(output, {"project", reader.text(root.at("project"))});
    for (const auto& [field, kind] :
         std::vector<std::pair<std::string, std::string>>{
             {"memoryRoots", "memory-root"},
             {"memoryHeaders", "memory-header"},
             {"forbiddenMemoryHeaders", "forbidden-memory-header"},
             {"auditRoots", "audit-root"},
             {"syscalls", "syscall"},
             {"runtimeEntries", "runtime-entry"}})
        for (const auto& value : root.at(field).array())
            reader.row(output, {kind, reader.text(value)});
    return output.str();
}
}  // namespace
void defaults(const Context& context) {
    // Validate all inputs before replacing any generated table.
    const auto source  = context.repository / "script/config";
    const auto convert = [&](const char* name,
                             std::string (*transform)(const fs::path&)) {
        const auto path = source / name;
        try {
            return transform(path);
        } catch (const std::exception& error) {
            const std::string message = error.what();
            throw std::runtime_error(message.rfind(path.string(), 0) == 0
                                         ? message
                                         : path.string() + ": " + message);
        }
    };
    const std::vector<std::pair<std::string, std::string>> outputs = {
        {"services.conf", convert("services.json", services)},
        {"layout.conf", convert("layout.json", layout)},
        {"checks.conf", convert("checks.json", checks)}};
    for (const auto& [name, value] : outputs) {
        const auto path = context.build / "config" / name;
        if (!fs::exists(path) || read(path) != value) write(path, value);
    }
}
}  // namespace host
