"""使用当前构建的 OSInfo 运行库生成专用 OS 回归程序，不安装到发布镜像。"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
work = root / "build/posix-tests"
work.mkdir(parents=True, exist_ok=True)
include = []
for part in ("", "stdc", "posix", "os-c++", "stdc++"):
    include += ["-I", str(root / "include" / part)]
common = ["-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fPIC", "-mno-red-zone", "-nostdinc"]
suites = [("posix-b03-process", "process_test_main"), ("posix-b10-sync", "sync_test_main"),
          ("posix-b04-memory", "memory_test_main"), ("posix-b09-clocks", "clock_test_main"),
          ("posix-b07-streams", "stream_test_main"),
          ("posix-b05-readiness", "readiness_test_main")]
objects = []
for name, entry in suites:
    output = work / f"{name}.o"
    subprocess.run(["gcc", "-std=c11", *common, *include, f"-Dmain={entry}", "-c",
                    str(root / f"script/tests/{name}.c"), "-o", str(output)], check=True)
    objects.append(str(output))
runner = work / "runner.o"
subprocess.run(["g++", "-std=c++20", *common, "-nostdinc++", "-fno-exceptions", "-fno-rtti", *include,
                "-c", str(root / "script/tests/posix-os-runner.cc"), "-o", str(runner)], check=True)
subprocess.run(["g++", "-no-pie", "-nostdlib", "-nostartfiles", "-nodefaultlibs",
                "-Wl,--build-id=none,--hash-style=sysv", f"-Wl,-T,{root}/script/user.lds",
                "-Wl,--dynamic-linker=/lib/ld-osinfo-amd64.so", "-o", str(work / "posix-tests"),
                str(root / "build/obj/lib/os/start.o"), str(runner), *objects, "-Wl,--no-as-needed",
                str(root / "packages/libc++/libc.so"), str(root / "packages/libos-c++/libos-c++.so"), "-lgcc"], check=True)
print(work / "posix-tests")
