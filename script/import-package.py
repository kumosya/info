#!/usr/bin/env python3
"""Validate and import an independently built application package."""
import argparse
from pathlib import Path
import shutil
import tempfile
from packages import inspect_archive


class PackageImporter:
    @staticmethod
    def main():
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('archive', type=Path)
        args = parser.parse_args()
        package = inspect_archive(args.archive)
        root = Path(__file__).resolve().parents[1] / 'packages/external'
        root.mkdir(parents=True, exist_ok=True)
        destination = root / (package.name + '.inpk')
        with tempfile.NamedTemporaryFile(dir=root, prefix=package.name + '.', suffix='.tmp', delete=False) as output:
            temporary = Path(output.name)
        try:
            shutil.copyfile(args.archive, temporary)
            inspect_archive(temporary)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
        print(f'Imported {package.name} {package.version}: {destination}')


if __name__ == '__main__':
    PackageImporter.main()
