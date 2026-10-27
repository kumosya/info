#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace host {
namespace fs = std::filesystem;
using Row    = std::vector<std::string>;
using Table  = std::vector<Row>;
struct Context {
    fs::path repository = fs::current_path(), build = "build",
             packages = "packages", objects = "obj", root, output, image;
    std::string argument;
    unsigned clicks     = 100;
    std::string pattern = "stationary";
    std::string expand(std::string value) const;
};
std::string read(const fs::path& path);
void write(const fs::path& path, const std::string& value);
std::string quote(const std::string& value);
Table table(const fs::path& path);
std::vector<std::string> split(const std::string& value, char separator);
void run(const std::vector<std::string>& arguments,
         const fs::path& output = {});
std::string capture(const std::vector<std::string>& arguments);
struct PackageMetadata {
    std::string name;
    std::vector<std::pair<std::string, bool>> files;
};
PackageMetadata packageMetadata(const fs::path& path, bool manifest = false);
fs::path inside(const fs::path& root, const fs::path& relative);
void copy(const fs::path& source, const fs::path& destination);
void generate(const Context& context);
void defaults(const Context& context);
void prepare(const Context& context);
void sdk(const Context& context);
void layout(const Context& context);
void deploy(const Context& context);
void audit(const Context& context);
void image(const Context& context);
void input(const Context& context);
void accounts(const Context& context);
}  // namespace host
