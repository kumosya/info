"""Run M6 verification in an isolated Linux checkout with prepared M0 tools."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5"
ENV = ["sh", "tests/storage/linux_m0_env.sh"]
HOST_BOOT_SECONDS = 180


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--version", help="also verify release metadata in the images")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", args.tag):
        parser.error("use a lowercase tag made of letters, digits and hyphens")
    tag = args.tag
    out = OUTPUT / "M6" / tag
    out.mkdir(parents=True, exist_ok=False)
    result = {"status": "BLOCKED", "commands": []}

    def run(name, argv, timeout=300):
        argv = [str(value) for value in argv]
        print("RUN", name, flush=True)
        entry = {"name": name, "argv": argv}
        result["commands"].append(entry)
        with (out / f"{name}.log").open("w") as log:
            completed = subprocess.run(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT,
                                       timeout=timeout)
        entry["exit_code"] = completed.returncode
        if completed.returncode:
            raise RuntimeError(f"{name} failed, see {out / (name + '.log')}")

    try:
        private = OUTPUT / "M0/linux-deps/sysroot/usr/bin/x86_64-linux-gnu-g++-14"
        missing = [name for name in ("g++", "make", "mkfs.ext3", "sfdisk")
                   if not shutil.which(name)]
        if not private.is_file():
            missing.append(str(private))
        if missing:
            raise RuntimeError("missing prerequisites: " + ", ".join(missing))
        result["status"] = "FAIL"
        for name in ("statistics-contract", "request-lifecycle", "flush-contract",
                     "block-contract", "partition-baseline"):
            binary = out / name
            run(name + "-compile", ["g++", "-std=c++20", "-Wall", "-Wextra",
                                    "-Werror", "-pthread", "-Iinclude", "-Iinclude/os-c++",
                                    f"tests/storage/{name}.cc", "-o", binary], 60)
            run(name, [binary], 20)
        run("baseline-profile", [sys.executable, "tests/storage/runtime-baseline-selftest.py"], 30)
        run("configure", [sys.executable, "tests/storage/configure_private_toolchain.py",
                          "--compiler", "gcc14", "--tag", tag])
        build, packages = f"build-{tag}", f"packages-{tag}"
        # The install layout command still reads the default build path.
        (ROOT / "build").symlink_to(build, target_is_directory=True)
        run("build-sdk", ENV + ["make", f"CONFIGURE_BUILD={build}", "-j2",
                                "all", "check", "sdk"], 600)
        base = ENV + [sys.executable, "tests/storage/runtime_baseline.py", "--build",
                      build, "--packages", packages, "--prepare", "--run",
                      "--seconds", str(HOST_BOOT_SECONDS)]
        if args.version:
            base += ["--version", args.version]
        base += ["--name"]
        run("storage-runtime", base + [tag + "-storage", "--extra-4kn"], 720)
        run("timeout", ENV + [sys.executable, "tests/storage/virtio-timeout.py",
                              OUTPUT / "M0" / (tag + "-storage") / "result.json",
                              out / "timeout", "--seconds", str(HOST_BOOT_SECONDS)],
            HOST_BOOT_SECONDS + 60)
        run("normal-boot", base + [tag + "-normal", "--normal", "--smp", "1"], 480)
        result["status"] = "PASS"
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result["error"] = str(error)
        print(result["status"], error, flush=True)
    finally:
        (out / "result.json").write_text(json.dumps(result, indent=2))
    print(result["status"], out, flush=True)
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[result["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
