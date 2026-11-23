"""在独立 QEMU 镜像及带标记的临时盘上验证真实存储驱动，不访问宿主磁盘。"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--smp", type=int, default=4)
parser.add_argument("--backend", choices=("virtio", "ide", "ahci"), default="virtio")
parser.add_argument("--image", type=Path, default=root / "build/export/osinfo.img")
args = parser.parse_args()
output = root / "build/storage-tests"
output.mkdir(parents=True, exist_ok=True)
includes = []
for part in ("", "stdc", "posix", "os-c++", "stdc++"):
    includes += ["-I", str(root / "include" / part)]
binary = output / "storage-smoke"
subprocess.run(["g++", "-std=c++20", "-ffreestanding", "-fno-builtin",
                "-fno-stack-protector", "-fPIC", "-mno-red-zone", "-nostdinc",
                "-nostdinc++", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics",
                "-fno-use-cxa-atexit", *includes, "-no-pie", "-nostdlib",
                "-nostartfiles", "-nodefaultlibs", "-Wl,--build-id=none,--hash-style=sysv",
                f"-Wl,-T,{root}/script/user.lds", "-Wl,--dynamic-linker=/lib/ld-osinfo-amd64.so",
                str(root / "build/obj/lib/os/start.o"), str(root / "script/tests/storage-smoke.cc"),
                str(root / "script/tests/filesystem-permissions.cc"),
                "-Wl,--no-as-needed", str(root / "packages/libc++/libc.so"),
                str(root / "packages/libos-c++/libos-c++.so"), "-lgcc", "-o", str(binary)], check=True)

with tempfile.TemporaryDirectory(prefix="osinfo-storage-") as folder:
    work = Path(folder)
    image = work / "osinfo.img"
    shutil.copyfile(args.image, image)
    with image.open("rb") as stream:
        mbr = stream.read(512)
    if mbr[510:512] != b"\x55\xaa":
        raise RuntimeError("test requires an MBR boot image")
    start, sectors = struct.unpack_from("<II", mbr, 446 + 8)
    if not start or not sectors or (start + sectors) * 512 > image.stat().st_size:
        raise RuntimeError("invalid root partition")
    partition = work / "root.ext3"
    with image.open("rb") as source, partition.open("wb") as target:
        source.seek(start * 512)
        remaining = sectors * 512
        while remaining:
            chunk = source.read(min(1024 * 1024, remaining))
            if not chunk:
                raise RuntimeError("truncated image")
            target.write(chunk)
            remaining -= len(chunk)
    sessions = work / "sessions"
    sessions.write_text("terminal|/usr/bin/terminal|terminal\nlogin|/bin/storage-smoke|/bin/storage-smoke\ngraphical|/usr/bin/guiw-session|guiw-session\n")
    commands = [f"write {binary} /bin/storage-smoke",
                "set_inode_field /bin/storage-smoke mode 0100755",
                "rm /etc/sessions", f"write {sessions} /etc/sessions"]
    for command in commands:
        subprocess.run(["debugfs", "-w", "-R", command, str(partition)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with partition.open("rb") as source, image.open("r+b") as target:
        target.seek(start * 512)
        shutil.copyfileobj(source, target)
    scratch = work / "scratch.raw"
    readonly = work / "readonly.raw"
    for path, size, marker in [(scratch, 16, b"OSINFO-STORAGE-SCRATCH"),
                               (readonly, 12, b"OSINFO-STORAGE-READONLY")]:
        with path.open("wb") as stream:
            stream.write(marker)
            stream.truncate(size * 1024 * 1024)
    suffix = f"{args.backend}-{args.smp}cpu"
    serial = output / f"serial-{suffix}.log"
    qemu_log = output / f"qemu-{suffix}.log"
    # QEMU 启动是异步的，先清空旧日志，不能把上一次的 PASS/FAIL 当成本轮结果。
    serial.write_text("")
    root_device = "virtio-blk-pci,drive=root,disable-legacy=on,bootindex=0"
    scratch_device = "virtio-blk-pci,drive=scratch,disable-legacy=on"
    if args.backend == "ide":
        root_device = "ide-hd,drive=root,bus=ide.0,unit=0,bootindex=0"
        scratch_device = "ide-hd,drive=scratch,bus=ide.0,unit=1"
    if args.backend == "ahci":
        root_device = "ide-hd,drive=root,bus=ahci.0,bootindex=0"
        scratch_device = "ide-hd,drive=scratch,bus=ahci.1"
    command = ["qemu-system-x86_64", "-m", "1024", "-smp", str(args.smp),
               "-display", "none", "-serial", f"file:{serial}", "-boot", "c",
               "-drive", f"file={image},if=none,format=raw,id=root,snapshot=on",
               "-device", root_device,
               "-drive", f"file={scratch},if=none,format=raw,id=scratch,cache=writeback",
               "-device", scratch_device,
               "-drive", f"file={readonly},if=none,format=raw,id=readonly,readonly=on",
               "-device", "virtio-blk-pci,drive=readonly,disable-legacy=on",
               "-no-reboot", "-no-shutdown"]
    if args.backend == "ahci":
        command[1:1] = ["-device", "ich9-ahci,id=ahci"]
    with qemu_log.open("w") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 240
            previous = ""
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace") if serial.exists() else ""
                lines = "\n".join(line for line in text.splitlines() if "[STORAGE-TEST]" in line)
                if lines != previous:
                    print(lines[len(previous):], flush=True)
                    previous = lines
                if "[STORAGE-TEST] RESULT FAIL" in text:
                    raise RuntimeError(f"storage test failed; see {serial}")
                if "fstab filesystem initialization failed" in text or "[PANIC]" in text:
                    raise RuntimeError(f"boot failed; see {serial}")
                if "[STORAGE-TEST] RESULT PASS" in text:
                    break
                if process.poll() is not None:
                    raise RuntimeError(qemu_log.read_text())
                time.sleep(0.2)
            else:
                raise TimeoutError(f"storage test timed out; see {serial}")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
    # 关机后从宿主重新读盘，确认 flush 后的数据确实写入后端文件。
    with scratch.open("rb") as stream:
        stream.seek(32 * 512)
        assert stream.read(16 * 512) == bytes((index * 13 + 7) & 255 for index in range(16 * 512))
        for number in range(4):
            stream.seek((64 + number * 16) * 512)
            expected = bytes((number * 37 + 23 * 11 + index) & 255 for index in range(7 * 512))
            assert stream.read(len(expected)) == expected
    with readonly.open("rb") as stream:
        assert stream.read(23) == b"OSINFO-STORAGE-READONLY"
        stream.seek(32 * 512)
        assert stream.read(512) == bytes(512)
    print(f"Storage tests and persisted data passed ({args.backend}, {args.smp} CPUs); {serial}", flush=True)
