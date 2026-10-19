#!/usr/bin/env python3
"""Exercise a staged real GUI with QMP mouse/keyboard events in snapshot mode."""
import argparse
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tarfile
import tempfile
import time
from packages import inspect_archive, install_archives


class DesktopInputSmoke:
    def __init__(self, stage, archives=(), command='echo gui alive'):
        self.repository = Path(__file__).resolve().parents[1]
        self.stage = stage.resolve(strict=True)
        if self.stage.parent != (self.repository / 'build').resolve() or not self.stage.name.startswith('hybrid-smoke.'):
            raise ValueError('use an isolated hybrid-smoke stage inside build')
        self.archives = archives
        self.command = command

    def prepare(self):
        partition = self.stage / 'root.ext3'
        if self.archives:
            install_archives(self.archives, self.stage / 'root')
            for archive_path in self.archives:
                package = inspect_archive(archive_path)
                for _, target in package.files:
                    source = self.stage / 'root' / str(target).lstrip('/')
                    for command in [f'rm {target}', f'write {source} {target}', f'set_inode_field {target} mode 0100755']:
                        subprocess.run(['debugfs', '-w', '-R', command, str(partition)], check=True,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with tarfile.open(self.repository / 'packages/external/guiw.inpk', 'r:gz') as archive:
            for member, target in [('terminal', '/usr/bin/terminal'), ('guiw.elf', '/sbin/sys/guiw.elf')]:
                source = self.stage / ('input-' + member)
                source.write_bytes(archive.extractfile(member).read())
                for command in [f'rm {target}', f'write {source} {target}', f'set_inode_field {target} mode 0100755']:
                    subprocess.run(['debugfs', '-w', '-R', command, str(partition)], check=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with (self.stage / 'smoke.img').open('r+b') as disk, partition.open('rb') as source:
            offset = struct.unpack_from('<I', disk.read(512), 454)[0] * 512
            while block := source.read(1024 * 1024):
                disk.seek(offset)
                if disk.read(len(block)) != block:
                    disk.seek(offset); disk.write(block)
                offset += len(block)
        boot = self.stage / 'iso/boot'
        shutil.copy2(self.stage / 'input-guiw.elf', boot / 'guiw.elf')
        shutil.copy2(self.repository / 'build/kernel.elf', boot / 'kernel.elf')
        iso = self.stage / 'input.iso'
        subprocess.run(['grub-mkrescue', '-o', str(iso), str(self.stage / 'iso')], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return iso

    def request(self, operation, arguments=None):
        self.stream.write((json.dumps({'execute': operation, 'arguments': arguments or {}}) + '\n').encode())
        self.stream.flush()
        while True:
            response = json.loads(self.stream.readline())
            if 'error' in response:
                raise RuntimeError(response['error'])
            if 'return' in response:
                return response['return']

    def move(self, x, y):
        self.request('input-send-event', {'events': [
            {'type': 'rel', 'data': {'axis': 'x', 'value': x}},
            {'type': 'rel', 'data': {'axis': 'y', 'value': y}}]})
        time.sleep(.06)

    def click(self):
        for down in (True, False):
            self.request('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
            time.sleep(.025)

    def move_exact(self, x, y):
        # Device policy accelerates PS/2 deltas. Invert it instead of assuming
        # a large QMP relative movement equals a screen-pixel movement.
        def packet(remaining):
            magnitude = abs(remaining)
            raw = min(127, magnitude // 4) if magnitude >= 32 else min(7, magnitude // 3) if magnitude >= 9 else min(2, magnitude)
            raw = -raw if remaining < 0 else raw
            scale = 4 if abs(raw) >= 8 else 3 if abs(raw) >= 3 else 1
            return raw, raw * scale
        while x or y:
            raw_x, actual_x = packet(x)
            raw_y, actual_y = packet(y)
            self.move(raw_x, raw_y)
            x -= actual_x; y -= actual_y

    def type(self, text):
        for value in text:
            code = {' ': 'spc', '\n': 'ret', '-': 'minus', '.': 'dot', '/': 'slash'}.get(value, value)
            self.request('send-key', {'keys': [{'type': 'qcode', 'data': code}], 'hold-time': 25})
            time.sleep(.07)

    def run(self):
        iso = self.prepare()
        log = self.repository / 'build/gui-input-runtime.log'
        with tempfile.TemporaryDirectory(prefix='osinfo-qmp.') as temporary:
            path = Path(temporary) / 'qmp'
            command = ['qemu-system-x86_64', '-m', '256M', '-smp', '4', '-boot', 'd', '-cdrom', str(iso),
                '-drive', f'file={self.stage / "smoke.img"},if=none,format=raw,id=osdisk,snapshot=on',
                '-device', 'virtio-blk-pci,drive=osdisk,disable-legacy=on', '-display', 'none',
                '-serial', f'file:{log}', '-monitor', 'none', '-qmp', f'unix:{path},server=on,wait=off', '-no-reboot']
            with subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as child:
                try:
                    deadline = time.monotonic() + 50
                    while time.monotonic() < deadline:
                        if child.poll() is not None: raise RuntimeError(child.stderr.read().decode())
                        if path.exists() and log.exists() and '[INIT] tty1-tty6 shells started' in log.read_text(errors='replace'): break
                        time.sleep(.1)
                    else: raise RuntimeError('desktop startup timed out')
                    self.connection = socket.socket(socket.AF_UNIX)
                    self.connection.settimeout(10)
                    self.connection.connect(str(path))
                    self.stream = self.connection.makefile('rwb')
                    json.loads(self.stream.readline())
                    self.request('qmp_capabilities')
                    time.sleep(2)
                    self.request('screendump', {'filename': str(self.repository / 'build/gui-input-before.ppm')})
                    # Start in the terminal body (cursor initially 512,384).
                    for _ in range(100):
                        self.move(2, 0); self.click(); self.move(-2, 0)
                    # Select the center of the new-tab button at screen 284,105.
                    self.move_exact(-228, -279); self.click(); time.sleep(2)
                    self.type(self.command + '\n'); time.sleep(2)
                    self.request('screendump', {'filename': str(self.repository / 'build/gui-input-after.ppm')})
                    self.request('quit')
                    child.wait(timeout=10)
                finally:
                    if child.poll() is None:
                        child.terminate()
                        child.wait(timeout=10)
        text = log.read_text(errors='replace')
        if any(marker in text for marker in ('page fault', 'unrecoverable kernel exception', '[LD]')):
            raise RuntimeError('guest reported a fault; inspect gui-input-runtime.log')
        print('100 mouse clicks, a new tab, and keyboard input delivered; inspect before/after screenshots.')

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--stage', type=Path, required=True)
        parser.add_argument('--package', action='append', type=Path, default=[])
        parser.add_argument('--command', default='echo gui alive')
        args = parser.parse_args()
        cls(args.stage, args.package, args.command).run()


if __name__ == '__main__':
    DesktopInputSmoke.main()
