#!/usr/bin/env python3
"""Regression for byte-validated archives and preflight installation checks."""
import importlib.util
import io
import os
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('packages', Path(__file__).resolve().parents[1] / 'script/packages.py')
packages = importlib.util.module_from_spec(spec)
sys.modules['packages'] = packages
spec.loader.exec_module(packages)


class PackageCacheTests(unittest.TestCase):
    def test_newer_but_incorrect_archive_is_rebuilt(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'probe'
            source.mkdir()
            (source / 'probe').write_bytes(b'current executable')
            configuration = b'name=probe\nversion=1\nbin=/usr/bin/probe\ninstall=probe -> /usr/bin/probe\n'
            conf = source / 'package.conf'
            conf.write_bytes(configuration)
            package = packages.parse_config(configuration, conf)
            archive = packages.pack(package, conf, directory)
            with tarfile.open(archive, 'w:gz') as output:
                for name, data in [('package.conf', configuration), ('probe', b'partial executable')]:
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    output.addfile(info, io.BytesIO(data))
            os.utime(archive, (2000000000, 2000000000))
            packages.pack(package, conf, directory)
            with tarfile.open(archive, 'r:gz') as result:
                self.assertEqual(result.extractfile('probe').read(), b'current executable')
            install = directory / 'root'
            install.mkdir()
            packages.install_archives([archive], install)
            self.assertEqual((install / 'usr/bin/probe').read_bytes(), b'current executable')
            self.assertTrue((install / 'var/lib/inpkg/probe.conf').is_file())

    def test_corrupt_archive_does_not_modify_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            root = directory / 'root'
            root.mkdir()
            existing = root / 'existing'
            existing.write_text('preserved')
            archive = directory / 'bad.inpk'
            archive.write_bytes(b'not gzip')
            with self.assertRaises((OSError, ValueError, tarfile.TarError)):
                packages.install_archives([archive], root)
            self.assertEqual(list(root.iterdir()), [existing])
            self.assertEqual(existing.read_text(), 'preserved')


if __name__ == '__main__':
    unittest.main()
