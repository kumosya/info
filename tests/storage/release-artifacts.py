"""Verify and hash the ordinary release images and matching SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
TEST_OUTPUT = ROOT / "tests/output/v0.1.5"


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normal", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    normal, build, packages, out = (
        item.resolve() for item in (args.normal, args.build, args.packages, args.output))
    if (not normal.is_relative_to(TEST_OUTPUT) or not out.is_relative_to(TEST_OUTPUT) or
            not build.is_relative_to(ROOT) or not packages.is_relative_to(ROOT)):
        parser.error("use build and image artifacts from the isolated checkout")
    baseline = json.loads((normal / "result.json").read_text())
    if (baseline["status"] != "PASS" or baseline.get("version") != args.version or
            {case["mode"] for case in baseline["boots"] if case["status"] == "PASS"} !=
            {"disk-normal", "iso-normal"}):
        parser.error("requires matching ordinary disk and ISO boot evidence")
    out.mkdir(parents=True, exist_ok=False)
    disk, iso, sdk = normal / "osinfo.img", normal / "osinfo.iso", build / "sdk"
    if digest(iso) != baseline["inputs_sha256"]["osinfo.iso"]:
        raise RuntimeError("ISO changed after boot verification")
    with disk.open("rb") as source, (out / "disk-root.ext3").open("wb") as target:
        mbr = source.read(512)
        start = int.from_bytes(mbr[454:458], "little") * 512
        count = int.from_bytes(mbr[458:462], "little") * 512
        if (len(mbr) != 512 or mbr[510:512] != b"\x55\xaa" or start < 512 or
                not count or start + count > disk.stat().st_size):
            raise RuntimeError("invalid root partition in release image")
        source.seek(start)
        for position in range(0, count, 1024 * 1024):
            size = min(1024 * 1024, count - position)
            data = source.read(size)
            if len(data) != size:
                raise RuntimeError("short read while extracting release partition")
            if any(data):
                target.write(data)
            else:
                target.seek(size, 1)
        target.truncate(count)
    iso_root = out / "iso-root.ext3"
    subprocess.run(["xorriso", "-osirrox", "on", "-indev", str(iso),
                    "-extract", "/boot/rootfs.ext3", str(iso_root)], check=True)

    metadata = json.loads((sdk / "sdk.json").read_text())
    for name, expected in metadata["files"].items():
        member = (sdk / name).resolve()
        if not member.is_relative_to(sdk.resolve()) or digest(member) != expected:
            raise RuntimeError("SDK digest mismatch: " + name)
    libraries = (("libos-c++", "libos-c++.so"),
                 ("libc++", "libc.so"), ("libc++", "libc++.so"))
    for package, name in libraries:
        if digest(packages / package / name) != digest(sdk / "lib" / name):
            raise RuntimeError("package and SDK library mismatch: " + name)
    for medium in ("disk", "iso"):
        fs = out / f"{medium}-root.ext3"
        release = subprocess.check_output(
            ["debugfs", "-R", "cat /lib/version.1", str(fs)], text=True).strip()
        if release != args.version:
            raise RuntimeError(f"{medium} release mismatch: {release!r}")
        installed = [(f"/usr/lib/{name}", name) for _, name in libraries]
        installed.append(("/lib/ld-osinfo-x86-64.so", "ld-osinfo-x86-64.so"))
        for deployed, sdk_name in installed:
            extracted = out / f"{medium}-{sdk_name}"
            subprocess.run(["debugfs", "-R", f"dump {deployed} {extracted}", str(fs)],
                           check=True)
            if digest(extracted) != digest(sdk / "lib" / sdk_name):
                raise RuntimeError(f"{medium} and SDK differ: {sdk_name}")
    paths = (disk, iso, sdk / "sdk.json")
    (out / "manifest.json").write_text(json.dumps(
        {"version": args.version, "sdk_files": len(metadata["files"]),
         "sha256": {str(path.relative_to(ROOT)): digest(path) for path in paths}},
        indent=2) + "\n")
    print("PASS release version and deployed runtime match the SDK on IMG and ISO")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
