#!/usr/bin/env python3
"""Cross compile and package an independent OSInfo application project."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys


class ProjectBuilder:
    flags = ['-std=c++20', '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
             '-fno-exceptions', '-fno-rtti', '-fno-use-cxa-atexit', '-fno-threadsafe-statics',
             '-fPIC', '-m64', '-mcmodel=small', '-mno-red-zone', '-nostdinc', '-nostdinc++',
             '-Os', '-Wall', '-Wextra']

    def __init__(self, project, sdk, compiler=None):
        self.project, self.sdk = project.resolve(strict=True), sdk.resolve(strict=True)
        self.configuration = json.loads((self.project / 'osinfo-project.json').read_text(encoding='utf-8-sig'))
        self.compiler = compiler or ('x86_64-elf-g++' if shutil.which('x86_64-elf-g++') else 'g++')
        if not (self.sdk / 'sdk.json').is_file():
            raise ValueError('not an exported OSInfo SDK')
        self.name = self.identifier(self.configuration['name'])
        self.output = self.project / 'build'
        self.payload = self.output / 'packages' / self.name
        self.includes = []
        for name in ('', 'stdc', 'stdc++', 'posix', 'os-c++', 'osgui-c++'):
            self.includes += ['-I', str(self.sdk / 'include' / name)]
        self.includes += ['-I', str(self.project)]

    @staticmethod
    def identifier(value):
        if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_+.-]{1,63}', value) or value in ('.', '..'):
            raise ValueError(f'invalid identifier: {value!r}')
        return value

    def inside(self, relative):
        value = self.project / relative
        if Path(relative).is_absolute() or not value.resolve().is_relative_to(self.project):
            raise ValueError(f'path escapes project: {relative}')
        return value

    def run(self, command):
        print(subprocess.list2cmdline(list(map(str, command))), flush=True)
        subprocess.run(list(map(str, command)), cwd=self.project, check=True)

    def build(self):
        self.payload.mkdir(parents=True, exist_ok=True)
        targets = {}
        for target in self.configuration['targets']:
            name = self.identifier(target['name'])
            if name in targets:
                raise ValueError(f'duplicate target: {name}')
            kind = target.get('kind', 'application')
            if kind not in ('application', 'boot-service', 'shared'):
                raise ValueError(f'unsupported target kind: {kind}')
            targets[name] = kind
            sources = set()
            for pattern in target['sources']:
                self.inside(pattern)
                sources.update(path for path in self.project.glob(pattern) if path.is_file())
            if not sources:
                raise ValueError(f'{name}: no source files')
            objects = []
            for source in sorted(sources):
                if not source.resolve().is_relative_to(self.project) or source.suffix != '.cc':
                    raise ValueError(f'unsupported source: {source}')
                relative = source.relative_to(self.project)
                output = self.output / 'obj' / name / relative.with_suffix('.o')
                output.parent.mkdir(parents=True, exist_ok=True)
                temporary = output.with_suffix('.o.tmp')
                self.run([self.compiler, *self.flags, *self.includes, '-c', source, '-o', temporary])
                temporary.replace(output)
                objects.append(output)
            link = ['-nostdlib', '-nostartfiles', '-nodefaultlibs', '-Wl,--build-id=none,--hash-style=sysv']
            if kind == 'shared':
                link += ['-shared', '-Wl,-soname,' + name]
            else:
                service = kind == 'boot-service'
                script = self.sdk / 'lib' / ('boot-service.lds' if service else 'user.lds')
                objects.append(self.sdk / 'lib' / ('service_start.o' if service else 'start.o'))
                link += ['-no-pie', '-Wl,-E,-T,' + str(script),
                         '-Wl,--no-dynamic-linker' if service else '-Wl,--dynamic-linker=/lib/ld-osinfo-x86-64.so']
            libraries = []
            for library in target.get('libraries', ['c', 'os-c++']):
                filename = 'lib' + self.identifier(library) + '.so'
                value = self.payload / filename if filename != name and targets.get(filename) == 'shared' else self.sdk / 'lib' / filename
                if not value.is_file():
                    raise ValueError(f'missing SDK library: {library}')
                libraries.append(value)
            if target.get('runpath'):
                value = target['runpath']
                if not isinstance(value, str) or ',' in value or any(ord(c) < 32 for c in value):
                    raise ValueError('invalid RUNPATH')
                link += ['-Wl,-rpath,' + value]
            destination = self.payload / name
            temporary = destination.with_name(name + '.tmp')
            self.run([self.compiler, *link, '-o', temporary, *objects, '-Wl,--no-as-needed', *libraries, '-lgcc'])
            temporary.replace(destination)
        self.package(targets)

    def package(self, targets):
        version = self.identifier(self.configuration.get('version', '1'))
        lines = [f'name={self.name}', f'version={version}']
        mappings = self.configuration['install']
        for source, target in mappings.items():
            source_path = PurePosixPath(source)
            destination = PurePosixPath(target)
            if source_path.is_absolute() or '..' in source_path.parts or not destination.is_absolute() or '..' in destination.parts:
                raise ValueError('invalid package mapping')
            if source not in targets:
                source_file = self.inside(source)
                copied = self.payload / source
                copied.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source_file, copied)
            lines += [('bin=' if source in targets and targets[source] != 'shared' else 'file=') + target,
                      f'install={source} -> {target}']
        definitions = self.output / 'definitions' / self.name
        definitions.mkdir(parents=True, exist_ok=True)
        (definitions / 'package.conf').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        self.run([sys.executable, self.sdk / 'tools/packages.py', '--user-dir', definitions.parent,
                  '--lib-dir', self.output / 'no-library-definitions', '--package-dir', self.payload.parent])
        print(f'Package: {self.payload.parent / (self.name + ".inpk")}', flush=True)

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--project', type=Path, default=Path.cwd())
        parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('OSINFO_SDK', Path(__file__).resolve().parents[1])))
        parser.add_argument('--cxx')
        args = parser.parse_args()
        cls(args.project, args.sdk, args.cxx).build()


if __name__ == '__main__':
    try:
        ProjectBuilder.main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'OSInfo build failed: {error}', file=sys.stderr)
        raise SystemExit(1)
