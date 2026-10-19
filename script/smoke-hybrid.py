#!/usr/bin/env python3
"""Boot current artifacts using an isolated image; never modify the source disk."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

repository = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--disk', default='build/osinfo.img')
parser.add_argument('--smp', type=int, default=4)
parser.add_argument('--seconds', type=int, default=25)
parser.add_argument('--memory-test', action='store_true')
parser.add_argument('--desktop-test', action='store_true')
parser.add_argument('--lifecycle-test', action='store_true')
args = parser.parse_args()
if args.lifecycle_test and args.smp < 3:
    parser.error('--lifecycle-test requires at least three CPUs')
if args.memory_test and args.desktop_test:
    parser.error('select either --memory-test or --desktop-test')
stage = Path(tempfile.mkdtemp(prefix='hybrid-smoke.', dir=repository / 'build'))
print(f'Staging isolated boot artifacts at {stage}', flush=True)
root = stage / 'root'
root.mkdir()
for name in ('bin', 'boot/grub', 'dev/pts', 'etc', 'home/user', 'root',
             'mnt/ext4', 'mnt/fat32', 'sbin/sys', 'sys', 'tmp', 'usr/lib'):
    (root / name).mkdir(parents=True, exist_ok=True)
shutil.copytree(repository / 'script/etc', root / 'etc', dirs_exist_ok=True)
subprocess.run(['python3', 'script/layout.py', '--root', str(root)], cwd=repository, check=True)
subprocess.run(['python3', 'script/packages.py', '--install-root', str(root)], cwd=repository, check=True)
if args.memory_test or args.desktop_test:
    shutil.copy2(root / 'usr/bin/terminal', root / 'usr/bin/terminal.real')
    runtime_test = 'desktop-runtime' if args.desktop_test else 'mm-runtime'
    shutil.copy2(repository / f'build/tests/{runtime_test}', root / 'usr/bin/terminal')
if args.desktop_test:
    probes = repository / 'build/tests/linker'
    (root / 'usr/lib/osinfo-test').mkdir(parents=True, exist_ok=True)
    for library in probes.glob('*.so'):
        shutil.copy2(library, root / 'usr/lib/osinfo-test' / library.name)
    shutil.copy2(probes / 'probe', root / 'usr/bin/ld-probe')
    shutil.copy2(probes / 'undefined', root / 'usr/bin/ld-undefined')
    (root / 'usr/bin/ld-missing').write_bytes((probes / 'probe').read_bytes().replace(b'libprobe.so\0', b'liblost0.so\0'))
for service in ('init', 'task', 'fs', 'pci', 'device', 'storage', 'tty', 'console', 'users', 'mm', 'pipe', 'msgq'):
    shutil.copy2(repository / f'build/services/{service}.elf', root / f'sbin/sys/{service}.elf')
source_disk = (repository / args.disk).resolve(strict=True)
with source_disk.open('rb') as source:
    mbr = source.read(512)
start, sectors = struct.unpack_from('<II', mbr, 446 + 8)
if mbr[510:] != b'\x55\xaa' or not start or not sectors:
    raise SystemExit('Expected an MBR disk with a non-empty first partition')
partition = stage / 'root.ext3'
with partition.open('xb') as output:
    output.truncate(sectors * 512)
subprocess.run(['mkfs.ext3', '-F', '-b', '4096', '-L', 'OSINFO_ROOT', '-d', str(root), str(partition)], check=True)
subprocess.run(['python3', 'script/layout.py', '--root', str(root), '--image', str(partition)], cwd=repository, check=True)
disk = stage / 'smoke.img'
shutil.copyfile(source_disk, disk)
with disk.open('r+b') as output, partition.open('rb') as source:
    output.seek(start * 512)
    shutil.copyfileobj(source, output)
iso_root = stage / 'iso'
boot = iso_root / 'boot'
(boot / 'grub').mkdir(parents=True)
shutil.copy2(repository / 'script/grub.cfg', boot / 'grub/grub.cfg')
kernel = 'build/tests/kernel-lifecycle.elf' if args.lifecycle_test else 'build/kernel.elf'
shutil.copy2(repository / kernel, boot / 'kernel.elf')
for service in ('init', 'task', 'fs', 'pci', 'device', 'storage', 'tty', 'console', 'guiw', 'users', 'mm', 'pipe', 'msgq'):
    source = root / 'sbin/sys/guiw.elf' if service == 'guiw' else repository / f'build/services/{service}.elf'
    shutil.copy2(source, boot / f'{service}.elf')
for library, package in (('libos-c++.so', 'libos-c++'), ('libc.so', 'libc++'),
                         ('libc++.so', 'libc++'), ('libosgui-c++.so', 'libosgui-c++')):
    shutil.copy2(repository / f'packages/{package}/{library}', boot / library)
iso = stage / 'smoke.iso'
subprocess.run(['grub-mkrescue', '-o', str(iso), str(iso_root)], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
log = stage / 'serial.log'
command = ['qemu-system-x86_64', '-m', '256M', '-smp', str(args.smp), '-boot', 'd',
           '-cdrom', str(iso), '-drive', f'file={disk},if=none,format=raw,id=osdisk,snapshot=on',
           '-device', 'virtio-blk-pci,drive=osdisk,disable-legacy=on',
           '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot']
with log.open('wb') as output:
    try:
        subprocess.run(command, timeout=args.seconds, stdout=output, stderr=subprocess.STDOUT, check=True)
    except subprocess.TimeoutExpired:
        pass
print(log.read_text(errors='replace'))
print(f'Serial log: {log}')
text = log.read_text(errors='replace')
required = ('[MM] Ring 3 memory policy and pager service ready', '[PCI] service ready', '[INIT] console and virtual TTY service started', '[INIT] tty1-tty6 shells started')
required += ('[PIPE] Ring 3 service ready', '[MSGQ] Ring 3 service ready')
if args.memory_test:
    required += ('[MM TEST] anonymous, protection, partial unmap, COW, shared, file and signal passed',
                 '[MM TEST] service publication and RPC deadlines passed',
                 '[MM TEST] exec resets memory policy generation passed',
                 '[MM TEST] Ring3 pipe and msgq passed')
if args.lifecycle_test:
    required += ('[TASK TEST] killed cleanup requester passed (32 rounds)',
                 '[TASK TEST] remote CR3 exec and concurrent kill passed (64 rounds)')
if args.desktop_test:
    required += ('[DESKTOP TEST] saturated window notifications passed',
                 '[DESKTOP TEST] shell argv conventions passed',
                 '[DESKTOP TEST] XDG path and environment semantics passed',
                 '[DESKTOP TEST] real user login and XDG session lifecycle passed')
    required += ('[DESKTOP TEST] dynamic dependencies and explicit loader failures passed',
                 '[LD TEST] transitive dependency and constructor passed',
                 '[LD TEST] shared-library destructor passed',
                 '[LD] library not found: liblost0.so',
                 '[LD] undefined symbol: osinfoMissingProbeSymbol')
if any(marker not in text for marker in required) or 'unrecoverable kernel exception' in text:
    raise SystemExit('Boot smoke check failed; inspect serial.log')
