"""Build-only fault instrumentation and a disposable QEMU lifecycle lab."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


FIXTURES = Path(__file__).resolve().parent


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError(f"expected one instrumentation anchor: {before!r}")
    return text.replace(before, after, 1)


def instrument(repo):
    kernel = repo / "src/kernel/device/resource/domain.cc"
    storage = repo / "src/device/storage/main.cc"
    files = {path: path.read_text() for path in (kernel, storage)}
    report = {str(path.relative_to(repo)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in files}
    kernel_source = replace_once(files[kernel], "    *status_ = 0;\n",
                                 (FIXTURES / "lifecycle-lab-reset.inc").read_text() +
                                 "    *status_ = 0;\n")
    claim_anchor = "            if (domains[i].state_ == State::Quarantined &&"
    claim_probe = r'''            auto& previous = domains[i];
            if (function.bus == 0 && function.device == 8 &&
                previous.state_ == State::Quarantined && !previous.exiting_) {
                bool retained = previous.references_ == 0 && previous.dmaPages_ == 8;
                for (auto* buffer = previous.dma_; buffer && retained;
                     buffer = buffer->next_) {
                    mm::PhysicalPageInfo frame;
                    retained = mm::queryPhysicalPage(buffer->physicalAddress_, frame) &&
                               frame.getState() == mm::FrameState::Allocated &&
                               frame.getOwner() == previous.owner_->id();
                }
                device::serialWrite(retained
                    ? "[M5 LAB] quarantine retained physical DMA frames\n"
                    : "[M5 LAB] FAIL quarantine frame retention\n");
            }
'''
    kernel_source = replace_once(kernel_source, claim_anchor, claim_probe + claim_anchor)
    reclaim_anchor = "        ::kernel::task::Task* owner = pending->owner_;"
    reclaim_probe = r'''        if (pending->function_.bus == 0 && pending->function_.device == 10) {
            device::serialWrite(pending->exiting_ && pending->addressSpaceGone_
                ? "[M5 LAB] active domain released after task address space\n"
                : "[M5 LAB] FAIL active domain reclaimed before task exit\n");
        }
'''
    kernel_source = replace_once(kernel_source, reclaim_anchor, reclaim_probe + reclaim_anchor)
    storage_source = replace_once(files[storage], "    int startup();",
                                  (FIXTURES / "lifecycle-lab-storage.inc").read_text() +
                                  "    int startup();")
    startup_anchor = '    os::Sys().log("[STORAGE] boot image / VirtIO storage online\\n");'
    storage_source = replace_once(storage_source, startup_anchor, startup_anchor +
                                  "\n    const int result = lifecycleLab();\n"
                                  "    if (result) return result;")
    kernel.write_text(kernel_source)
    storage.write_text(storage_source)
    print(json.dumps(report))


def run(repo, build, baseline, output):
    output.mkdir(parents=True, exist_ok=False)
    cd = output / "cd"
    root = output / "root"
    shutil.copytree(baseline / "root", root, symlinks=True)
    sessions = root / "etc/sessions"
    sessions.write_text(replace_once(sessions.read_text(),
                                    "login|/usr/bin/m0-storage-probe|m0-storage-probe",
                                    "login|/bin/login|login"))
    shutil.copytree(baseline / "cd", cd, ignore=shutil.ignore_patterns("rootfs.ext3"))
    boot = cd / "boot"
    shutil.copy2(build / "kernel.elf", boot / "kernel.elf")
    shutil.copy2(build / "services/storage.elf", boot / "storage.elf")
    fs = boot / "rootfs.ext3"
    with fs.open("wb") as image:
        image.truncate(64 * 1024 * 1024)
    subprocess.run(["mkfs.ext3", "-q", "-F", "-b", "4096", "-L", "OSINFO_ROOT",
                    "-d", root, fs], cwd=repo, check=True)
    subprocess.run([repo / "tools/osinfo-host", "layout", "--build", build,
                    "--root", root, "--image", fs], cwd=repo, check=True)
    iso = output / "lifecycle.iso"
    subprocess.run(["grub-mkrescue", "-o", iso, cd], cwd=repo, check=True)
    command = ["qemu-system-x86_64", "-accel", "tcg,thread=single", "-m", "512M",
               "-smp", "2", "-display", "none", "-serial", "stdio", "-monitor",
               "none", "-no-reboot", "-nic", "none", "-boot", "order=d,strict=on",
               "-cdrom", str(iso)]
    for slot in (8, 9, 10):
        disk = output / f"slot{slot}.img"
        with disk.open("wb") as stream:
            stream.truncate((slot + 4) * 1024 * 1024)
        command += ["-drive", f"file={disk},if=none,format=raw,id=slot{slot}",
                    "-device", f"virtio-blk-pci,drive=slot{slot},disable-legacy=on,addr=0x{slot:x}"]

    required = ("injected reset failure", "quarantine retained physical DMA frames",
                "quarantine rejected claim from live owner",
                "stop wait returned unconfirmed", "pending stop retained physical DMA frames",
                "original storage task exiting", "active domain released after task address space",
                "replacement owner read all three disks")
    log = output / "serial.log"
    with log.open("w") as stream:
        process = subprocess.Popen(command, cwd=repo, stdin=subprocess.DEVNULL,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 100
            while process.poll() is None and time.monotonic() < deadline:
                text = log.read_text(errors="replace")
                if ("[M5 LAB] FAIL" in text or
                        all("[M5 LAB] " + marker in text for marker in required)):
                    break
                time.sleep(0.2)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    text = log.read_text(errors="replace")
    missing = [marker for marker in required if "[M5 LAB] " + marker not in text]
    passed = not missing and "[M5 LAB] FAIL" not in text
    (output / "result.json").write_text(json.dumps(
        {"status": "PASS" if passed else "FAIL", "missing": missing, "argv": command},
        indent=2))
    print("PASS M5 lifecycle lab" if passed else f"FAIL missing {missing}")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("instrument", "run"))
    parser.add_argument("workspace", type=Path, help="disposable /dev/shm/osinfo-m5lab.* source copy")
    parser.add_argument("--build", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    repo = args.workspace.resolve()
    if repo.parent != Path("/dev/shm") or not repo.name.startswith("osinfo-m5lab."):
        parser.error("instrumentation is limited to a disposable lab source copy")
    if args.action == "instrument":
        instrument(repo)
        return 0
    if not (args.build and args.baseline and args.output):
        parser.error("run requires --build, --baseline and --output")
    return run(repo, args.build.resolve(), args.baseline.resolve(), args.output.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
