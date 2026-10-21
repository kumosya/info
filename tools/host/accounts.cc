#include "host.h"

namespace host {
void accounts(const Context& context) {
    if (context.argument != "development" || context.root.empty() || !fs::is_directory(context.root) ||
        fs::canonical(context.root) == "/") throw std::runtime_error("accounts requires --argument development and a staging --root");
    const auto root = fs::canonical(context.root);
    for (const char* name : {"passwd", "shadow"}) {
        const auto target = inside(root, fs::path("etc") / name);
        host::copy(fs::path("script/profiles/development") / name, target);
        fs::permissions(target, static_cast<fs::perms>(std::string(name) == "shadow" ? 0600 : 0644));
    }
}
}  // namespace host
