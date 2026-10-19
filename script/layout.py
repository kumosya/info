#!/usr/bin/env python3
"""Create the shared system/user layout and normalize offline image ownership."""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


class Layout:
    system = (
        'bin', 'boot/grub', 'dev/pts', 'etc/xdg',
        'home', 'lib', 'media', 'mnt/cdrom', 'mnt/ext4', 'mnt/fat32',
        'proc', 'run/lock', 'run/user', 'sbin/sys', 'sys', 'tmp',
        'usr/bin', 'usr/sbin', 'usr/lib', 'usr/include', 'usr/share',
        'usr/local/share', 'var/cache', 'var/lib/inpkg', 'var/log', 'var/tmp',
    )
    user = ('.local', '.local/bin', '.local/lib', '.local/include',
            '.local/share', '.local/state/inpkg', '.config', '.cache')

    def __init__(self, root):
        self.root = Path(root).resolve(strict=True)
        if not self.root.is_dir() or self.root == Path('/'):
            raise ValueError('expected an installation root, not the host root')

    def inside(self, relative):
        path = self.root / relative.lstrip('/')
        if not path.resolve().is_relative_to(self.root):
            raise ValueError(f'path escapes installation root: {relative}')
        return path

    def accounts(self):
        records = []
        passwd = self.inside('etc/passwd')
        if passwd.is_file():
            for line in passwd.read_text().splitlines():
                fields = line.split(':')
                if len(fields) != 7 or not fields[5].startswith('/'):
                    continue
                uid, gid = int(fields[2]), int(fields[3])
                if uid < 0 or gid < 0:
                    raise ValueError('negative account identifier')
                home = self.inside(fields[5])
                if home == self.root:
                    raise ValueError('account home cannot be installation root')
                records.append((home, uid, gid))
        return records

    def merge(self, source, destination):
        # Both absolute targets are checked before every recursive move.
        self.inside(str(source.relative_to(self.root)))
        self.inside(str(destination.relative_to(self.root)))
        if not destination.exists():
            source.rename(destination)
            return
        if source.is_dir() and destination.is_dir() and not source.is_symlink() and not destination.is_symlink():
            for child in sorted(source.iterdir()):
                self.merge(child, destination / child.name)
            if not any(source.iterdir()):
                source.rmdir()
        else:
            print(f'layout: preserved migration conflict: {source}', file=sys.stderr)

    def create(self):
        for relative in self.system:
            directory = self.inside(relative)
            directory.mkdir(parents=True, exist_ok=True)
            directory.chmod(0o1777 if relative in ('tmp', 'var/tmp') else 0o755)
        legacy_database = self.inside('etc/inpkg/system')
        if legacy_database.exists():
            self.merge(legacy_database, self.inside('var/lib/inpkg'))
        for home, uid, gid in self.accounts():
            home.mkdir(parents=True, exist_ok=True)
            home.chmod(0o700 if uid == 0 else 0o755)
            if os.geteuid() == 0:
                os.chown(home, uid, gid)
            legacy, local = home / 'app', home / '.local'
            if legacy.exists():
                self.merge(legacy, local)
            for relative in self.user:
                directory = self.inside(str((home / relative).relative_to(self.root)))
                directory.mkdir(parents=True, exist_ok=True)
                directory.chmod(0o700)
                if os.geteuid() == 0:
                    os.chown(directory, uid, gid)

    def ownership(self, image):
        # mkfs -d otherwise copies the developer's host uid into system files.
        # debugfs works on the isolated regular image without root privileges.
        homes = sorted(self.accounts(), key=lambda entry: len(str(entry[0])), reverse=True)
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', suffix='.debugfs') as batch:
            for path in [self.root, *sorted(self.root.rglob('*'))]:
                if path.is_symlink():
                    raise ValueError(f'symbolic links require explicit image support: {path}')
                relative = '/' if path == self.root else '/' + path.relative_to(self.root).as_posix()
                uid = gid = 0
                for home, owner, group in homes:
                    if path.is_relative_to(home):
                        uid, gid = owner, group
                        break
                quoted = json.dumps(relative, ensure_ascii=False)
                batch.write(f'set_inode_field {quoted} uid {uid}\n')
                batch.write(f'set_inode_field {quoted} gid {gid}\n')
                # Preserve sticky/private modes even on filesystems with
                # reduced host chmod support, such as Windows-mounted paths.
                mode = 0o644
                if path.is_dir():
                    mode = 0o755
                    if relative in ('/tmp', '/var/tmp'):
                        mode = 0o1777
                    elif path in (home for home, _, _ in homes):
                        mode = 0o700 if uid == 0 else 0o755
                    elif any(path.is_relative_to(home) for home, _, _ in homes):
                        mode = 0o700
                    mode |= stat.S_IFDIR
                else:
                    if relative.startswith(('/bin/', '/sbin/', '/usr/bin/', '/usr/sbin/')) or relative == '/lib/ld-osinfo-x86-64.so':
                        mode = 0o755
                    elif relative == '/etc/shadow':
                        mode = 0o600
                    elif relative == '/etc/sudoers':
                        mode = 0o440
                    mode |= stat.S_IFREG
                batch.write(f'set_inode_field {quoted} mode 0{mode:o}\n')
            batch.flush()
            result = subprocess.run(['debugfs', '-w', '-f', batch.name, str(Path(image).resolve(strict=True))],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=True)
            if 'File not found' in result.stderr or 'Command not found' in result.stderr:
                raise RuntimeError(result.stderr)

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--root', required=True)
        parser.add_argument('--image')
        args = parser.parse_args()
        layout = cls(args.root)
        if args.image:
            layout.ownership(args.image)
        else:
            layout.create()


if __name__ == '__main__':
    Layout.main()
