"""在启动镜像的临时副本中运行单个用户程序，按串口标记检查结果。"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("marker")
parser.add_argument("--image", type=Path, default=root / "build/export/osinfo.img")
args = parser.parse_args()
output = root / "build/guest-probe" / args.binary.name
output.mkdir(parents=True, exist_ok=True)
serial = output / "serial.log"
serial.write_text("")

with tempfile.TemporaryDirectory(prefix="osinfo-guest-probe-") as directory:
    work = Path(directory)
    image = work / "test.img"
    shutil.copyfile(args.image, image)
    with image.open("rb") as stream:
        mbr = stream.read(512)
        start, sectors = struct.unpack_from("<II", mbr, 454)
        if mbr[510:512] != b"\x55\xaa" or not start or not sectors or (start + sectors) * 512 > image.stat().st_size:
            raise RuntimeError("invalid root partition")
        stream.seek(start * 512)
        partition = work / "root.ext3"
        partition.write_bytes(stream.read(sectors * 512))

    sessions = work / "sessions"
    sessions.write_text("terminal|/usr/bin/terminal|terminal\nlogin|/bin/guest-probe|guest-probe\ngraphical|/usr/bin/guiw-session|guiw-session\n")
    for command in (f"write {args.binary.resolve()} /bin/guest-probe",
                    "set_inode_field /bin/guest-probe mode 0100755",
                    "rm /etc/sessions", f"write {sessions} /etc/sessions"):
        subprocess.run(["debugfs", "-w", "-R", command, str(partition)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with partition.open("rb") as source, image.open("r+b") as destination:
        destination.seek(start * 512)
        shutil.copyfileobj(source, destination)

    with (output / "qemu.log").open("w") as log:
        process = subprocess.Popen(["qemu-system-x86_64", "-m", "1024", "-smp", "4",
            "-display", "none", "-serial", f"file:{serial}", "-no-reboot", "-no-shutdown",
            "-drive", f"file={image},if=none,format=raw,id=root,snapshot=on",
            "-device", "ich9-ahci,id=ahci", "-device", "ide-hd,drive=root,bus=ahci.0,bootindex=0"],
            stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 240
            while time.monotonic() < deadline:
                text = serial.read_text(errors="replace")
                if "[PANIC]" in text or "[LD] undefined symbol:" in text or args.marker.replace("PASS", "FAIL") in text:
                    raise RuntimeError(f"guest test failed: {serial}")
                if args.marker in text:
                    print(args.marker, flush=True)
                    break
                if process.poll() is not None:
                    raise RuntimeError(f"guest stopped: {serial}")
                time.sleep(0.2)
            else:
                raise TimeoutError(f"guest test timed out: {serial}")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
