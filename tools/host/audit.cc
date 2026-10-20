#include "host.h"
namespace host {
static std::string mask(std::string value) {
    enum class State { Code, Line, Block, String, Character };
    State state = State::Code;
    bool escape = false;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i], next = i + 1 < value.size() ? value[i + 1] : 0;
        if (state == State::Code) {
            if (c == '/' && next == '/') {
                value[i] = ' ';
                state    = State::Line;
            } else if (c == '/' && next == '*') {
                value[i] = ' ';
                state    = State::Block;
                ++i;
                value[i] = ' ';
            } else if (c == '"' || c == '\'') {
                value[i] = ' ';
                state    = c == '"' ? State::String : State::Character;
                escape   = false;
            }
        } else if (state == State::Line) {
            if (c == '\n')
                state = State::Code;
            else
                value[i] = ' ';
        } else if (state == State::Block) {
            if (c == '*' && next == '/') {
                value[i]   = ' ';
                value[++i] = ' ';
                state      = State::Code;
            } else if (c != '\n')
                value[i] = ' ';
        } else {
            if (c != '\n') value[i] = ' ';
            if (!escape && ((state == State::String && c == '"') ||
                            (state == State::Character && c == '\'')))
                state = State::Code;
            if (!escape && c == '\\')
                escape = true;
            else
                escape = false;
        }
    }
    return value;
}
void audit(const Context& context) {
    std::set<std::string> roots, allowed, headers, forbidden, memory;
    for (const auto& row : table("config/checks.conf")) {
        if (row.size() != 2) throw std::runtime_error("invalid audit policy");
        if (row[0] == "audit-root")
            roots.insert(row[1]);
        else if (row[0] == "syscall")
            allowed.insert(row[1]);
        else if (row[0] == "memory-root")
            memory.insert(row[1]);
        else if (row[0] == "memory-header")
            headers.insert(row[1]);
        else if (row[0] == "forbidden-memory-header")
            forbidden.insert(row[1]);
        else if (row[0] != "project")
            throw std::runtime_error("unknown audit policy");
    }
    for (const auto& row : table("config/services.conf"))
        if (row[1] == "3" && row[8] != "external")
            roots.insert("src/" + row[2]);
    std::vector<std::string> violations;
    for (const auto& root : memory)
        if (fs::exists(root))
            for (const auto& entry : fs::recursive_directory_iterator(root))
                if (entry.path().extension() == ".h" &&
                    !headers.count(entry.path().filename().string()))
                    violations.push_back("memory header: " +
                                         entry.path().string());
    for (const auto& root : {"include/kernel", "include/os-c++/os"})
        for (const auto& name : forbidden)
            if (fs::exists(fs::path(root) / name))
                violations.push_back("forbidden header: " +
                                     (fs::path(root) / name).string());
    std::regex classPattern(
        R"(\b(?:class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?\w+\s*(?:final\s*)?(?::[^{};]+)?\{)");
    std::regex functionPattern(
        R"(^[ \t]*(?:(?:inline|static|constexpr|\[\[[^\]]*\]\])\s+)*[A-Za-z_][\w:<>,*& \t]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{)",
        std::regex::ECMAScript | std::regex::multiline);
    std::set<fs::path> inspected;
    for (const auto& root : roots)
        for (const auto& entry : fs::recursive_directory_iterator(root))
            if ((entry.path().extension() == ".h" ||
                 entry.path().extension() == ".cc") &&
                inspected.insert(entry.path()).second) {
                auto original = read(entry.path()), value = mask(original);
                std::vector<size_t> stack;
                std::map<size_t, size_t> ends;
                for (size_t i = 0; i < value.size(); ++i) {
                    if (value[i] == '{')
                        stack.push_back(i);
                    else if (value[i] == '}' && !stack.empty()) {
                        ends[stack.back()] = i;
                        stack.pop_back();
                    }
                }
                std::vector<std::pair<size_t, size_t>> classes;
                for (std::sregex_iterator
                         it(value.begin(), value.end(), classPattern),
                     end;
                     it != end; ++it) {
                    size_t opening = value.find('{', it->position());
                    if (ends.count(opening))
                        classes.emplace_back(opening, ends.at(opening));
                }
                for (std::sregex_iterator
                         it(value.begin(), value.end(), functionPattern),
                     end;
                     it != end; ++it) {
                    std::string name = (*it)[1];
                    if (name == "if" || name == "while" || name == "for" ||
                        name == "switch" || name == "catch" || name == "main" ||
                        name == "osRuntimeInit" || name == "osRuntimeFini")
                        continue;
                    size_t position     = it->position(),
                           namePosition = it->position(1);
                    if (namePosition && value[namePosition - 1] == ':')
                        continue;
                    bool member = false;
                    for (const auto& [begin, finish] : classes)
                        if (begin < position && position < finish)
                            member = true;
                    if (member) continue;
                    if (entry.path() == fs::path("lib/os/runtime.cc") &&
                        allowed.count(name))
                        continue;
                    violations.push_back(
                        entry.path().string() + ":" +
                        std::to_string(1 + std::count(value.begin(),
                                                      value.begin() + position,
                                                      '\n')) +
                        ": " + name);
                }
            }
    auto symbols = context.build / "audit-symbols.txt";
    run({"nm", "-g", "--defined-only",
         (context.objects / "user/runtime/runtime.o").string(),
         (context.objects / "user/runtime/service.o").string()},
        symbols);
    auto defined = read(symbols);
    fs::remove(symbols);
    auto imports = read("src/kernel/task/services.cc");
    std::regex importPattern(R"rx(\{"([^"]+)", reinterpret_cast<uint64_t>)rx");
    for (std::sregex_iterator it(imports.begin(), imports.end(), importPattern),
         end;
         it != end; ++it)
        if (defined.find((*it)[1].str()) == std::string::npos)
            violations.push_back("missing trusted import: " + (*it)[1].str());
    for (const auto& violation : violations) std::cerr << violation << '\n';
    if (!violations.empty())
        throw std::runtime_error("source/ABI audit failed");
    std::cout
        << "Service object, memory header and trusted import audits passed\n";
}
}  // namespace host
