"""Configure a fresh baseline using a pinned private compiler, not PATH gcc.

Run after prepare_linux_dependencies.py (--with-gcc12 for GCC 12).
No package installation or system configuration is performed.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", choices=("gcc12", "gcc14"), default="gcc14")
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", args.tag):
        parser.error("tag must use lowercase letters, digits and hyphens")
    private = OUTPUT / "M0/linux-deps/sysroot"
    version = args.compiler.removeprefix("gcc")
    cc = private / f"usr/bin/x86_64-linux-gnu-gcc-{version}"
    cpp = private / f"usr/bin/x86_64-linux-gnu-g++-{version}"
    if not cc.is_file() or not cpp.is_file():
        parser.error("prepare the selected private compiler first")
    build, objects, packages = (f"{prefix}-{args.tag}" for prefix in ("build", "obj", "packages"))
    if any((ROOT / path).exists() for path in (build, objects, packages)):
        parser.error("output already exists; select a fresh tag to avoid stale/partial objects")
    inputs = sorted(p for p in (ROOT / "packages").iterdir() if p.suffix in {".inpk", ".zip"} and p.is_file())
    if not inputs:
        parser.error("no package archives found")
    (ROOT / packages).mkdir()
    for source in inputs:
        shutil.copy2(source, ROOT / packages / source.name)
    output = OUTPUT / "toolchain-fix" / args.tag
    output.mkdir(parents=True, exist_ok=False)
    command = ["sh", "tests/storage/linux_m0_env.sh", "env", f"CC={cc}", f"CPP={cpp}",
               "./configure", "--build-dir", build, "--object-dir", objects, "--package-dir", packages]
    with (output / "configure.log").open("w") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    (output / "configuration.json").write_text(json.dumps({"compiler": args.compiler,
        "command": command, "exit_code": result.returncode}, indent=2))
    if not result.returncode:
        print(f"Configured pinned {args.compiler}; run:")
        print(f"sh tests/storage/linux_m0_env.sh make CONFIGURE_BUILD={build} -j2 all check sdk")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
