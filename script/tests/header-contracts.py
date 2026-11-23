"""对本轮头文件与类职责调整运行实际实例化、所有权和解析测试。"""
from pathlib import Path
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
output = root / "build/header-audit"
output.mkdir(parents=True, exist_ok=True)
tests = [
    ("mm-page-flags", ["script/tests/mm-page-flags.cc", "src/mm/state.cc"],
     ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"]),
    ("elf-sections", ["script/tests/elf-sections.cc", "lib/os/elf.cc", "lib/os/service/runtime.cc"],
     ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Datexit=osinfo_test_atexit"]),
    ("console-clear", ["script/tests/console-clear.cc", "lib/os/console.cc", "lib/os/service/runtime.cc"],
     ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Datexit=osinfo_test_atexit"]),
    ("display-frame", ["script/tests/display-frame.cc", "../guiw/server/display.cc"],
     ["-I", str(root.parent/'guiw/include'), "-I", str(root.parent/'guiw/lib/osgui-c++/include'),
      "-I", str(root.parent/'guiw/lib/guiw-font/include')]),
    ("storage-queue", ["script/tests/storage-queue.cc", "src/device/storage/request_queue.cc",
                       "src/device/storage/block_backend.cc"], []),
    ("atomic-ownership", ["script/tests/header-contracts.cc"], ["-pthread"]),
    ("texture-ownership", ["script/tests/texture-ownership.cc", "../guiw/server/texture-pixels.cc"],
     ["-I", str(root.parent / "guiw/include")]),
    ("ata-identity", ["script/tests/ata-identity.cc", "src/device/storage/ata.cc"], []),
    ("configuration-text", ["script/tests/configuration-text.cc", "lib/os/service/configuration.cc"], []),
    ("password-record", ["script/tests/password-record.cc", "src/users/password.cc"], []),
    ("memory-models", ["script/tests/memory-models.cc", "src/mm/models.cc", "src/mm/region.cc"], []),
    ("service-objects", ["script/tests/service-objects.cc", "lib/os/service/boot.cc",
                         "lib/os/service/session.cc", "src/console/boot.cc", "lib/os/service/runtime.cc",
                         "lib/os/service/identity.cc"],
     ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Datexit=osinfo_test_atexit"]),
    ("storage-contracts", ["script/tests/storage-contracts.cc", "src/device/storage/block_backend.cc"], []),
    ("partition-reader", ["script/tests/partition-reader.cc", "src/device/storage/partition.cc",
                          "lib/os/storage.cc", "lib/os/service/runtime.cc"],
     ["-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-Datexit=osinfo_test_atexit"]),
]
results = []
subprocess.run([sys.executable, str(root / "script/tests/libc-allocator.py")], check=True)
results.append({"name": "libc-allocator", "passed": True})
for name, sources, flags in tests:
    binary = output / name
    subprocess.run(["g++", "-std=c++20", "-g", "-Wall", "-Wextra", "-Werror=return-type",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(root / "include"), "-I", str(root / "include/os-c++"),
                    *flags, *[str(root / source) for source in sources], "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    results.append({"name": name, "passed": True})
    print(name + " PASS", flush=True)
(output / "contracts.json").write_text(json.dumps(results, indent=2))
