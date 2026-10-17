#!/usr/bin/env python3
"""Build tar.gz .inpk archives and install their explicit absolute destinations.

package.conf is copied unchanged. Example:
    name=sysinfo
    version=0.2
    bin=/usr/bin/sysinfo
    install=sysinfo -> /usr/bin/sysinfo
"""

import argparse
from dataclasses import dataclass
import gzip
import io
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tarfile

REPOSITORY = Path(__file__).resolve().parents[1]
IDENTIFIER = re.compile(r'[A-Za-z0-9_+.-]{1,63}\Z')


def valid_name(value):
    return IDENTIFIER.fullmatch(value) and value not in ('.', '..')


def checked_path(value, absolute):
    if value.startswith('/') != absolute or '\\' in value or any(ord(c) < 32 for c in value):
        raise ValueError(f'expected {"absolute destination" if absolute else "relative archive path"}: {value!r}')
    parts = value.lstrip('/').split('/')
    if any(part in ('', '.', '..') for part in parts):
        raise ValueError(f'invalid path: {value!r}')
    return PurePosixPath(value)


@dataclass
class Package:
    name: str
    version: str
    config: bytes
    binaries: list
    files: list


def parse_config(data, label):
    metadata, binaries, declarations, files = {}, [], [], []
    sources, destinations = set(), set()
    for number, raw in enumerate(data.decode('utf-8').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        key, separator, value = line.partition('=')
        key, value = key.strip(), value.strip()
        if not separator or not value:
            raise ValueError(f'{label}:{number}: expected key=value')
        if key in ('name', 'version'):
            if key in metadata or not valid_name(value):
                raise ValueError(f'{label}:{number}: invalid or duplicate {key}')
            metadata[key] = value
        elif key in ('bin', 'file', 'include'):
            target = checked_path(value, True)
            declarations.append(target)
            if key == 'bin':
                binaries.append(target)
        elif key == 'install':
            source, arrow, target = value.partition('->')
            if not arrow:
                raise ValueError(f'{label}:{number}: expected install=archive/path -> /absolute/path')
            source, target = checked_path(source.strip(), False), checked_path(target.strip(), True)
            if str(source) == 'package.conf' or source in sources or target in destinations:
                raise ValueError(f'{label}:{number}: duplicate or reserved install path')
            sources.add(source)
            destinations.add(target)
            files.append((source, target))
        else:
            raise ValueError(f'{label}:{number}: unknown key: {key}')
    if not metadata.get('name') or not files:
        raise ValueError(f'{label}: name and install mappings are required')
    for target in declarations:
        if target not in destinations:
            raise ValueError(f'{label}: missing install mapping for {target}')
    return Package(metadata['name'], metadata.get('version', '1'), data, binaries, files)


def collect(user_dir, lib_dir):
    configs = sorted(user_dir.glob('*/package.conf')) + sorted(lib_dir.glob('*.conf'))
    packages, names, destinations = [], set(), {}
    for conf in configs:
        package = parse_config(conf.read_bytes(), conf)
        if package.name in names:
            raise ValueError(f'duplicate package name: {package.name}')
        names.add(package.name)
        for _, target in package.files:
            if target in destinations:
                raise ValueError(f'{target} belongs to both {destinations[target]} and {package.name}')
            destinations[target] = package.name
        packages.append((package, conf))
    if not packages:
        raise ValueError('no package configurations found')
    return packages


def payload_source(directory, package, relative):
    if relative.parts[0] == 'include':
        return REPOSITORY / str(relative)
    return directory / package.name / str(relative)


def validate_sources(packages, directory):
    for package, _ in packages:
        for relative, _ in package.files:
            path = payload_source(directory, package, relative)
            if not path.is_file():
                raise ValueError(f'{package.name}: missing build input: {path}')


def pack(package, conf, directory):
    stage = directory / package.name
    stage.mkdir(parents=True, exist_ok=True)
    config = stage / 'package.conf'
    if not config.is_file() or config.read_bytes() != package.config:
        config.write_bytes(package.config)
    payloads = []
    for relative, target in package.files:
        source = payload_source(directory, package, relative)
        staged = stage / str(relative)
        if source.resolve() != staged.resolve():
            staged.parent.mkdir(parents=True, exist_ok=True)
            if not staged.is_file() or staged.read_bytes() != source.read_bytes():
                shutil.copyfile(source, staged)
        staged.chmod(0o755 if target in package.binaries else 0o644)
        payloads.append((staged, str(relative)))
    archive = directory / (package.name + '.inpk')
    latest = max(path.stat().st_mtime_ns for path, _ in payloads + [(conf, '')])
    if archive.is_file() and archive.stat().st_mtime_ns >= latest:
        return archive
    temporary = archive.with_suffix('.inpk.tmp')
    try:
        with tarfile.open(temporary, 'w:gz', format=tarfile.USTAR_FORMAT) as output:
            info = tarfile.TarInfo('package.conf')
            info.size, info.mode = len(package.config), 0o644
            output.addfile(info, io.BytesIO(package.config))
            for path, relative in payloads:
                info = output.gettarinfo(str(path), arcname=relative)
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ''
                with path.open('rb') as source:
                    output.addfile(info, source)
        temporary.replace(archive)
    finally:
        temporary.unlink(missing_ok=True)
    return archive


def inspect_archive(path):
    # Read through the gzip trailer before modifying the installation root.
    with gzip.open(path, 'rb') as stream:
        while stream.read(1024 * 1024):
            pass
    with tarfile.open(path, 'r:gz') as archive:
        members = {}
        for member in archive.getmembers():
            checked_path(member.name, False)
            if not member.isfile() or member.name in members:
                raise ValueError(f'{path}: duplicate or non-regular archive entry: {member.name}')
            members[member.name] = member
        if 'package.conf' not in members or members['package.conf'].size > 1024 * 1024:
            raise ValueError(f'{path}: missing or oversized package.conf')
        package = parse_config(archive.extractfile(members['package.conf']).read(), path)
        expected = {'package.conf'} | {str(source) for source, _ in package.files}
        if set(members) != expected:
            raise ValueError(f'{path}: archive payload does not match install mappings')
        return package


def image_path(root, absolute):
    path = root / str(absolute).lstrip('/')
    if not path.resolve().is_relative_to(root):
        raise ValueError(f'installation path escapes target root: {absolute}')
    if path.exists() and not path.is_file():
        raise ValueError(f'installation destination is not a regular file: {absolute}')
    return path


def install_archives(paths, root):
    root = root.resolve()
    if root == Path('/') or not root.is_dir():
        raise ValueError('--install-root must be an existing image or staging directory, not /')
    packages = [(inspect_archive(path), path) for path in paths]
    names, targets = set(), set()
    for package, _ in packages:
        if package.name in names:
            raise ValueError(f'duplicate package: {package.name}')
        names.add(package.name)
        for _, target in package.files:
            if target in targets:
                raise ValueError(f'duplicate installation destination: {target}')
            targets.add(target)
            image_path(root, target)
        image_path(root, f'/etc/inpkg/system/{package.name}.conf')
    for package, path in packages:
        with tarfile.open(path, 'r:gz') as archive:
            for source, target in package.files:
                destination = image_path(root, target)
                destination.parent.mkdir(parents=True, exist_ok=True)
                temporary = destination.with_name(destination.name + '.inpkg.tmp')
                if temporary.is_symlink():
                    raise ValueError(f'unsafe temporary destination: {temporary}')
                try:
                    with archive.extractfile(str(source)) as stream, temporary.open('wb') as output:
                        shutil.copyfileobj(stream, output)
                    temporary.chmod(0o755 if target in package.binaries else 0o644)
                    temporary.replace(destination)
                finally:
                    temporary.unlink(missing_ok=True)
        record = image_path(root, f'/etc/inpkg/system/{package.name}.conf')
        record.parent.mkdir(parents=True, exist_ok=True)
        record.write_bytes(package.config)
        record.chmod(0o644)
    print('inpkg: installed ' + ', '.join(package.name for package, _ in packages))


def makefile(packages, directory, output):
    applications, lines = [], ['# Generated from source package.conf files.']
    for package, _ in packages:
        for source, target in package.files:
            if target not in package.binaries:
                continue
            if str(target).startswith('/sbin/sys/') and source.suffix == '.elf':
                continue
            name = source.name
            if not valid_name(name) or name in applications:
                raise ValueError(f'duplicate or invalid application name: {name}')
            applications.append(name)
            lines.append(f'APP_OUTPUT_{name} := {directory}/{package.name}/{source}')
    lines += ['APPLICATION_NAMES := ' + ' '.join(applications),
              'APPLICATION_PROGRAMS := $(foreach app,$(APPLICATION_NAMES),$(APP_OUTPUT_$(app)))']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build')
    parser.add_argument('--package-dir', default='packages')
    parser.add_argument('--user-dir', default='user')
    parser.add_argument('--lib-dir', default='lib')
    parser.add_argument('--install-root')
    parser.add_argument('--install', nargs='+', metavar='ARCHIVE.inpk')
    parser.add_argument('--makefile', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.install:
        if not args.install_root:
            raise ValueError('--install requires --install-root')
        install_archives([Path(path) for path in args.install], Path(args.install_root))
        return
    packages = collect(REPOSITORY / args.user_dir, REPOSITORY / args.lib_dir)
    if args.makefile:
        makefile(packages, args.package_dir, args.makefile)
        return
    directory = (REPOSITORY / args.package_dir).resolve()
    validate_sources(packages, directory)
    if args.check:
        print('inpkg: validated ' + ', '.join(package.name for package, _ in packages))
        return
    archives = [pack(package, conf, directory) for package, conf in packages]
    print('inpkg: packed ' + ', '.join(path.name for path in archives))
    if args.install_root:
        install_archives(archives, Path(args.install_root))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, tarfile.TarError, EOFError) as error:
        print(f'inpkg: {error}', file=sys.stderr)
        sys.exit(1)
