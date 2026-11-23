"""使用 OSInfo 的真实头文件和转换源码，在宿主机运行边界/内存检查。"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "lib/libc/unicode_runtime.c"
# 隔离被测符号，防止宿主运行库误调用 OSInfo 的不同 mbstate_t 实现。
names = re.findall(r"^(?:int|size_t|wchar_t\*|wint_t) (\w+)\(", source.read_text(), re.M)
renames = [f"-D{name}=osinfo_{name}" for name in names]
flags = ["-std=c11", "-g", "-fno-builtin", "-fsanitize=address,undefined", "-nostdinc"]
for part in ("", "stdc", "posix"):
    flags += ["-I", str(root / "include" / part)]

with tempfile.TemporaryDirectory(prefix="osinfo-b02-unicode-") as folder:
    work = Path(folder)
    subprocess.run(["gcc", *flags, *renames, "-Dstrlen=fixture_strlen", "-c", str(source), "-o", str(work / "unicode.o")], check=True)
    subprocess.run(["gcc", *flags, *renames, str(Path(__file__).with_suffix(".c")), str(work / "unicode.o"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
