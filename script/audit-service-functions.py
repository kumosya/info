#!/usr/bin/env python3
"""Report free C++ functions in service and OS-library sources."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]


def mask_cpp(text):
    pattern = r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda match: ''.join('\n' if c == '\n' else ' ' for c in match[0]), text)


def functions(path):
    text = path.read_text(encoding='utf-8-sig')
    masked = mask_cpp(text)
    stack, ends = [], {}
    for i, value in enumerate(masked):
        if value == '{':
            stack.append(i)
        elif value == '}' and stack:
            ends[stack.pop()] = i
    classes = []
    class_pattern = r'\b(?:class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?\w+\s*(?:final\s*)?(?::[^{};]+)?\{'
    for match in re.finditer(class_pattern, masked):
        opening = masked.index('{', match.start(), match.end())
        if opening in ends:
            classes.append((opening, ends[opening]))
    pattern = r'^[ \t]*(?:(?:inline|static|constexpr|\[\[[^\]]*\]\])\s+)*[A-Za-z_][\w:<>,*& \t]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{'
    for match in re.finditer(pattern, masked, re.M):
        name = match[1]
        if name in ('if', 'while', 'for', 'switch', 'catch', 'main', 'osRuntimeInit', 'osRuntimeFini'):
            continue
        if match.start(1) and masked[match.start(1) - 1] == ':':
            continue
        if any(start < match.start() < end for start, end in classes):
            continue
        yield name, text.count('\n', 0, match.start()) + 1, match.start(), ends[match.end() - 1] + 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--service-root', action='append', type=Path, default=[])
    args = parser.parse_args()
    roots = ['src/init', 'src/task', 'src/fs', 'src/pci', 'src/device', 'src/storage',
             'src/tty', 'src/console', 'src/users', 'user/ld', 'user/mm', 'user/pipe', 'user/msgq', 'lib/os', 'lib/osgui',
             'include/os-c++', 'include/osgui-c++']
    allowed_syscalls = {'invokeSyscall', 'exitTask', 'exitThread', 'createThread',
        'waitAddress', 'wakeAddress', 'forkTask', 'replaceTaskImage', 'yield', 'sleep',
        'mapMemory', 'sendMessage', 'receiveMessage', 'callService', 'replyMessage',
        'getStandardHandle', 'getBootstrapHandle', 'closeHandle', 'unmapMemory', 'protectMemory'}
    violations = []
    for directory in [*(ROOT / name for name in roots), *args.service_root]:
        for path in sorted(directory.rglob('*')):
            if path.suffix not in ('.cc', '.h'):
                continue
            for name, line, _, _ in functions(path):
                label = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else path.as_posix()
                if label == 'lib/os/runtime.cc' and name in allowed_syscalls:
                    continue
                violations.append(f'{label}:{line}: {name}')
    print('\n'.join(violations) if violations else 'Service/OS API object audit passed')
    raise SystemExit(bool(violations))
