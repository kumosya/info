"""验证重复配置保留本地新包，更新 ZIP 或恢复缺失成员时重新导入。"""
from pathlib import Path
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[2]
host = root / "tools/osinfo-host"
package_tool = root / "tools/inpkg"
with tempfile.TemporaryDirectory(prefix="osinfo-bundle-import-") as directory:
    temporary = Path(directory)
    source = temporary / "source"
    source.mkdir()
    (source / "package.conf").write_text(
        "name=bundle-import-test\nversion=1\ninstall=note -> /usr/share/bundle-import-test/note\n")
    packages = temporary / "packages"
    packages.mkdir()
    build = temporary / "build"
    subprocess.run([str(host), "defaults", "--build", str(build)], cwd=root, check=True)
    variants = []
    for content in ("old\n", "new\n"):
        (source / "note").write_text(content)
        destination = temporary / (content.strip() + ".inpk")
        subprocess.run([str(package_tool), "pack", str(source), "--manifest",
            str(source / "package.conf"), "--output", str(destination)], cwd=root, check=True)
        variants.append(destination.read_bytes())

    archive = packages / "bundle.zip"
    def bundle(comment=b""):
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("bundle-import-test.inpk", variants[0])
            output.comment = comment

    def prepare():
        subprocess.run([str(host), "prepare", "--argument", "base", "--build", str(build),
            "--objects", str(temporary / "objects"), "--packages", str(packages)], cwd=root, check=True)

    installed = packages / "bundle-import-test.inpk"
    bundle()
    prepare()
    assert installed.read_bytes() == variants[0]
    installed.write_bytes(variants[1])
    prepare()
    assert installed.read_bytes() == variants[1], "unchanged ZIP overwrote local package"
    bundle(b"updated bundle")
    prepare()
    assert installed.read_bytes() == variants[0], "updated ZIP was not imported"
    installed.unlink()
    prepare()
    assert installed.read_bytes() == variants[0], "missing package was not restored"
print("bundle import PASS")
