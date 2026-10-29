#include "host.h"
namespace host {
void generate(const Context& context) {
    auto services = table(context.build / "config/services.conf");
    std::set<std::string> names;
    std::map<std::string, Row> users;
    std::set<int> ids, orders;
    std::regex symbol("[A-Za-z_][A-Za-z_0-9]*");
    std::set<std::string> capabilities = {"0",
                                          "taskCapabilitySpawn",
                                          "taskCapabilityPci",
                                          "taskCapabilityDevice",
                                          "taskCapabilityDisplay",
                                          "taskCapabilityUsers",
                                          "taskCapabilityMemory",
                                          "taskCapabilityReadIdentity"};
    const auto capabilityExpression    = [&](const std::string& value) {
        std::string result;
        for (const auto& token : split(value, '+')) {
            if (!capabilities.count(token))
                throw std::runtime_error("invalid service capability: " +
                                            token);
            if (!result.empty()) result += " | ";
            result += token == "0" ? "0" : "kernel::task::" + token;
        }
        if (result.empty())
            throw std::runtime_error("empty service capabilities");
        return result;
    };
    for (const auto& service : services) {
        if (service.size() != 17 || !names.insert(service[0]).second)
            throw std::runtime_error("invalid or duplicate service");
        if (service[1] != "0" && service[1] != "3")
            throw std::runtime_error("invalid service ring");
        if (service[2] != "-" && !fs::is_directory("src/" + service[2]))
            throw std::runtime_error("missing service directory " + service[2]);
        if (service[1] == "0") {
            if (service[2].rfind("kernel/", 0) || service[15] != "resident" ||
                service[16] != "kernel")
                throw std::runtime_error("Ring0 outside kernel");
            continue;
        }
        if (service[2].rfind("kernel/", 0) == 0)
            throw std::runtime_error("Ring3 inside kernel");
        if ((service[15] != "boot" && service[15] != "file") ||
            (service[16] != "init" && service[16] != "task" &&
             service[16] != "device" && service[16] != "kernel") ||
            (service[15] == "file" && service[16] != "init"))
            throw std::runtime_error("invalid service loading source or owner");
        if (!std::regex_match(service[3], symbol) ||
            !std::regex_match(service[5], symbol) ||
            !ids.insert(std::stoi(service[4])).second ||
            !orders.insert(std::stoi(service[11])).second)
            throw std::runtime_error("invalid fixed service identity");
        users[service[0]] = service;
    }
    for (const auto& [name, service] : users)
        if (service[10] != "-")
            for (const auto& dependency : split(service[10], ','))
                if (!users.count(dependency) ||
                    std::stoi(users.at(dependency)[11]) >=
                        std::stoi(service[11]))
                    throw std::runtime_error(
                        "missing, cyclic or unordered service dependency: " +
                        name);
    if (users.size() != 13 || *ids.begin() != 0 || *ids.rbegin() != 12)
        throw std::runtime_error(
            "fixed boot ABI requires 13 contiguous identities");
    std::vector<Row> boot;
    for (const auto& [name, service] : users) boot.push_back(service);
    std::sort(boot.begin(), boot.end(), [](const Row& a, const Row& b) {
        return std::stoi(a[4]) < std::stoi(b[4]);
    });
    std::ostringstream make, images, assertions, startup, grub, modules;
    make << "# Generated; edit script/config/*.json\nSERVICE_NAMES :=";
    for (const auto& service : boot)
        if (service[8] != "external") make << ' ' << service[0];
    make << "\nDISK_SERVICE_NAMES :=";
    for (const auto& service : boot)
        if (service[8] == "disk") make << ' ' << service[0];
    make << '\n';
    images << "ProgramImage PROGRAM_IMAGES[] = {\n";
    grub << "set timeout=0\nset default=0\nset gfxmode=1024x768x32\nset "
            "gfxpayload=keep\nmenuentry \"OS Info x86-64\" {\n    multiboot2 "
            "/boot/kernel.elf\n";
    for (const auto& service : boot) {
        assertions
            << "static_assert(static_cast<uint64_t>(os::ProgramId::"
            << service[3] << ") == " << service[4]
            << ");\nstatic_assert(static_cast<uint32_t>(os::ServiceRole::"
            << service[5] << ") == " << service[6] << ");\n";
        images << "    {" << quote(service[0]) << ", " << quote(service[12])
               << ", 0, 0, " << capabilityExpression(service[7])
               << ", os::ServiceRole::" << service[5] << ", "
               << (service[15] == "boot" ? "true" : "false") << "},\n";
        if (service[8] != "external")
            make << "SERVICE_DIR_" << service[0] << " := " << service[2]
                 << '\n';
        if (service[14] == "recursive")
            make << "SERVICE_EXTRA_" << service[0]
                 << " := $(wildcard $(SOURCE_DIR)/" << service[2]
                 << "/*/*.cc)\n";
        if (service[13] != "-") {
            make << "SERVICE_LIBS_" << service[0] << " =";
            for (const auto& library : split(service[13], ',')) {
                if (library == "graphic")
                    make << " $(GRAPHIC_A)";
                else if (library == "c")
                    make << " $(LIBC_SO)";
                else
                    throw std::runtime_error("unknown service library");
            }
            make << '\n';
        }
        modules << service[0] << '|' << service[8] << '\n';
        if (service[15] == "boot")
            grub << "    module2 /boot/" << service[0] << ".elf " << service[0]
                 << '\n';
    }
    images << "};\n";
    images << "static constexpr const char* configuredRuntimeLibraries[] = {\n";
    for (const auto& row : table(context.build / "config/layout.conf"))
        if (row[0] == "runtime")
            images << "    " << quote(row.at(2)) << ",\n";
    images << "};\nstatic constexpr uint32_t configuredRuntimeCount = "
              "sizeof(configuredRuntimeLibraries) / "
              "sizeof(configuredRuntimeLibraries[0]);\n";
    std::sort(boot.begin(), boot.end(), [](const Row& a, const Row& b) {
        return std::stoi(a[11]) < std::stoi(b[11]);
    });
    startup << "struct ConfiguredService { os::ProgramId program; const char* "
               "ready; const char* name; const char* owner; };\nstatic const "
               "ConfiguredService "
               "configuredServices[] = {\n";
    for (const auto& service : boot)
        if (service[0] != "init" && service[15] == "boot")
            startup << "    {os::ProgramId::" << service[3] << ", "
                    << quote(service[9]) << ", " << quote(service[0]) << ", "
                    << quote(service[16]) << "},\n";
    startup << "};\n";
    for (const auto& row : table(context.build / "config/layout.conf"))
        if (row[0] == "runtime") {
            grub << "    module2 /boot/" << row.at(2) << ' ' << row[2] << '\n';
        }
    grub << "    boot\n}\n";
    auto out = context.build / "generated";
    write(out / "services.mk", make.str());
    write(out / "boot-images.inc", images.str());
    write(out / "boot-abi.inc", assertions.str());
    write(out / "startup.inc", startup.str());
    write(out / "grub.cfg", grub.str());
    write(out / "modules.conf", modules.str());
    write(out / "stamp", "generated\n");
}
}  // namespace host
