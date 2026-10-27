#include <chrono>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <thread>
#include <unistd.h>

#include "host.h"
#include "qmp_json.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
namespace host {
class Qmp {
    int socket_ = -1;
    std::string buffer_;

   public:
    ~Qmp() {
        if (socket_ >= 0) close(socket_);
    }
    void connect(const fs::path& path) {
        socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_ < 0) throw std::runtime_error("QMP socket failed");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        auto value         = path.string();
        if (value.size() >= sizeof(address.sun_path))
            throw std::runtime_error("QMP path too long");
        strcpy(address.sun_path, value.c_str());
        if (::connect(socket_, reinterpret_cast<sockaddr*>(&address),
                      sizeof(address)))
            throw std::runtime_error("QMP connect failed");
        message();
        request("qmp_capabilities", "{}");
    }
    host_json::Json message() {
        while (true) {
            auto end = buffer_.find('\n');
            if (end != std::string::npos) {
                auto line = buffer_.substr(0, end);
                buffer_.erase(0, end + 1);
                return host_json::Json::parse(line);
            }
            pollfd wait{socket_, POLLIN, 0};
            if (poll(&wait, 1, 10000) <= 0)
                throw std::runtime_error("QMP reply timed out");
            char bytes[8192];
            ssize_t count = recv(socket_, bytes, sizeof(bytes), 0);
            if (count <= 0) throw std::runtime_error("QMP disconnected");
            buffer_.append(bytes, count);
            if (buffer_.size() > 1024 * 1024)
                throw std::runtime_error("oversized QMP reply");
        }
    }
    void request(const std::string& operation, const std::string& arguments) {
        std::string request = "{\"execute\":" + quote(operation) +
                              ",\"arguments\":" + arguments + "}\n";
        size_t used = 0;
        while (used < request.size()) {
            ssize_t count = send(socket_, request.data() + used,
                                 request.size() - used, MSG_NOSIGNAL);
            if (count <= 0) throw std::runtime_error("QMP send failed");
            used += count;
        }
        while (true) {
            auto reply = message();
            if (reply.find("error"))
                throw std::runtime_error("QMP operation rejected: " +
                                         operation);
            if (reply.find("return")) return;
        }
    }
    static void sleep(unsigned ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    void move(int x, int y) {
        request("input-send-event",
                "{\"events\":[{\"type\":\"rel\",\"data\":{\"axis\":\"x\","
                "\"value\":" +
                    std::to_string(x) +
                    "}},{\"type\":\"rel\",\"data\":{\"axis\":\"y\",\"value\":" +
                    std::to_string(y) + "}}]}");
        sleep(60);
    }
    void click() {
        for (bool down : {true, false}) {
            request("input-send-event",
                    std::string("{\"events\":[{\"type\":\"btn\",\"data\":{"
                                "\"button\":\"left\",\"down\":") +
                        (down ? "true" : "false") + "}}]}");
            sleep(25);
        }
    }
    void exact(int x, int y) {
        while (x || y) {
            auto packet = [](int remaining) {
                int magnitude = std::abs(remaining),
                    raw       = magnitude >= 32  ? std::min(127, magnitude / 4)
                                : magnitude >= 9 ? std::min(7, magnitude / 3)
                                                 : std::min(2, magnitude);
                if (remaining < 0) raw = -raw;
                int scale = std::abs(raw) >= 8 ? 4 : std::abs(raw) >= 3 ? 3 : 1;
                return std::pair{raw, raw * scale};
            };
            auto a = packet(x), b = packet(y);
            move(a.first, b.first);
            x -= a.second;
            y -= b.second;
        }
    }
    void type(const std::string& text) {
        std::map<char, std::string> special = {{' ', "spc"},
                                               {'\n', "ret"},
                                               {'-', "minus"},
                                               {'.', "dot"},
                                               {'/', "slash"}};
        for (char value : text) {
            std::string key = special.count(value) ? special.at(value)
                                                   : std::string(1, value);
            request("send-key", "{\"keys\":[{\"type\":\"qcode\",\"data\":" +
                                    quote(key) + "}],\"hold-time\":25}");
            sleep(70);
        }
    }
    void terminal(unsigned number) {
        request(
            "send-key",
            "{\"keys\":[{\"type\":\"qcode\",\"data\":\"ctrl\"},{\"type\":"
            "\"qcode\",\"data\":\"shift\"},{\"type\":\"qcode\",\"data\":\"f" +
                std::to_string(number) + "\"}],\"hold-time\":25}");
        sleep(1000);
    }
};
void input(const Context& context) {
    auto stage = fs::canonical(context.root);
    if (!fs::exists(stage / ".osinfo-stage"))
        throw std::runtime_error("input requires isolated --root stage");
    auto qmp = stage / "qmp.sock", log = stage / "input-serial.log";
    if (fs::exists(qmp)) fs::remove(qmp);
    const auto image   = context.image.empty() ? stage / "smoke.img"
                                               : fs::canonical(context.image);
    const bool optical = image.extension() == ".iso";
    std::vector<std::string> command = {
        "qemu-system-x86_64",
        "-m",
        "512M",
        "-smp",
        "4",
        "-boot",
        optical ? "order=d,strict=on" : "order=c,strict=on",
        "-display",
        "none",
        "-serial",
        "file:" + log.string(),
        "-monitor",
        "none",
        "-qmp",
        "unix:" + qmp.string() + ",server=on,wait=off",
        "-no-reboot"};
    if (optical)
        command.insert(command.end(), {"-cdrom", image.string()});
    else
        command.insert(
            command.end(),
            {"-drive",
             "file=" + image.string() + ",if=none,format=raw,id=osdisk",
             "-device",
             "virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0"});
    std::vector<char*> argv;
    for (auto& argument : command) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t child = fork();
    if (child < 0) throw std::runtime_error("QEMU fork failed");
    if (!child) {
        execvp(argv[0], argv.data());
        _exit(127);
    }
    try {
        bool ready = false;
        for (unsigned i = 0; i < 500; ++i) {
            int status;
            if (waitpid(child, &status, WNOHANG) == child) {
                child = -1;
                throw std::runtime_error("QEMU exited before input test");
            }
            if (fs::exists(qmp) && fs::exists(log) &&
                read(log).find("[INIT] tty1-tty6 login sessions started") !=
                    std::string::npos) {
                ready = true;
                break;
            }
            Qmp::sleep(100);
        }
        if (!ready) throw std::runtime_error("desktop input startup timeout");
        Qmp control;
        control.connect(qmp);
        Qmp::sleep(2000);
        auto occurrences = [&](const std::string& marker) {
            const auto serial = read(log);
            size_t offset     = 0;
            unsigned count    = 0;
            while ((offset = serial.find(marker, offset)) !=
                   std::string::npos) {
                ++count;
                offset += marker.size();
            }
            return count;
        };
        if (context.pattern == "windows") {
            for (unsigned tabs = 1; tabs <= 4; ++tabs) {
                if (occurrences("[TERMINAL] tab started") >= tabs) continue;
                if (tabs > 1) {
                    control.move(-127, -127);
                    control.move(-127, -127);
                    control.exact(130 + (tabs - 1) * 136 + 18, 105);
                    control.click();
                }
                bool started = false;
                for (unsigned attempt = 0; attempt < 200; ++attempt) {
                    if (occurrences("[TERMINAL] tab started") >= tabs) {
                        started = true;
                        break;
                    }
                    Qmp::sleep(50);
                }
                if (!started)
                    throw std::runtime_error(
                        "four terminal tabs were not ready before input "
                        "stress");
            }
        }
        if (context.pattern == "pressure" &&
            read(log).find("[INPUT PRESSURE] subscriber queue full") ==
                std::string::npos)
            throw std::runtime_error("input pressure client was not armed");
        control.request("screendump",
                        "{\"filename\":" +
                            quote((stage / "input-before.ppm").string()) + "}");
        for (unsigned i = 0; i < context.clicks; ++i) {
            if (context.pattern == "windows") {
                static const int positions[][2] = {
                    {110, 190}, {110, 250}, {600, 80},  {280, 105}, {110, 250},
                    {600, 80},  {420, 105}, {550, 105}, {690, 105}};
                control.move(-127, -127);
                control.move(-127, -127);
                const auto position =
                    i % (sizeof(positions) / sizeof(positions[0]));
                control.exact(positions[position][0], positions[position][1]);
                control.click();
            } else {
                control.move(2, 0);
                control.click();
                control.move(-2, 0);
            }
        }
        control.move(-127, -127);
        control.move(-127, -127);
        control.exact(284, 105);
        control.click();
        Qmp::sleep(2000);
        control.type(
            (context.argument.empty() ? "echo gui alive" : context.argument) +
            "\n");
        if (context.argument == "killall sh") {
            bool restored = false;
            for (unsigned attempt = 0; attempt < 300; ++attempt) {
                const unsigned count = occurrences("[TERMINAL] login restored");
                if (count >= (context.pattern == "windows" ? 4U : 1U)) {
                    restored = true;
                    break;
                }
                Qmp::sleep(100);
            }
            if (!restored)
                throw std::runtime_error(
                    "GUI login recovery did not complete after killall sh");
        }
        Qmp::sleep(2000);
        control.terminal(1);
        control.terminal(7);
        control.request("screendump",
                        "{\"filename\":" +
                            quote((stage / "input-after.ppm").string()) + "}");
        control.request("quit", "{}");
        waitpid(child, nullptr, 0);
        child       = -1;
        auto serial = read(log);
        if (serial.find("[CONSOLE] switched to tty1") == std::string::npos ||
            serial.rfind("[CONSOLE] switched to tty7") <
                serial.find("[CONSOLE] switched to tty1"))
            throw std::runtime_error(
                "keyboard/TTY switching stalled after mouse input");
        for (const auto& marker :
             {"page fault", "unrecoverable kernel exception", "[LD]",
              "[SERVICE] startup/exit failure"})
            if (serial.find(marker) != std::string::npos)
                throw std::runtime_error("guest fault during input test");
        if (read(stage / "input-before.ppm") == read(stage / "input-after.ppm"))
            throw std::runtime_error(
                "desktop did not visually change after input");
        std::cout
            << context.clicks
            << " mouse clicks, new tab and keyboard delivered; screenshots: "
            << stage << '\n';
    } catch (...) {
        if (child > 0) {
            kill(child, SIGTERM);
            waitpid(child, nullptr, 0);
        }
        throw;
    }
}
}  // namespace host
