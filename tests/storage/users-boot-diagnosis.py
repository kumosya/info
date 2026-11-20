"""Boot a disposable copy of a persistence image with a diagnostic init module."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests/output/v0.1.5"


def replace_once(text, source, target):
    if text.count(source) != 1:
        raise RuntimeError(f"diagnostic source anchor changed: {source.splitlines()[0]}")
    return text.replace(source, target, 1)


def overlay(source, errors_only):
    diagnostic = r'''    static void usersDiagnostic(const Unit& unit, const char* stage,
                                int64_t ipc = 0, int64_t value = 0,
                                bool inspect = false) {
        if (!equal(unit.status.name, "users")) return;
        os::wire::TaskInfo task{};
        os::wire::ThreadInfo thread{};
        os::wire::ExitInfo exit{};
        const int info = inspect ? os::detail::TaskQuery().getInfo(unit.task, task) : -1;
        const int threadStatus = info ? -1 : os::detail::TaskQuery().threadInfo(
            task.id, task.mainThreadId, thread);
        const int exitStatus = info ? -1 : os::detail::TaskQuery().exitInfo(task.id, exit);
        char line[280] = "[USERS DIAG] ";
        size_t used = 13;
        auto append = [&](const char* text) {
            while (*text && used + 2 < sizeof(line)) line[used++] = *text++;
        };
        auto field = [&](const char* key, uint64_t value) {
            append(key);
            char hex[17]{};
            for (unsigned index = 0; index < 16; ++index)
                hex[15 - index] = "0123456789abcdef"[(value >> (index * 4)) & 15U];
            const char* digits = hex;
            while (*digits == '0' && digits[1]) ++digits;
            append(digits);
        };
        append(stage);
        field(" pid=", unit.status.pid);
        field(" ipc=", ipc);
        field(" value=", value);
        if (inspect) {
            field(" query=", info);
            field(" taskState=", task.state);
            field(" threadQuery=", threadStatus);
            field(" threadState=", thread.state);
            field(" cpuNs=", task.cpuNanoseconds);
            field(" exitQuery=", exitStatus);
            field(" signal=", exit.signal);
            field(" rip=", exit.rip);
        }
        append("\n");
        os::Sys().log(line);
    }

'''
    source = replace_once(source, "    bool waitReady(Unit& unit) {",
                          diagnostic + "    bool waitReady(Unit& unit) {\n"
                          "        usersDiagnostic(unit, \"wait begin\");\n"
                          "        int lookupStatus = 0;\n"
                          "        int64_t lookupValue = 0;")
    source = replace_once(
        source, "            if (!os::callService(registry_, request, reply) && !reply.data[0] &&",
        "            lookupStatus = os::callService(registry_, request, reply);\n"
        "            lookupValue = static_cast<int64_t>(reply.data[0]);\n"
        "            if (!lookupStatus && !reply.data[0] &&")
    source = replace_once(source, "                return owned;",
                          "                usersDiagnostic(unit, owned ? \"ready\" : \"wrong publisher\",\n"
                          "                                lookupStatus, reply.data[2]);\n"
                          "                return owned;")
    source = replace_once(
        source, "            if (os::detail::TaskControl().getState(unit.task, code))\n"
                "                return false;",
        "            const int state = os::detail::TaskControl().getState(unit.task, code);\n"
        "            if (state) {\n"
        "                usersDiagnostic(unit, \"task state before ready\", state, code, true);\n"
        "                return false;\n"
        "            }")
    source = replace_once(source, "        } while (os::Sys().readMonotonicTime() < deadline);\n"
                                  "        return false;",
                          "        } while (os::Sys().readMonotonicTime() < deadline);\n"
                          "        usersDiagnostic(unit, \"readiness expired\", lookupStatus, lookupValue, true);\n"
                          "        return false;")
    source = replace_once(source, "    bool start(Unit& unit) {",
                          "    bool start(Unit& unit) {\n"
                          "        usersDiagnostic(unit, \"start\", unit.status.builtin, unit.task);")
    source = replace_once(
        source, "        if (!unit.task || static_cast<int64_t>(unit.task) < 0) {",
        "        usersDiagnostic(unit, \"spawn returned\", unit.task);\n"
        "        if (!unit.task || static_cast<int64_t>(unit.task) < 0) {")
    source = replace_once(
        source, "        if (os::detail::TaskQuery().getInfo(unit.task, info)) {",
        "        const int infoStatus = os::detail::TaskQuery().getInfo(unit.task, info);\n"
        "        usersDiagnostic(unit, \"getInfo returned\", infoStatus, info.id);\n"
        "        if (infoStatus) {")
    source = replace_once(source, "    bool failed(Unit& unit, int code) {",
                          "    bool failed(Unit& unit, int code) {\n"
                          "        usersDiagnostic(unit, \"failed\", code);")
    if errors_only:
        for line in ('        usersDiagnostic(unit, "start", unit.status.builtin, unit.task);\n',
                     '        usersDiagnostic(unit, "wait begin");\n'):
            source = replace_once(source, line, "")
        source = replace_once(
            source, '                usersDiagnostic(unit, owned ? "ready" : "wrong publisher",\n'
                    '                                lookupStatus, reply.data[2]);',
            '                if (!owned)\n'
            '                    usersDiagnostic(unit, "wrong publisher", lookupStatus, reply.data[2]);')
        source = replace_once(
            source, '        usersDiagnostic(unit, "spawn returned", unit.task);\n'
                    '        if (!unit.task || static_cast<int64_t>(unit.task) < 0) {',
            '        if (!unit.task || static_cast<int64_t>(unit.task) < 0) {\n'
            '            usersDiagnostic(unit, "spawn failed", unit.task);')
        source = replace_once(
            source, '        usersDiagnostic(unit, "getInfo returned", infoStatus, info.id);\n'
                    '        if (infoStatus) {',
            '        if (infoStatus) {\n'
            '            usersDiagnostic(unit, "getInfo failed", infoStatus, info.id);')
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--boot-baseline", type=Path, help="baseline containing the boot module tree")
    parser.add_argument("--attempts", type=int, default=1,
                        help="repeat successful boots from the unchanged snapshot")
    parser.add_argument("--disk-overlay", action="store_true",
                        help="load diagnostic init from a disposable raw hard disk")
    parser.add_argument("--fresh-disk", action="store_true",
                        help="restore the pristine root volume and run create, then persist")
    parser.add_argument("--errors-only", action="store_true",
                        help="omit successful startup logs to reduce timing changes")
    args = parser.parse_args()
    if args.attempts < 1:
        parser.error("--attempts must be positive")
    if args.disk_overlay and args.attempts != 1:
        parser.error("--disk-overlay runs once because the test writes to the copy")
    if args.fresh_disk and not args.disk_overlay:
        parser.error("--fresh-disk requires --disk-overlay")
    baseline, out = args.baseline.resolve(), args.output.resolve()
    boot = args.boot_baseline.resolve() if args.boot_baseline else baseline
    if not all(path.is_relative_to(OUTPUT) for path in (baseline, boot, out)):
        parser.error("diagnostic inputs and outputs must be in the test output tree")
    result = json.loads((baseline / "result.json").read_text())
    version = result.get("version")
    case = next(item for item in result["boots"] if item["mode"] == "disk-persist")
    out.mkdir(parents=True, exist_ok=False)
    header = out / "include/service/common/lifecycle.h"
    header.parent.mkdir(parents=True)
    header.write_text(overlay((ROOT / "include/service/common/lifecycle.h").read_text(),
                              args.errors_only))
    compile_record = result["commands"][0]["argv"]
    sdk_start = next(Path(value) for value in compile_record if value.endswith("/sdk/lib/start.o"))
    build = sdk_start.parents[2]
    tag = build.name.removeprefix("build-")
    compiler = compile_record[0]
    flags = ["-std=c++20", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-fno-exceptions", "-fno-rtti", "-fno-use-cxa-atexit", "-fno-threadsafe-statics",
             "-fPIC", "-m64", "-mcmodel=small", "-mno-red-zone", "-nostdinc", "-nostdinc++", "-Os"]
    include = [out / "include", *(ROOT / "include" / name for name in
                                 ("", "stdc", "stdc++", "posix", "os-c++")), build / "generated"]
    obj = out / "init.o"
    init = out / "init.cc"
    init.write_text(replace_once((ROOT / "src/init/main.cc").read_text(),
                                 "[INIT] Ring 3 init started",
                                 "[INIT] Ring 3 init started (USERS DIAG overlay)"))
    subprocess.run([compiler, *flags, *(f"-I{path}" for path in include), "-c",
                    init, "-o", obj], cwd=ROOT, check=True)
    cd = out / "cd"
    shutil.copytree(boot / "cd", cd, ignore=shutil.ignore_patterns("rootfs.ext3"))
    grub = cd / "boot/grub/grub.cfg"
    grub.write_text(replace_once(grub.read_text(),
                                 "    module2 /boot/rootfs.ext3 rootfs\n", ""))
    subprocess.run([compiler, *flags, "-no-pie", "-nostdlib", "-nostartfiles",
                    "-nodefaultlibs", "-Wl,--build-id=none,--hash-style=sysv",
                    "-Wl,-T,script/boot-service.lds", "-Wl,-E,--no-dynamic-linker",
                    "-o", cd / "boot/init.elf", obj, f"obj-{tag}/lib/os/service_start.o",
                    "-Wl,--no-as-needed", f"packages-{tag}/libos-c++/libos-c++.so", "-lgcc"],
                   cwd=ROOT, check=True)
    marker = "[INIT] Ring 3 init started (USERS DIAG overlay)"
    if marker.encode() not in (cd / "boot/init.elf").read_bytes():
        raise RuntimeError("diagnostic init does not contain its startup marker")
    argv = list(case["argv"])
    if args.disk_overlay:
        disk = prepare_disk(baseline, out, cd, grub, args.fresh_disk)
        index = argv.index(f"file={baseline / 'osinfo.img'},if=none,format=raw,id=osdisk")
        argv[index] = f"file={disk},if=none,format=raw,id=osdisk"
    else:
        iso = out / "init-diagnostic.iso"
        subprocess.run(["grub-mkrescue", "-o", iso, cd], cwd=ROOT, check=True)
        disk = argv.index("virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0")
        argv[disk] = argv[disk].removesuffix(",bootindex=0")
        argv[argv.index("-boot") + 1] = "strict=on"
        argv += ["-drive", f"file={iso},if=none,format=raw,media=cdrom,readonly=on,id=diagcd",
                 "-device", "ide-cd,drive=diagcd,bootindex=0", "-snapshot"]

    modes = ["disk-create", "disk-persist"] if args.fresh_disk else ["disk-persist"] * args.attempts
    for attempt, mode in enumerate(modes, 1):
        run = out
        if len(modes) > 1:
            run = out / (mode if args.fresh_disk else f"attempt-{attempt}")
            run.mkdir()
        print(f"Diagnostic boot {attempt}/{len(modes)}: {mode}", flush=True)
        result = run_boot(argv, run, marker, mode, version)
        if not result["overlay_loaded"] or not result["probe_passed"]:
            return 1
        if not args.errors_only and not result["diagnostics"]:
            return 1
    return 0


def prepare_disk(baseline, out, cd, grub, fresh):
    original = baseline / "osinfo.img"
    partitions = json.loads(subprocess.check_output(["sfdisk", "--json", original]))["partitiontable"]
    volumes = partitions["partitions"]
    if (partitions["label"] != "dos" or partitions["sectorsize"] != 512 or len(volumes) != 1
            or volumes[0]["start"] != 2048 or volumes[0]["size"] != 262144):
        raise RuntimeError("unexpected persistence fixture layout")

    filesystem = out / "disk-root.ext3"
    source = baseline / "root.ext3" if fresh else original
    skip = "skip=0" if fresh else "skip=1"
    subprocess.run(["dd", f"if={source}", f"of={filesystem}", "bs=1M", skip,
                    "count=128", "conv=sparse", "status=none"], check=True)
    if filesystem.stat().st_size != volumes[0]["size"] * partitions["sectorsize"]:
        raise RuntimeError("persistence fixture root volume is incomplete")
    config = out / "diagnostic.cfg"
    config.write_text(replace_once(grub.read_text(), "/boot/init.elf init",
                                  "/boot/init-diag.elf init"))
    for host, guest in ((cd / "boot/init.elf", "/boot/init-diag.elf"),
                        (config, "/boot/grub/diagnostic.cfg")):
        subprocess.run(["debugfs", "-w", "-R", f'write "{host}" {guest}', filesystem], check=True)
        contents = subprocess.check_output(["debugfs", "-R", f"cat {guest}", filesystem])
        if contents != host.read_bytes():
            raise RuntimeError(f"failed to install {guest} into the diagnostic copy")

    early = out / "early.cfg"
    early.write_text("set root=(hd0,msdos1)\nset prefix=($root)/boot/grub\n"
                     "normal ($root)/boot/grub/diagnostic.cfg\n")
    subprocess.run(["grub-mkimage", "-O", "i386-pc", "-p", "/boot/grub", "-c", early,
                    "-o", out / "core.img", "biosdisk", "part_msdos", "ext2", "normal",
                    "multiboot2", "configfile", "all_video", "gfxterm", "font"], check=True)
    shutil.copy2(baseline / "boot.img", out / "boot.img")

    disk = out / "osinfo.img"
    with disk.open("xb") as stream:
        stream.truncate(original.stat().st_size)
    for source, extra in ((original, ["count=1"]), (filesystem, ["seek=1"]),
                          (original, ["skip=129", "seek=129"])):
        subprocess.run(["dd", f"if={source}", f"of={disk}", "bs=1M", *extra,
                        "conv=sparse,notrunc", "status=none"], check=True)
    subprocess.run(["tools/osinfo-host", "image", "--argument", "embed-grub",
                    "--output", out, "--image", disk], cwd=ROOT, check=True)
    return disk


def run_boot(argv, out, marker, mode, version):
    failure = "[SERVICE] startup/exit failure: users"
    failure_markers = (failure, "[M0 STORAGE] FAIL", "unrecoverable kernel exception")
    expected = ["[M0 STORAGE] PASS", "[M0 STORAGE] BACKEND VirtioBlock",
                "[M0 STORAGE] FILE " + ("created" if mode == "disk-create" else "persisted")]
    if version:
        expected.append(f"[RELEASE] {version} verified")
    if any("drive=disk4kn," in value for value in argv):
        expected.append("[M0 STORAGE] 4Kn identified, I/O rejected")
    log = out / "serial.log"
    started = time.monotonic()
    with log.open("w") as stream:
        with subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.DEVNULL,
                              stdout=stream, stderr=subprocess.STDOUT) as process:
            try:
                deadline = started + 150
                while process.poll() is None and time.monotonic() < deadline:
                    text = log.read_text(errors="replace")
                    if (all(value in text for value in expected) or
                            any(value in text for value in failure_markers)):
                        break
                    time.sleep(0.5)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
    text = log.read_text(errors="replace")
    diagnostics = [line for line in text.splitlines() if "[USERS DIAG]" in line]
    passed = all(value in text for value in expected) and not any(
        value in text for value in failure_markers)
    result = {"argv": argv, "diagnostics": diagnostics, "overlay_loaded": marker in text,
              "probe_passed": passed,
              "users_failed": failure in text,
              "duration_seconds": round(time.monotonic() - started, 2)}
    (out / "result.json").write_text(json.dumps(result, indent=2))
    for line in diagnostics:
        print(line, flush=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
