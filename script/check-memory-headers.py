#!/usr/bin/env python3
"""Keep memory declarations in mm.h/page.h while splitting implementation files."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
allowed = {'mm.h', 'page.h'}
violations = []
for directory in ('include/kernel/memory', 'src/kernel/memory', 'user/mm'):
    for path in (root / directory).rglob('*.h'):
        if path.name not in allowed:
            violations.append(path.relative_to(root).as_posix())
for directory in ('include/kernel', 'include/os-c++/os'):
    for name in ('memory.h', 'heap.h', 'buddy.h', 'slab.h', 'pool.h', 'page_object.h', 'numa.h'):
        path = root / directory / name
        if path.exists():
            violations.append(path.relative_to(root).as_posix())
if violations:
    raise SystemExit('Unexpected memory headers: ' + ', '.join(violations))
print('Memory header boundary: mm.h/page.h only')
