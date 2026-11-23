"""在 OSInfo 真实 FILE 布局上验证内存流、缓冲、递归流锁和所有权。"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = [root / f"lib/libc/{name}" for name in ["stdio_stream.c", "stdio_extra.c", "wide_stdio.c",
    "portable/stdio/wide_format.c", "portable/stdio/wide_scan.c", "portable/stdio/scan.c",
    "portable/internal/intscan.c", "portable/internal/floatscan.c"]]
names = set(re.findall(r"^(?:FILE\*|int|void|off_t|size_t|ssize_t|wint_t|wchar_t\*|char\*) (\w+)\(",
    "\n".join(source.read_text(encoding="utf-8") for source in sources), re.M))
names = [name for name in names if not name.startswith("__libc_")]
flags = ["-std=c11", "-g", "-fno-builtin", "-fsanitize=address,undefined", "-nostdinc"]
for part in ("", "stdc", "posix", "os-c++"):
    flags += ["-I", str(root / "include" / part)]
flags += [f"-D{name}=osinfo_{name}" for name in names]
flags += ["-I", str(root / "lib/libc/portable/internal")]
flags += ["-include", str(root / "lib/libc/portable/internal/compat.h")]
with tempfile.TemporaryDirectory(prefix="osinfo-b07-streams-") as folder:
    work = Path(folder)
    namespace = work / "namespace.h"
    namespace.write_text("#include <stdio.h>\n" + "".join(
        f"#undef {name}\n#define {name} osinfo_{name}\nextern FILE *osinfo_{name};\n"
        for name in ["stdin", "stdout", "stderr"]))
    flags += ["-include", str(namespace)]
    subprocess.run(["gcc", *flags, "-DOSINFO_HOST_FIXTURE", *(str(source) for source in sources),
                    str(Path(__file__).with_suffix(".c")), "-lm", "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
