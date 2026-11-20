"""Check preflight failure reporting and the read-only default invocation."""
from pathlib import Path
import subprocess
import sys

script = Path(__file__).with_name("m0_baseline.py")
root = script.resolve().parents[2]
output = root / "tests/output/v0.1.5/M0"

def snapshot():
    return {str(p): (p.stat().st_size, p.stat().st_mtime_ns)
            for p in output.rglob("*") if p.is_file()} if output.exists() else {}

before = snapshot()
missing = subprocess.run([sys.executable, str(script), "--cxx",
                          "__osinfo_missing_compiler__"],
                         capture_output=True, text=True, timeout=30)
if missing.returncode != 2 or "BLOCKED host-cxx" not in missing.stdout:
    raise SystemExit("FAIL missing compiler must return BLOCKED/2")
print("PASS missing-compiler-exit-code")
if snapshot() != before:
    raise SystemExit("FAIL default preflight modified output files")
print("PASS default-preflight-read-only")
invalid = subprocess.run([sys.executable, str(script), "--profile", "runtime",
                          "--run-host"], capture_output=True, text=True, timeout=30)
if invalid.returncode != 2 or "requires --profile host" not in invalid.stderr:
    raise SystemExit("FAIL incompatible profile flags must fail before execution")
print("PASS incompatible-profile-flags")
