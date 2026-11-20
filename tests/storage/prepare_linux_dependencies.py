"""Prepare private M0 host dependencies; never install system packages.

Run on the isolated Linux copy. Debian libstdc++ is pinned to its published
SHA-256. openEuler RPMs must pass the host's existing RPM signature checks.
Package maintainer scripts are never executed. Outputs stay in tests/output.
"""
import hashlib
import argparse
import json
from pathlib import Path
import platform
import os
import shlex
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5/M0/linux-deps"
SYSROOT = OUTPUT / "sysroot"
REPO = "https://repo.openeuler.org/openEuler-24.03-LTS-SP4/everything/x86_64/Packages/"
PACKAGES = [
    ("https://deb.debian.org/debian/pool/main/g/gcc-14/libstdc++6_14.2.0-19_amd64.deb",
     "ab1fa05837aa7a92aae748fd07a18a35f7d18bb4a71c4724fe2bbf0e32089de0"),
    *[("https://deb.debian.org/debian/pool/main/g/gcc-14/" + name +
       "_14.2.0-19_amd64.deb", digest) for name, digest in (
        ("gcc-14-x86-64-linux-gnu", "a17ef039f1ba482051c3efb5c2c24070002e60dd0bd09fd456ec31481a11b725"),
        ("cpp-14-x86-64-linux-gnu", "ef274b5379f5f97fc71619d39ecc84d039d3e184570d207b909c90afaa5d79e0"),
        ("g++-14-x86-64-linux-gnu", "c66b009fedd340520279d1af2f91d0668e9f64f0c08fe62179ca1c75e556be58"),
        ("libgcc-14-dev", "ca6f2d36d96b19b3eb71405b0b80134d8c89380b02204a2512e5c58ceb090628"),
    )],
    *[(REPO + name, None) for name in (
        "qemu-system-x86_64-8.2.0-80.oe2403sp4.x86_64.rpm",
        "qemu-8.2.0-80.oe2403sp4.x86_64.rpm",
        "qemu-seabios-8.2.0-80.oe2403sp4.x86_64.rpm",
        "grub2-pc-modules-2.12-52.oe2403sp4.noarch.rpm",
        "dtc-1.7.2-3.oe2403sp4.x86_64.rpm",
        "libibverbs-50.0-47.oe2403sp4.x86_64.rpm",
        "librdmacm-50.0-47.oe2403sp4.x86_64.rpm",
        "libslirp-4.7.0-2.oe2403sp4.x86_64.rpm",
        "liburing-2.4-1.oe2403sp4.x86_64.rpm",
        "qatzip-libs-1.2.0-1.oe2403sp4.x86_64.rpm",
        "qatlib-24.02.0-1.oe2403sp4.x86_64.rpm",
        "intel-qpl-1.7.0-1.oe2403sp4.x86_64.rpm",
        "isl-0.24-2.oe2403sp4.x86_64.rpm",
    )],
]
GCC12_PACKAGES = [
    ("https://deb.debian.org/debian/pool/main/g/gcc-12/" + name +
     "_12.2.0-14+deb12u1_amd64.deb", digest) for name, digest in (
        ("gcc-12", "b46f33cc2ec245e435e043807038cecf4b201ef004800e9dfc1455240360e49d"),
        ("cpp-12", "fedbb98e877adde83c983c6071537ea25ac52b277ac6e4637d85025949ca1454"),
        ("g++-12", "68b182badae4ace7d472f1393c559d3532a95a2d70817498b10bb481d2627251"),
        ("libgcc-12-dev", "d720259380a84f2ffc6fe516eff5cbe9c8a005138e8d6a5748747ba4b3404a82"),
    )
]


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, capture_output=True, timeout=120, **kwargs)


