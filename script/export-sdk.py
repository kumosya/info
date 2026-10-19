#!/usr/bin/env python3
"""Export the application SDK without kernel-private headers or sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


class SdkExporter:
    def __init__(self, repository, destination):
        self.repository = repository.resolve()
        self.destination = destination.resolve()
        if self.destination == self.repository or self.destination in self.repository.parents:
            raise ValueError('SDK output cannot replace the repository or its parents')

    def copy(self, source, target):
        source = self.repository / source
        target = self.destination / target
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    def export(self):
        for name in ('stdc', 'stdc++', 'posix', 'os-c++', 'osgui-c++'):
            shutil.copytree(self.repository / 'include' / name,
                            self.destination / 'include' / name, dirs_exist_ok=True)
        for package, libraries in [('libos-c++', ['libos-c++.so']),
                                   ('libc++', ['libc.so', 'libc++.so']),
                                   ('libosgui-c++', ['libosgui-c++.so'])]:
            for library in libraries:
                self.copy(f'packages/{package}/{library}', f'lib/{library}')
        for entry in ('start.o', 'service_start.o'):
            self.copy(f'obj/user/runtime/{entry}', f'lib/{entry}')
        self.copy('build/lib/ld-osinfo-x86-64.so', 'lib/ld-osinfo-x86-64.so')
        for name in ('user.lds', 'boot-service.lds'):
            self.copy('script/' + name, 'lib/' + name)
        self.copy('script/build-project.py', 'tools/build-project.py')
        self.copy('script/packages.py', 'tools/packages.py')
        files = {}
        for path in sorted(self.destination.rglob('*')):
            if path.is_file() and path.name != 'sdk.json':
                files[path.relative_to(self.destination).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
        metadata = {'format': 1, 'target': 'osinfo-x86-64', 'syscall_count': 19,
                    'interpreter': '/lib/ld-osinfo-x86-64.so', 'files': files}
        (self.destination / 'sdk.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        print(f'OSInfo SDK: {self.destination} ({len(files)} files)')

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--output', default='build/sdk')
        args = parser.parse_args()
        cls(Path(__file__).resolve().parents[1], Path(args.output)).export()


if __name__ == '__main__':
    SdkExporter.main()
