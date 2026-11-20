"""M0 preflight and host parser baseline. No installs, mounts or VM startup.

Default mode is read-only. --record and --run-host explicitly write only under
tests/output/v0.1.5/M0/. Production builds still require Linux and configure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5/M0"
SOURCES = [
    "src/device/storage/main.cc",
    "include/service/device/storage/boot_image.h",
    "include/service/device/storage/partition.h",
    "include/os-c++/os/storage.h", "lib/os/storage.cc",
    "include/service/fs/block.h", "src/fs/devices.cc",
    "src/kernel/device/resource/domain.cc",
    "src/kernel/device/storage/boot_image.cc",
    "include/service/common/requests.h", "Makefile", "configure",
]

def invoke(arguments):
    result = subprocess.run(arguments, cwd=ROOT, capture_output=True,
                            text=True, errors="replace", timeout=60)
    return {"command": [str(arg) for arg in arguments],
            "exit_code": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("host", "runtime"), default="host")
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--run-host", action="store_true")
    parser.add_argument("--record", action="store_true")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--package-dir", default="packages")
    args = parser.parse_args()
    if args.run_host and args.profile != "host":
        parser.error("--run-host requires --profile host")
    for directory in (args.build_dir, args.package_dir):
        if not (ROOT / directory).resolve().is_relative_to(ROOT):
            parser.error("build and package directories must stay inside repository")
    packages = ROOT / args.package_dir
    checks, commands = [], []
    def check(name, available, detail):
        checks.append({"name": name, "status": "PASS" if available else "BLOCKED",
                       "detail": detail})
    compiler = shutil.which(args.cxx)
    check("host-cxx", compiler is not None, compiler or args.cxx)
    check("python", sys.version_info >= (3, 9), sys.version.split()[0])
    hashes = {}
    for relative in SOURCES:
        source = ROOT / relative
        check(relative, source.is_file(), "required source")
        if source.is_file():
            hashes[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
    for name in ("services", "layout", "checks"):
        path = ROOT / f"script/config/{name}.json"
        try:
            json.loads(path.read_text(encoding="utf-8"))
            check(str(path.relative_to(ROOT)), True, "JSON syntax only")
        except (OSError, ValueError) as error:
            check(str(path.relative_to(ROOT)), False, str(error))
    if args.profile == "runtime":
        check("linux-production-host", platform.system() == "Linux", platform.platform())
        for name in ("make", "gcc", "g++", "ar", "nm", "readelf", "grub-file",
                     "grub-mkrescue", "grub-mkimage", "grub-install", "xorriso",
                     "mkfs.ext3", "mkfs.ext4", "mkfs.fat", "debugfs", "truncate",
                     "sfdisk", "losetup", "partprobe", "blockdev", "mount",
                     "mountpoint", "umount", "sync", "flock", "qemu-system-x86_64"):
            found = shutil.which(name)
            check(name, found is not None, found or "not on PATH")
        startup_tools = [("tools/inpkg", "--help"), ("tools/infoide", "--version")]
        if any(packages.glob("*.zip")) and (ROOT / "tools/unzip").is_file():
            startup_tools.append(("tools/unzip", "-v"))
        for relative, flag in startup_tools:
            path = ROOT / relative
            usable = path.is_file() and os.access(path, os.X_OK) and platform.system() == "Linux"
            check(relative, usable, "requires executable Linux host tool")
            if usable:
                try:
                    result = invoke([str(path), flag])
                    commands.append(result)
                    check(relative + ":startup", result["exit_code"] == 0,
                          result["stderr"].strip() or result["stdout"].strip()
                          or f"exit {result['exit_code']}")
                except (OSError, subprocess.TimeoutExpired) as error:
                    check(relative + ":startup", False, str(error))
        check("package-inputs", any(packages.glob("*.inpk")),
              "presence only; archive validity not yet checked")
        check("configured-build", (ROOT / args.build_dir / "config.mk").is_file(),
              "prepare on Linux using ./configure; configuration freshness not validated")
        for name in ("storage/runtime_baseline.py", "storage/runtime-baseline.cc", "storage/check_sdk_elf.py"):
            check("current-runtime-test:" + name, (ROOT / "tests" / name).is_file(),
                  "presence only; runtime result must be checked separately")
    blocked = any(row["status"] == "BLOCKED" for row in checks)
    status = "BLOCKED" if blocked else "PASS"
    if args.record or args.run_host:
        OUTPUT.mkdir(parents=True, exist_ok=True)
    if args.run_host and not blocked:
        binary = OUTPUT / ("partition-baseline.exe" if os.name == "nt" else "partition-baseline")
        try:
            commands.append(invoke([compiler, "--version"]))
            commands.append(invoke([compiler, "-std=c++20", "-Wall", "-Wextra", "-pedantic",
                "-I", str(ROOT / "include/os-c++"), "-I", str(ROOT / "include"),
                str(ROOT / "tests/storage/partition-baseline.cc"), "-o", str(binary)]))
            if commands[-1]["exit_code"] == 0:
                commands.append(invoke([str(binary)]))
            status = "PASS" if all(item["exit_code"] == 0 for item in commands) else "FAIL"
        except (OSError, subprocess.TimeoutExpired) as error:
            status = "FAIL"
            commands.append({"error": str(error)})
    callers, constants = [], []
    for folder in ("src", "include", "lib/os"):
        for path in sorted((ROOT / folder).rglob("*")):
            if not path.is_file() or path.suffix not in (".h", ".cc"):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            relative = path.relative_to(ROOT).as_posix()
            if re.search(r"Storage::|SectorDevice|StorageRequest|maximumStorageTransferBlocks", text):
                callers.append(relative)
                hashes[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
            if ("/storage/" in relative or relative.startswith("lib/os/storage") or
                    relative.startswith("include/service/fs/") or relative.startswith("src/fs/")):
                for number, line in enumerate(text.splitlines(), 1):
                    if re.search(r"\b512\b|\bblockSize\b|maximumStorageTransferBlocks", line):
                        constants.append({"path": relative, "line": number, "text": line.strip()})
    inputs = []
    for folder in (args.package_dir, "tools"):
        for path in sorted((ROOT / folder).iterdir()):
            if path.is_file() and (path.suffix in (".inpk", ".zip") or not path.suffix):
                inputs.append({"path": path.relative_to(ROOT).as_posix(),
                               "bytes": path.stat().st_size,
                               "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    report = {"profile": args.profile, "status": status,
              "scope": "host partition parser" if args.run_host else "prerequisite checks only",
              "python": sys.version, "platform": platform.platform(),
              "checks": checks, "source_sha256": hashes, "commands": commands,
              "callers": callers, "storage_unit_inventory": constants,
              "binary_package_inputs": inputs,
              "historical_coverage_not_restored": ["kernel memory", "kernel IPC", "lifecycle faults"],
              "not_validated": ["production build", "disk/ISO boot", "DMA/IRQ/stop",
                                "filesystem runtime", "SDK export"]}
    if args.record or args.run_host:
        path = OUTPUT / (args.profile + ("-baseline" if args.run_host else "-preflight") + ".json")
        path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        print("REPORT", path)
    for row in checks:
        if row["status"] != "PASS":
            print(row["status"], row["name"], row["detail"])
    for item in commands:
        if "stdout" in item:
            print(item["stdout"].strip())
        if item.get("exit_code", 0) != 0:
            print(item.get("stderr", item.get("error", "")))
    print("RESULT", status, "profile=" + args.profile)
    return 0 if status == "PASS" else 2 if status == "BLOCKED" else 1

if __name__ == "__main__":
    raise SystemExit(main())
