"""通过真实 PS/2 虚拟设备注入键鼠，验证双插槽多核输入和 GUI 消费。"""
from pathlib import Path
import argparse
import json
import re
import socket
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--image', type=Path, default=root/'build/export/osinfo.img')
parser.add_argument('--cores', type=int, default=8)
parser.add_argument('--sockets', type=int, default=2)
args = parser.parse_args()
output = root/'build/input-tests'
output.mkdir(parents=True, exist_ok=True)
suffix = f'{args.sockets}socket-{args.cores}core'
serial = output/f'{suffix}.log'
serial.write_text('')
with tempfile.TemporaryDirectory(prefix='osinfo-input-') as folder:
    qmp = str(Path(folder)/'qmp')
    command = ['qemu-system-x86_64', '-m', '1024', '-smp',
               f'{args.cores*args.sockets},sockets={args.sockets},cores={args.cores},threads=1',
               '-display', 'none', '-serial', f'file:{serial}', '-qmp', f'unix:{qmp},server=on,wait=off',
               '-drive', f'file={args.image},if=none,id=root,format=raw,snapshot=on',
               '-device', 'ich9-ahci,id=ahci',
               '-device', 'ide-hd,drive=root,bus=ahci.0,bootindex=0', '-no-reboot', '-no-shutdown']
    with (output/f'{suffix}-qemu.log').open('w') as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        connection = socket.socket(socket.AF_UNIX)
        try:
            deadline = time.monotonic()+240
            while not Path(qmp).exists() and time.monotonic() < deadline:
                time.sleep(0.1)
            connection.connect(qmp)
            connection.settimeout(20)
            stream = connection.makefile('rwb', buffering=0)
            stream.readline()
            def request(name, arguments=None):
                stream.write((json.dumps({'execute':name, 'arguments':arguments or {}})+'\n').encode())
                while True:
                    answer = json.loads(stream.readline())
                    if 'error' in answer:
                        raise RuntimeError(answer)
                    if 'return' in answer:
                        return answer['return']
            request('qmp_capabilities')
            def wait_for(predicate, label):
                while time.monotonic() < deadline:
                    text = serial.read_text(errors='replace')
                    if '[PANIC]' in text or process.poll() is not None:
                        raise RuntimeError(f'guest stopped: {serial}')
                    if predicate(text):
                        return text
                    time.sleep(0.2)
                raise TimeoutError(f'{label}: {serial}')
            wait_for(lambda text:'boot display complete' in text, 'GUI startup')
            request('human-monitor-command', {'command-line':'sendkey ctrl-shift-f2'})
            wait_for(lambda text:'[CONSOLE] switched to tty2' in text, 'keyboard switch')
            request('human-monitor-command', {'command-line':'sendkey ctrl-shift-f7'})
            time.sleep(1)
            for index in range(24):
                request('input-send-event', {'events':[
                    {'type':'rel','data':{'axis':'x','value':8 if index < 12 else -8}},
                    {'type':'rel','data':{'axis':'y','value':2}}]})
                time.sleep(0.08)
            text = wait_for(lambda text:any(int(value) > 0 for value in
                re.findall(r'pointer_samples=(\d+)', text)), 'mouse dispatch')
            expected = f'[SMP] online CPUs={args.cores*args.sockets}'
            if expected not in text:
                raise RuntimeError(f'CPU topology not online: {serial}')
            print(f'{suffix}: keyboard TTY switch and mouse presentation passed')
        finally:
            connection.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
