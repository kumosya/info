"""报告批次接口的实际链接符号；强符号仅表示接线完成，不代表语义验收。"""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--require", nargs="*", type=int, default=[], help="这些批次必须全部提供强符号")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]

def symbols(path, dynamic=False):
    command = ["nm", "-g", "--defined-only"]
    if dynamic:
        command.append("-D")
    output = subprocess.check_output([*command, str(path)], text=True)
    result = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and len(fields[1]) == 1:
            result.setdefault(fields[2], set()).add(fields[1])
    return result

archive = symbols(root / "build/lib/libc.a")
shared_path = root / "packages/libc++/libc.so"
shared = symbols(shared_path, True)
failed = False
for batch in range(3, 11):
    document = next((root / "docs/prompt").glob(f"posix-batch-{batch:02}-*-outline.md"))
    names = re.findall(r"^#### `([a-zA-Z_][a-zA-Z0-9_]*)\(\)`", document.read_text(encoding="utf-8"), re.M)
    missing = []
    for name in names:
        # 同一归档可能还包含弱 fallback，必须至少含一个全局强实现。
        if not (archive.get(name, set()) & {"T", "D"}) or not (shared.get(name, set()) & {"T", "D"}):
            missing.append(name)
    print(f"B{batch:02}: strong {len(names) - len(missing)}/{len(names)}")
    if missing:
        print("  missing/weak: " + ", ".join(missing))
    if batch in args.require and missing:
        failed = True

sdk = root / "build/sdk/lib/libc.so"
same = hashlib.sha256(shared_path.read_bytes()).digest() == hashlib.sha256(sdk.read_bytes()).digest()
print("SDK libc.so matches: " + str(same))
raise SystemExit(1 if failed or not same else 0)
