"""Build disposable M0 images and run SDK/storage/FS regressions on Linux.

Use through linux_m0_env.sh after make all check sdk. No mounts, root access,
host block devices or production source modifications. A fresh output is required.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from check_sdk_elf import check as check_sdk_elf

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5/M0"


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def boot_arguments(stage, mode, smp, extra_4kn):
    arguments = ["qemu-system-x86_64", "-accel", "tcg,thread=single", "-m", "512M",
                 "-smp", str(smp), "-display", "none", "-serial", "stdio",
                 "-monitor", "none", "-no-reboot", "-nic", "none"]
    if mode.startswith("iso"):
        return arguments + ["-boot", "order=d,strict=on", "-cdrom", str(stage / "osinfo.iso")]
    arguments += ["-boot", "order=c,strict=on", "-drive",
                  f"file={stage}/osinfo.img,if=none,format=raw,id=osdisk",
                  "-device", "virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0"]
    if extra_4kn:
        arguments += ["-drive", f"file={stage}/4kn.img,if=none,format=raw,id=disk4kn,readonly=on",
                      "-device", "virtio-blk-pci,drive=disk4kn,disable-legacy=on,"
                                 "logical_block_size=4096,physical_block_size=4096"]
    return arguments


def baseline_id(report):
    inputs = report.get("inputs_sha256", {})
    if not {"osinfo.img", "storage-probe"}.issubset(inputs):
        raise ValueError("baseline has no prepared image fingerprints")
    return hashlib.sha256(json.dumps(inputs, sort_keys=True).encode("ascii")).hexdigest()


def verify_boot_profile(report, stage, profile):
    saved = report.get("boot_profile")
    boots = report["boots"]
    if saved is None and not boots:
        raise ValueError("old report has no boot commands to verify; prepare a fresh baseline")
    if saved is not None and saved != profile:
        raise ValueError("boot configuration differs; prepare a fresh baseline")
    if profile["extra_4kn"] != (stage / "4kn.img").is_file():
        raise ValueError("4Kn fixture differs; prepare a fresh baseline")
    identity = baseline_id(report)
    if report.get("baseline_id", identity) != identity:
        raise ValueError("baseline fingerprints differ; prepare a fresh baseline")
    for case in boots:
        mode = case["mode"]
        if (mode not in profile["modes"] or
                case.get("baseline_id", identity) != identity or
                case.get("argv") != boot_arguments(stage, mode, profile["smp"], profile["extra_4kn"])):
            raise ValueError("stored boot case has a different configuration; prepare a fresh baseline")
    report["boot_profile"] = profile
    report["baseline_id"] = identity
    for case in boots:
        case["baseline_id"] = identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build-m0-final")
    parser.add_argument("--packages", default="packages-m0-final")
    parser.add_argument("--name", default="runtime-final")
    parser.add_argument("--cxx", default="x86_64-linux-gnu-g++-14")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--smp", type=int, choices=(1, 2), default=2)
    parser.add_argument("--version", help="require this release in image and running guest")
    parser.add_argument("--case", action="append",
                        help="run only named cases against already prepared images")
    parser.add_argument("--disk-only", action="store_true",
                        help="prepare or run the hard-disk cases without generating an ISO")
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--normal", action="store_true", help="keep original login sessions; check ordinary boot")
    parser.add_argument("--extra-4kn", action="store_true",
                        help="attach a disposable 16 MiB 4Kn disk to hard disk boots")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error("seconds must be between 10 and 600")
    if not args.prepare and not args.run:
        parser.error("select --prepare and/or --run")
    if args.version and not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("version must use numeric major.minor.patch")
    modes = ("disk-normal", "iso-normal") if args.normal else ("disk-create", "disk-persist", "iso-create")
    if args.disk_only:
        modes = tuple(mode for mode in modes if mode.startswith("disk-"))
    profile = {"version": args.version, "modes": list(modes), "smp": args.smp,
               "media": "disk" if args.disk_only else "disk-and-iso",
               "extra_4kn": args.extra_4kn}
    if args.case and (args.prepare or not args.run or set(args.case) - set(modes)):
        parser.error("--case selects existing images and requires --run")
    if Path(args.name).name != args.name or args.name in {"", ".", ".."}:
        parser.error("name must be a single directory name")
    for name in (args.build, args.packages):
        if not (ROOT / name).resolve().is_relative_to(ROOT):
            parser.error("build and packages must be inside repository")
    stage = OUTPUT / args.name
    if args.prepare:
        stage.mkdir(exist_ok=False)
    elif not stage.is_dir():
        parser.error("prepare this output first")
    if args.prepare:
        report = {"status": "RUNNING", "commands": [], "boots": [], "boot_profile": profile}
        if args.disk_only:
            report["disk_only"] = True
    else:
        report = json.loads((stage / "result.json").read_text())
        if args.version != report.get("version") or args.disk_only != report.get("disk_only", False):
            parser.error("version and selected media must match the prepared images")
        try:
            verify_boot_profile(report, stage, profile)
        except ValueError as error:
            parser.error(str(error))
        report["status"] = "RUNNING"
        report.pop("error", None)
    commands = report["commands"]

    def save():
        (stage / "result.json").write_text(json.dumps(report, indent=2))

    def run(arguments, input=None):
        arguments = [str(a) for a in arguments]
        index = len(commands)
        print("RUN", arguments[0], flush=True)
        with (stage / f"command-{index:02}.log").open("w") as log:
            result = subprocess.run(arguments, cwd=ROOT, input=input, text=True,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=300)
        commands.append({"argv": arguments, "exit_code": result.returncode,
                         "log": f"command-{index:02}.log"})
        save()
        if result.returncode:
            raise RuntimeError(f"command failed: {arguments}; see command-{index:02}.log")

    try:
        if args.prepare:
            if args.extra_4kn:
                with (stage / "4kn.img").open("wb") as stream:
                    stream.truncate(16 * 1024 * 1024)
            sdk = ROOT / args.build / "sdk"
            metadata = json.loads((sdk / "sdk.json").read_text())
            for name, expected in metadata["files"].items():
                if not (sdk / name).resolve().is_relative_to(sdk.resolve()) or digest(sdk / name) != expected:
                    raise RuntimeError("SDK digest mismatch: " + name)
            storage_header = digest(ROOT / "include/os-c++/os/storage.h")
            if storage_header != digest(ROOT / "packages/libos-c++/include/os-c++/os/storage.h"):
                raise RuntimeError("package storage header differs from production")
            if storage_header != digest(sdk / "include/os-c++/os/storage.h"):
                raise RuntimeError("SDK storage header differs from production")
            report["sdk_files_verified"] = len(metadata["files"])
            linker = sdk / "lib/user.lds"
            flags = ["-std=c++20", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
                     "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
                     "-fPIC", "-m64", "-mno-red-zone", "-nostdinc", "-nostdinc++", "-Os"]
            for directory in ("stdc", "stdc++", "posix", "os-c++", ""):
                flags += ["-I", str(sdk / "include" / directory)]
            if args.version:
                flags += [f'-DOSINFO_EXPECTED_RELEASE="{args.version}"']
            run([args.cxx, *flags, "-no-pie", "-nostdlib", "-nostartfiles", "-nodefaultlibs",
                 "-Wl,--build-id=none,--hash-style=sysv", "-Wl,-T," + str(linker),
                 "-Wl,--dynamic-linker=/lib/ld-osinfo-x86-64.so", sdk / "lib/start.o",
                 ROOT / "tests/storage/runtime-baseline.cc", "-L", sdk / "lib", "-Wl,--no-as-needed",
                 "-l:libc++.so", "-l:libc.so", "-l:libos-c++.so", "-lgcc", "-o", stage / "storage-probe"])
            report["sdk_executable"] = check_sdk_elf(stage / "storage-probe")
            root = stage / "root"
            run(["sh", "script/install.sh", root, args.build, args.packages, "0"])
            if args.version:
                installed = (root / "lib/version.1").read_text().strip()
                if installed != args.version:
                    raise RuntimeError("installed release differs: " + installed)
                report["version"] = installed
            if not args.normal:
                shutil.copy2(stage / "storage-probe", root / "usr/bin/m0-storage-probe")
                sessions = root / "etc/sessions"
                sessions.write_text(sessions.read_text().replace("login|/bin/login|login", "login|/usr/bin/m0-storage-probe|m0-storage-probe"))
            (root / "etc/fstab").write_text("LABEL=OSINFO_ROOT / ext3 defaults 0 1\n")
            filesystem = stage / "root.ext3"
            with filesystem.open("wb") as stream:
                stream.truncate(128 * 1024 * 1024)
            run(["mkfs.ext3", "-q", "-F", "-b", "4096", "-L", "OSINFO_ROOT", "-d", root, filesystem])
            run(["tools/osinfo-host", "layout", "--root", root, "--image", filesystem])
            (stage / "early.cfg").write_text("set root=(hd0,msdos1)\nset prefix=($root)/boot/grub\n")
            run(["grub-mkimage", "-O", "i386-pc", "-p", "/boot/grub", "-c", stage / "early.cfg",
                 "-o", stage / "core.img", "biosdisk", "part_msdos", "ext2", "normal", "multiboot2",
                 "configfile", "all_video", "gfxterm", "font"])
            shutil.copy2(OUTPUT / "linux-deps/sysroot/usr/lib/grub/i386-pc/boot.img", stage / "boot.img")
            disk = stage / "osinfo.img"
            with disk.open("wb") as stream:
                stream.truncate(160 * 1024 * 1024)
            run(["sfdisk", disk], "label: dos\nunit: sectors\n\n2048,262144,83,*\n")
            with disk.open("r+b") as target, filesystem.open("rb") as source:
                target.seek(1024 * 1024)
                shutil.copyfileobj(source, target)
            run(["tools/osinfo-host", "image", "--argument", "embed-grub", "--output", stage, "--image", disk])
            images = ["osinfo.img", "storage-probe"]
            if not args.disk_only:
                cd = stage / "cd"
                shutil.copytree(root / "boot", cd / "boot")
                shutil.copy2(filesystem, cd / "boot/rootfs.ext3")
                config = (ROOT / args.build / "generated/grub.cfg").read_text()
                lines = []
                for line in config.splitlines():
                    if line.strip() == "boot":
                        lines.append("    module2 /boot/rootfs.ext3 rootfs")
                    lines.append(line)
                (cd / "boot/grub/grub.cfg").write_text("\n".join(lines) + "\n")
                run(["grub-mkrescue", "-o", stage / "osinfo.iso", cd])
                images.append("osinfo.iso")
            report["inputs_sha256"] = {name: digest(stage / name) for name in images}
            if args.extra_4kn:
                report["inputs_sha256"]["4kn.img"] = digest(stage / "4kn.img")
            report["baseline_id"] = baseline_id(report)
        if args.run:
            for mode in modes:
                if args.case and mode not in args.case:
                    continue
                log = stage / (mode + ".log")
                previous = next((case for case in report["boots"] if case["mode"] == mode), None)
                if previous is not None:
                    attempts = report.setdefault("previous_attempts", [])
                    archived = f"{mode}.attempt-{len(attempts) + 1}.log"
                    shutil.copy2(log, stage / archived)
                    attempts.append({**previous, "log": archived})
                    report["boots"].remove(previous)
                    save()
                arguments = boot_arguments(stage, mode, args.smp, args.extra_4kn)
                expected = ["[M0 STORAGE] PASS", "[M0 STORAGE] BACKEND " + ("BootImage" if mode.startswith("iso") else "VirtioBlock"),
                            "[M0 STORAGE] FILE " + ("persisted" if mode == "disk-persist" else "created")]
                if args.normal:
                    expected = ["[INIT] tty1-tty6 login sessions started",
                                "[INIT] boot display complete; console TTY switching enabled",
                                "[SESSION] graphical greeter ready"]
                elif args.extra_4kn and mode.startswith("disk"):
                    expected.append("[M0 STORAGE] 4Kn identified, I/O rejected")
                if args.version and not args.normal:
                    expected.append(f"[RELEASE] {args.version} verified")
                with log.open("w") as stream:
                    process = subprocess.Popen(arguments, cwd=ROOT, stdin=subprocess.DEVNULL,
                                               stdout=stream, stderr=subprocess.STDOUT)
                    started = time.monotonic()
                    deadline = started + args.seconds
                    try:
                        while process.poll() is None and time.monotonic() < deadline:
                            text = log.read_text(errors="replace")
                            if all(marker in text for marker in expected) or "[M0 STORAGE] FAIL" in text:
                                break
                            time.sleep(1)
                    finally:
                        if process.poll() is None:
                            process.terminate()
                        try:
                            process.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                text = log.read_text(errors="replace")
                passed = all(marker in text for marker in expected) and not any(marker in text for marker in ("[M0 STORAGE] FAIL", "unrecoverable kernel exception"))
                report["boots"].append(
                    {"mode": mode, "argv": arguments, "status": "PASS" if passed else "FAIL",
                     "baseline_id": report["baseline_id"], "host_wait_seconds": args.seconds,
                     "duration_seconds": round(time.monotonic() - started, 2),
                     "exit_code": process.returncode})
                save()
                if not passed:
                    raise RuntimeError("boot failed: " + mode)
                print("PASS", mode, flush=True)
        passed_modes = {case["mode"] for case in report["boots"] if case["status"] == "PASS"}
        if args.run and any(case["status"] == "FAIL" for case in report["boots"]):
            report["status"] = "FAIL"
        else:
            report["status"] = "PASS" if args.run and passed_modes == set(modes) else "PREPARED"
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report["status"] = "FAIL"
        report["error"] = str(error)
        print("FAIL", error, flush=True)
    save()
    return 0 if report["status"] in {"PASS", "PREPARED"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
