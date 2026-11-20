"""Exercise two writers and unmount/remount on a disposable VirtIO data disk."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="passing runtime_baseline.py output directory")
    parser.add_argument("tag")
    parser.add_argument("--seconds", type=int, default=300,
                        help="host wait budget for boot and both writers")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error("seconds must be between 10 and 600")
    baseline = args.baseline.resolve()
    if not baseline.is_relative_to(OUTPUT) or Path(args.tag).name != args.tag or args.tag in {".", ".."}:
        parser.error("use a local test output baseline and a fresh output tag")
    report = json.loads((baseline / "result.json").read_text())
    if report["status"] != "PASS":
        parser.error("requires a passing baseline")
    out = OUTPUT / "release-fs" / args.tag
    out.mkdir(parents=True, exist_ok=False)
    results = []

    def command(name, argv):
        argv = [str(arg) for arg in argv]
        with (out / (name + ".log")).open("w") as log:
            result = subprocess.run(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=180)
        results.append({"name": name, "argv": argv, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(f"{name} failed; see {out / (name + '.log')}")

    try:
        argv = list(report["commands"][0]["argv"])
        sources = [i for i, arg in enumerate(argv) if Path(arg).name == "runtime-baseline.cc"]
        if (len(sources) != 1 or argv[-2] != "-o" or
                Path(argv[-1]).name != "storage-probe" or not argv[0].endswith("g++-14")):
            raise RuntimeError("unexpected SDK compiler record")
        argv[sources[0]] = str(ROOT / "tests/storage/release-filesystem.cc")
        argv[-1] = str(out / "probe")
        command("sdk", argv)
        root = out / "root"
        shutil.copytree(baseline / "root", root, symlinks=True)
        shutil.copy2(out / "probe", root / "usr/bin/m0-storage-probe")
        (root / "mnt/storage-gate").mkdir(parents=True)
        cd = out / "cd"
        shutil.copytree(baseline / "cd", cd, ignore=shutil.ignore_patterns("rootfs.ext3"))
        root_image = cd / "boot/rootfs.ext3"
        data_image = out / "aux.ext3"
        for image, length in ((root_image, 64), (data_image, 32)):
            with image.open("wb") as stream:
                stream.truncate(length * 1024 * 1024)
        command("rootfs", ["mkfs.ext3", "-q", "-F", "-b", "4096",
                           "-L", "OSINFO_ROOT", "-d", root, root_image])
        command("layout", ["tools/osinfo-host", "layout", "--root", root, "--image", root_image])
        command("datafs", ["mkfs.ext3", "-q", "-F", "-b", "4096",
                           "-L", "OSINFO_GATE", data_image])
        iso = out / "gate.iso"
        command("iso", ["grub-mkrescue", "-o", iso, cd])
        argv = ["qemu-system-x86_64", "-accel", "tcg,thread=single", "-m", "512M",
                "-smp", "2", "-display", "none", "-serial", "stdio", "-monitor", "none",
                "-no-reboot", "-nic", "none", "-boot", "order=d,strict=on",
                "-cdrom", str(iso), "-drive", f"file={data_image},if=none,format=raw,id=gate",
                "-device", "virtio-blk-pci,drive=gate,disable-legacy=on"]
        serial = out / "serial.log"
        with serial.open("w") as log:
            process = subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.seconds
                while process.poll() is None and time.monotonic() < deadline:
                    text = serial.read_text(errors="replace")
                    if "[RELEASE FS] PASS" in text or "[RELEASE FS] FAIL" in text:
                        break
                    time.sleep(0.5)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        results.append({"name": "qemu", "argv": argv, "exit_code": process.returncode})
        text = serial.read_text(errors="replace")
        if "[RELEASE FS] PASS" not in text or "[RELEASE FS] FAIL" in text:
            raise RuntimeError(f"filesystem gate failed; see {serial}")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        (out / "result.json").write_text(json.dumps(
            {"status": "FAIL", "commands": results, "error": str(error)}, indent=2))
        print(error)
        return 1
    (out / "result.json").write_text(json.dumps({"status": "PASS", "commands": results}, indent=2))
    print("PASS independent mount, concurrent writes, unmount and remount")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
