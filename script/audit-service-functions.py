#!/usr/bin/env python3
"""Report free C++ functions in service and OS-library sources."""
from pathlib import Path
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
    roots = ['src/init', 'src/task', 'src/fs', 'src/pci', 'src/device', 'src/storage',
             'src/tty', 'src/console', 'src/users', 'user/guiw/server', 'lib/os', 'lib/osgui',
             'include/os-c++', 'include/osgui-c++']
    allowed_syscalls = {'invokeSyscall', 'exitTask', 'exitThread', 'createThread', 'waitThread',
        'waitAddress', 'wakeAddress', 'setSignalAction', 'setSignalMask', 'getPendingSignals',
        'waitSignal', 'signalReturn', 'sendThreadSignal', 'forkTask', 'waitChild', 'replaceTaskImage',
        'yield', 'sleep', 'mapMemory', 'createEndpoint', 'sendMessage', 'receiveMessage', 'callService',
        'replyMessage', 'cancelIpc', 'spawnProgram', 'spawnImage', 'getStandardHandle',
        'getBootstrapHandle', 'getTaskState', 'getTaskInfo', 'terminateTask', 'sendSignal',
        'setTaskGroup', 'createSession', 'getTaskIdentifiers', 'closeHandle', 'getTaskId',
        'getThreadId', 'unmapMemory', 'protectMemory'}
    violations = []
    for directory in roots:
        for path in sorted((ROOT / directory).rglob('*')):
            if path.suffix not in ('.cc', '.h'):
                continue
            for name, line, _, _ in functions(path):
                if path.relative_to(ROOT).as_posix() == 'lib/os/runtime.cc' and name in allowed_syscalls:
                    continue
                violations.append(f'{path.relative_to(ROOT)}:{line}: {name}')
    print('\n'.join(violations) if violations else 'Service/OS API object audit passed')
    raise SystemExit(bool(violations))