def prepare_commands():
    commands = OUTPUT.parent / "host-bin"
    commands.mkdir(exist_ok=True)
    compat = SYSROOT / "compat"
    compat.mkdir(exist_ok=True)
    bzip = compat / "libbz2.so.1.0"
    if not bzip.is_symlink():
        bzip.symlink_to("/usr/lib64/libbz2.so.1.0.8")
    for name, target in (("grub-file", "/usr/bin/grub2-file"),
                         ("grub-install", "/usr/sbin/grub2-install")):
        link = commands / name
        if not link.is_symlink():
            link.symlink_to(target)
    for name, arguments in (
        ("grub-mkimage", ["/usr/bin/grub2-mkimage", "--directory=" + str(SYSROOT / "usr/lib/grub/i386-pc")]),
        ("grub-mkrescue", ["/usr/bin/grub2-mkrescue", "--directory=" + str(SYSROOT / "usr/lib/grub/i386-pc")]),
        ("qemu-system-x86_64", [str(SYSROOT / "usr/bin/qemu-system-x86_64"), "-L", str(SYSROOT / "usr/share/qemu")]),
    ):
        script = commands / name
        text = '#!/bin/sh\nexec ' + shlex.join(arguments) + ' "$@"\n'
        if script.exists():
            if script.read_text() != text:
                raise RuntimeError(f"refusing to overwrite existing command: {script}")
        else:
            with os.fdopen(os.open(script, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o755), "w") as stream:
                stream.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--with-gcc12", action="store_true", help="also unpack independent GCC 12 comparison compiler")
    args = parser.parse_args()
    if platform.system() != "Linux":
        raise SystemExit("requires isolated Linux copy")
    SYSROOT.mkdir(parents=True, exist_ok=True)
    cache = OUTPUT / "packages"
    cache.mkdir(exist_ok=True)
    records = []
    for url, expected in PACKAGES + (GCC12_PACKAGES if args.with_gcc12 else []):
        package = cache / url.rsplit("/", 1)[1]
        print("Preparing", package.name, flush=True)
        if not package.exists():
            with urllib.request.urlopen(url, timeout=60) as response:
                payload = response.read(128 * 1024 * 1024 + 1)
            if len(payload) > 128 * 1024 * 1024:
                raise RuntimeError("package exceeds 128 MiB limit")
            package.write_bytes(payload)
        digest = hashlib.sha256(package.read_bytes()).hexdigest()
        if expected and digest != expected:
            raise RuntimeError(f"SHA-256 mismatch: {package.name}")
        if package.suffix == ".rpm":
            signature = run(["rpm", "--checksig", str(package)]).stdout.decode()
            if "signatures OK" not in signature:
                raise RuntimeError(f"signature not verified: {signature}")
            payload = run(["rpm2cpio", str(package)]).stdout
            run(["cpio", "-idm", "--quiet", "--no-absolute-filenames",
                 "--no-preserve-owner"], input=payload, cwd=SYSROOT)
        else:
            signature = "published SHA-256 matched"
            member = next(name for name in run(["ar", "t", str(package)]).stdout
                          .decode().splitlines() if name.startswith("data.tar"))
            tarball = OUTPUT / member
            tarball.write_bytes(run(["ar", "p", str(package), member]).stdout)
            with tarfile.open(tarball) as archive:
                # Reject paths and links escaping this private extraction root.
                for item in archive.getmembers():
                    target = SYSROOT / item.name
                    if not target.resolve().is_relative_to(SYSROOT.resolve()):
                        raise RuntimeError("archive path escapes private root")
                    if item.islnk() or item.issym():
                        link = target.parent / item.linkname if item.issym() else SYSROOT / item.linkname
                        if not link.resolve().is_relative_to(SYSROOT.resolve()):
                            raise RuntimeError("archive link escapes private root")
                archive.extractall(SYSROOT)
        records.append({"url": url, "sha256": digest,
                        "bytes": package.stat().st_size, "verification": signature.strip()})
        (OUTPUT / "packages.json").write_text(json.dumps(records, indent=2))
    prepare_commands()
    print("Private dependency root:", SYSROOT, flush=True)


if __name__ == "__main__":
    main()
