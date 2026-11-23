"""验证启动对象、图形会话连接与回复解析。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sources = ["script/tests/service-objects.cc", "lib/os/service/boot.cc",
           "lib/os/service/session.cc", "src/console/boot.cc",
           "lib/os/service/runtime.cc", "lib/os/service/identity.cc"]
with tempfile.TemporaryDirectory(prefix="osinfo-service-objects-") as folder:
    binary = Path(folder) / "service-objects"
    subprocess.run(["g++", "-std=c++20", "-fsanitize=address,undefined",
                    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                    "-Datexit=osinfo_test_atexit", "-I", str(root / "include"),
                    "-I", str(root / "include/os-c++"),
                    *[str(root / source) for source in sources],
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("service objects passed")
