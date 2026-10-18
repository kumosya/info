#!/usr/bin/env python3
"""Fail if a trusted kernel import no longer matches the OS library ABI."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
source = (root / 'src/kernel/task/services.cc').read_text()
imports = set(re.findall(r'\{"([^"]+)", reinterpret_cast<uint64_t>', source))
objects = [root / 'obj/user/runtime/runtime.o', root / 'obj/user/runtime/service.o']
symbols = subprocess.check_output(['nm', '-g', '--defined-only', *map(str, objects)], text=True)
defined = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) == 3}
missing = imports - defined
if missing:
    raise SystemExit('Missing trusted service imports: ' + ', '.join(sorted(missing)))
print(f'Trusted service ABI: {len(imports)} imports verified')
