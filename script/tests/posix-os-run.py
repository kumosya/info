"""在一次性 CD 镜像运行 POSIX 回归，并通过 QMP 注入 tty1 就绪测试输入。"""
from pathlib import Path
import json
import re
import shutil
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix="osinfo-posix-"))
logdir = root / "build/posix-tests"
logdir.mkdir(exist_ok=True)
serial = work / "serial.log"
print(f"isolated stage: {work}", flush=True)
stage = work / "root"
subprocess.run(["sh", "script/install.sh", str(stage), "build", "packages", "0"], cwd=root, check=True,
               stdout=(work / "install.log").open("w"), stderr=subprocess.STDOUT)
shutil.copy2(root / "build/posix-tests/posix-tests", stage / "bin/posix-tests")
(stage / "bin/posix-tests").chmod(0o755)
(stage / "etc/sessions").write_text("terminal|/usr/bin/terminal|terminal\nlogin|/bin/posix-tests|/bin/posix-tests\ngraphical|/usr/bin/guiw-session|guiw-session\n")
(stage / "etc/console").write_text("defaultTerminal=tty1\n")
(stage / "etc/fstab").write_text("LABEL=OSINFO_ROOT / ext3 defaults 0 1\n")
cd = work / "cd"
shutil.copytree(stage / "boot", cd / "boot")
image = cd / "boot/rootfs.ext3"
# 开发工具包包含较大的编译前端，按已安装内容为文件系统留出空间。
installed_kib = int(subprocess.check_output(["du", "-sk", str(stage)], text=True).split()[0])
image_mib = max(128, ((installed_kib + 65536 + 65535) // 65536) * 64)
with image.open("wb") as output:
    output.truncate(image_mib * 1024 * 1024)
subprocess.run(["mkfs.ext3", "-q", "-F", "-b", "4096", "-L", "OSINFO_ROOT", "-d", str(stage), str(image)], check=True)
subprocess.run([str(root / "tools/osinfo-host"), "layout", "--root", str(stage), "--image", str(image)], cwd=root, check=True,
               stdout=(work / "layout.log").open("w"), stderr=subprocess.STDOUT)
subprocess.run(["debugfs", "-w", "-R", "set_inode_field /bin/posix-tests mode 0100755", str(image)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
config = (root / "build/generated/grub.cfg").read_text()
config = re.sub(r"(?m)^\s*boot$", "    module2 /boot/rootfs.ext3 rootfs\n    boot", config)
(cd / "boot/grub/grub.cfg").write_text(config)
iso = work / "posix.iso"
subprocess.run(["grub-mkrescue", "-o", str(iso), str(cd)], check=True,
               stdout=(work / "grub.log").open("w"), stderr=subprocess.STDOUT)
qmp_path = work / "qmp.sock"
command = ["qemu-system-x86_64", "-m", "2048", "-smp", "8", "-vga", "std", "-display", "none",
           "-cdrom", str(iso), "-boot", "d", "-serial", f"file:{serial}", "-qmp", f"unix:{qmp_path},server=on,wait=off",
           "-no-reboot", "-no-shutdown"]
process = subprocess.Popen(command, cwd=work, stdout=(work / "qemu.log").open("w"), stderr=subprocess.STDOUT)
try:
    deadline = time.monotonic() + 240
    injected = False
    last = ""
    while time.monotonic() < deadline:
        text = serial.read_text(errors="replace") if serial.exists() else ""
        lines = "\n".join(line for line in text.splitlines() if "[POSIX]" in line)
        if lines != last:
            print(lines[len(last):], flush=True)
            last = lines
        if "[POSIX] B05 CONSOLE WAIT" in text and "boot display complete" in text and not injected:
            with socket.socket(socket.AF_UNIX) as connection:
                connection.connect(str(qmp_path))
                stream = connection.makefile("rwb", buffering=0)
                json.loads(stream.readline())
                def execute(name, arguments=None):
                    stream.write((json.dumps({"execute": name, "arguments": arguments or {}}) + "\n").encode())
                    while True:
                        reply = json.loads(stream.readline())
                        if "error" in reply: raise RuntimeError(reply)
                        if "return" in reply: return reply["return"]
                execute("qmp_capabilities")
                execute("human-monitor-command", {"command-line": "sendkey ctrl-shift-f1"})
                switch_deadline = time.monotonic() + 5
                while time.monotonic() < switch_deadline:
                    switches = re.findall(r"\[CONSOLE\] switched to tty(\d+)", serial.read_text(errors="replace"))
                    if switches and switches[-1] == "1":
                        break
                    time.sleep(0.05)
                else:
                    raise RuntimeError("console input test could not select tty1")
                time.sleep(0.2)
                execute("send-key", {"keys": [{"type": "qcode", "data": "q"}], "hold-time": 100})
                time.sleep(0.2)
                execute("send-key", {"keys": [{"type": "qcode", "data": "ret"}], "hold-time": 100})
            injected = True
        if re.search(r"\[POSIX\] B05 CONSOLE RESULT \d+.*\n", text):
            if re.search(r"RESULT (?!0\b)\d+", lines):
                raise RuntimeError("OS suite failed; see " + str(serial))
            print("All OS suites passed; serial log: " + str(serial), flush=True)
            break
        if process.poll() is not None:
            raise RuntimeError("QEMU exited: " + (work / "qemu.log").read_text())
        time.sleep(0.2)
    else:
        raise TimeoutError("OS suites did not finish; see " + str(serial))
finally:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)

shutil.copy2(serial, logdir / "serial.log")
