"""用真实目录/符号链接对照宿主 realpath，并检查遍历顺序及 cwd 恢复。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="osinfo-b06-paths-") as folder:
    work = Path(folder)
    flags = ["-std=c11", "-D_XOPEN_SOURCE=700", "-g", "-fsanitize=address,undefined"]
    for name, function in [("path_runtime", "realpath"), ("tree_walk", "nftw")]:
        subprocess.run(["gcc", *flags, f"-D{function}=osinfo_{function}", "-c",
                        str(root / f"lib/libc/{name}.c"), "-o", str(work / f"{name}.o")], check=True)
    subprocess.run(["gcc", *flags, str(Path(__file__).with_suffix(".c")),
                    str(work / "path_runtime.o"), str(work / "tree_walk.o"),
                    "-o", str(work / "test")], check=True)
    (work / "tree-root").mkdir()
    subprocess.run([str(work / "test"), str(work / "tree-root")], check=True)
