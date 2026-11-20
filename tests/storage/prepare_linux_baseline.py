"""Package an isolated M0 source snapshot; exclude Git, outputs and secrets."""
from pathlib import Path
import hashlib
import io
import json
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5/M0/remote"
ARCHIVE = OUTPUT / "source.tar.gz"
ALLOWED = {"src", "include", "lib", "script", "tools", "packages", "docs", "tests"}
ROOT_FILES = {"configure", "Makefile", "README.md", "LICENSE", ".gitignore", ".clang-format"}

def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = {}
    with tarfile.open(ARCHIVE, "w:gz") as archive:
        for path in sorted(ROOT.rglob("*")):
            relative = path.relative_to(ROOT)
            if relative.parts[0] not in ALLOWED and relative.as_posix() not in ROOT_FILES:
                continue
            if path.is_symlink() or not path.is_file():
                continue
            if relative.parts[:2] == ("tests", "output") or "__pycache__" in relative.parts:
                continue
            payload = path.read_bytes()
            name = relative.as_posix()
            manifest[name] = hashlib.sha256(payload).hexdigest()
            info = tarfile.TarInfo(name)
            info.size = len(payload)
            info.mode = 0o755 if path.suffix == ".sh" or name in {
                "configure", "tools/inpkg", "tools/infoide", "tools/unzip", "tools/osinfo-host"
            } else 0o644
            archive.addfile(info, io.BytesIO(payload))
        payload = json.dumps(manifest, indent=2).encode()
        info = tarfile.TarInfo("tests/output/v0.1.5/M0/transfer-manifest.json")
        info.size = len(payload)
        info.mode = 0o644
        archive.addfile(info, io.BytesIO(payload))
    digest = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    (OUTPUT / "source.sha256").write_text(digest + "\n", encoding="ascii")
    print(json.dumps({"archive": str(ARCHIVE), "files": len(manifest),
                      "sha256": digest, "bytes": ARCHIVE.stat().st_size}))

if __name__ == "__main__":
    main()
