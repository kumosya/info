"""在独立 QEMU 镜像中检查六个文字终端的动态 exec 继承。"""
from pathlib import Path
import argparse
import shutil
import struct
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--image', type=Path, default=root/'build/export/osinfo.img')
parser.add_argument('--backend', choices=('virtio', 'ahci'), default='ahci')
args = parser.parse_args()
output = root/'build/terminal-tests'
output.mkdir(parents=True, exist_ok=True)
binary = output/'tty-probe'
includes = []
for part in ('', 'stdc', 'stdc++', 'posix', 'os-c++'):
    includes += ['-I', str(root/'include'/part)]
subprocess.run(['g++', '-std=c++20', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-fPIC', '-mno-red-zone', '-nostdinc',
                '-nostdinc++', '-fno-exceptions', '-fno-rtti',
                '-fno-threadsafe-statics', '-fno-use-cxa-atexit', *includes,
                '-no-pie', '-nostdlib', '-nostartfiles', '-nodefaultlibs',
                '-Wl,--build-id=none,--hash-style=sysv',
                f'-Wl,-T,{root}/script/user.lds',
                '-Wl,--dynamic-linker=/lib/ld-osinfo-amd64.so',
                str(root/'build/obj/lib/os/start.o'),
                str(root/'script/tests/terminal-inheritance.cc'),
                '-Wl,--no-as-needed', str(root/'packages/libc++/libc.so'),
                str(root/'packages/libos-c++/libos-c++.so'), '-lgcc',
                '-o', str(binary)], check=True)

with tempfile.TemporaryDirectory(prefix='osinfo-terminal-test-') as folder:
    work = Path(folder)
    image = work/'test.img'
    shutil.copyfile(args.image, image)
    with image.open('rb') as stream:
        mbr = stream.read(512)
        first, sectors = struct.unpack_from('<II', mbr, 454)
        if mbr[510:512] != b'\x55\xaa' or not first or not sectors or (first+sectors)*512 > image.stat().st_size:
            raise RuntimeError('invalid test image partition')
        stream.seek(first*512)
        partition = work/'root.ext3'
        partition.write_bytes(stream.read(sectors*512))
    sessions = work/'sessions'
    sessions.write_text('terminal|/usr/bin/terminal|terminal\nlogin|/bin/tty-probe|tty-probe\ngraphical|/usr/bin/guiw-session|guiw-session\n')
    for command in (f'write {binary} /bin/tty-probe',
                    'set_inode_field /bin/tty-probe mode 0100755',
                    'rm /etc/sessions', f'write {sessions} /etc/sessions'):
        subprocess.run(['debugfs', '-w', '-R', command, str(partition)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with image.open('r+b') as target, partition.open('rb') as source:
        target.seek(first*512)
        shutil.copyfileobj(source, target)

    serial = output/f'{args.backend}-serial.log'
    serial.write_text('')
    command = ['qemu-system-x86_64', '-m', '1024', '-smp', '4', '-display', 'none',
               '-serial', f'file:{serial}', '-no-reboot', '-no-shutdown',
               '-drive', f'file={image},if=none,format=raw,id=root,snapshot=on']
    if args.backend == 'ahci':
        command += ['-device', 'ich9-ahci,id=ahci', '-device',
                    'ide-hd,drive=root,bus=ahci.0,bootindex=0']
    else:
        command += ['-device', 'virtio-blk-pci,drive=root,disable-legacy=on,bootindex=0']
    with (output/f'{args.backend}-qemu.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+240
            while time.monotonic() < deadline:
                text = serial.read_text(errors='replace')
                if '[TTY-EXEC] FAIL' in text or '[PANIC]' in text:
                    raise RuntimeError(f'terminal inheritance failed: {serial}\n'+
                        '\n'.join(line for line in text.splitlines() if '[TTY-EXEC]' in line))
                if all(f'[TTY-EXEC] PASS tty={index} runtime={index}' in text for index in range(1, 7)):
                    print('tty1-tty6: terminal, environment, cwd and descriptors survived exec', flush=True)
                    break
                if process.poll() is not None:
                    raise RuntimeError('QEMU exited before terminal tests completed')
                time.sleep(0.2)
            else:
                raise TimeoutError(f'terminal tests timed out: {serial}')
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
