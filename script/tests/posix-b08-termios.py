"""使用真实 OSInfo 头文件验证终端属性和服务错误转换。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
names = ["cfgetispeed", "cfgetospeed", "cfsetispeed", "cfsetospeed", "cfsetspeed",
         "tcgetwinsize", "tcsetwinsize", "posix_devctl"]
flags = ["-std=c11", "-g", "-fno-builtin", "-fsanitize=address,undefined", "-nostdinc"]
for part in ("", "stdc", "posix"):
    flags += ["-I", str(root / "include" / part)]
flags += [f"-D{name}=osinfo_{name}" for name in names]
with tempfile.TemporaryDirectory(prefix="osinfo-b08-termios-") as folder:
    executable = str(Path(folder) / "test")
    subprocess.run(["gcc", *flags, str(root / "lib/libc/termios_runtime.c"),
                    str(Path(__file__).with_suffix(".c")), "-o", executable], check=True)
    subprocess.run([executable], check=True)
