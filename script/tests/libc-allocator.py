"""隔离目标分配器符号，使用主机 ASan/UBSan 验证其行为。"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
output = root / "build/header-audit"
output.mkdir(parents=True, exist_ok=True)
object_file = output / "libc-allocator.o"
binary = output / "libc-allocator"
flags = ["-g", "-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
names = ("malloc", "free", "calloc", "realloc", "posix_memalign", "aligned_alloc")
subprocess.run(["gcc", "-std=c11", *flags, "-I", str(root / "include/os-c++"),
    *[f"-D{name}=osinfo_test_{name}" for name in names], "-c",
    str(root / "lib/libc/memory_runtime.c"), "-o", str(object_file)], check=True)
subprocess.run(["g++", "-std=c++20", *flags, "-pthread", str(object_file),
    str(Path(__file__).with_suffix(".cc")), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print("libc-allocator PASS", flush=True)
