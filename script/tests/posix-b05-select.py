"""在真实 OSInfo 头文件上验证 poll/select 包装；等待后端为确定性替身。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
flags = ["-std=c11", "-g", "-fno-builtin", "-fsanitize=address,undefined", "-nostdinc"]
for part in ("", "stdc", "posix", "os-c++"):
    flags += ["-I", str(root / "include" / part)]
flags += ["-I", str(root / "lib/libc")]
flags += [f"-D{name}=osinfo_{name}" for name in ("poll", "ppoll", "select", "pselect")]
with tempfile.TemporaryDirectory(prefix="osinfo-b05-select-") as folder:
    executable = str(Path(folder) / "test")
    subprocess.run(["gcc", *flags, str(root / "lib/libc/poll_runtime.c"),
                    str(root / "lib/libc/select_runtime.c"),
                    str(Path(__file__).with_suffix(".c")), "-o", executable], check=True)
    subprocess.run([executable], check=True)
print("B05 poll/select wrapper checks passed (ASan/UBSan)")
