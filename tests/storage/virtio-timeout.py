"""Suspend a read on an extra QEMU disk and verify the real stop/reclaim path."""
import argparse
from contextlib import ExitStack, contextmanager
import json
from pathlib import Path
import select
import socket
import subprocess
from threading import Event, Thread
import time


@contextmanager
def launch(command, log):
    with log.open("w") as stream:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=stream, stderr=subprocess.STDOUT)
        try:
            yield process
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


@contextmanager
def connect_monitor(process, address, log, deadline):
    while not address.exists() and process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.02)
    with socket.socket(socket.AF_UNIX) as client, log.open("w") as transcript:
        client.settimeout(6)
        client.connect(str(address))
        with client.makefile("rwb", buffering=0) as stream:
            greeting = stream.readline()
            if not greeting:
                raise RuntimeError("QMP disconnected during handshake")
            transcript.write(greeting.decode())

            def qmp(command, **arguments):
                message = json.dumps({"execute": command, "arguments": arguments}) + "\n"
                transcript.write(message)
                transcript.flush()
                stream.write(message.encode())
                while True:
                    line = stream.readline()
                    if not line:
                        raise RuntimeError("QMP disconnected")
                    transcript.write(line.decode())
                    transcript.flush()
                    response = json.loads(line)
                    if "error" in response:
                        raise RuntimeError(str(response["error"]))
                    if "return" in response:
                        return response["return"]

            qmp("qmp_capabilities")
            yield qmp


@contextmanager
def delay_reply(front, back, armed, suspended, resume):
    # Relay the NBD stream unchanged. After the guest's ready marker, hold
    # one scratch-disk reply outside QEMU until the guest reports a timeout.
    finished = Event()
    errors = []
    with socket.socket(socket.AF_UNIX) as listener:
        listener.bind(str(front))
        listener.listen(1)
        listener.settimeout(0.2)

        def relay():
            try:
                while not finished.is_set():
                    try:
                        guest, _ = listener.accept()
                        break
                    except TimeoutError:
                        continue
                else:
                    return
                with guest, socket.socket(socket.AF_UNIX) as backend:
                    backend.connect(str(back))
                    peers = {guest: backend, backend: guest}
                    while not finished.is_set():
                        readable, _, _ = select.select(peers, [], [], 0.2)
                        for source in readable:
                            data = source.recv(65536)
                            if not data:
                                return
                            if source is backend and armed.is_set() and not suspended.is_set():
                                suspended.set()
                                resume.wait()
                                if finished.is_set():
                                    return
                            peers[source].sendall(data)
            except OSError as error:
                if not finished.is_set():
                    errors.append(str(error))

        worker = Thread(target=relay, daemon=True)
        worker.start()
        try:
            yield errors
        finally:
            finished.set()
            resume.set()
            worker.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="m5-storage result.json")
    parser.add_argument("output", type=Path, help="new directory under tests/output")
    parser.add_argument("--seconds", type=int, default=100,
                        help="host wait budget for boot and the fault scenario")
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error("seconds must be between 10 and 600")
    report = json.loads(args.baseline.read_text())
    if report["status"] != "PASS":
        parser.error("requires a passing disposable image")
    command = list(next(case["argv"] for case in report["boots"]
                        if case["mode"] == "disk-persist"))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    disk = output / "timeout.img"
    with disk.open("wb") as stream:
        stream.truncate(16 * 1024 * 1024)
    monitor = output / "qmp.sock"
    backend_monitor = output / "backend.sock"
    export = output / "nbd.sock"
    proxy = output / "scratch.sock"
    # Resetting VirtIO can drain pending I/O in the guest QEMU process.
    # Serve the scratch disk independently so the relay can release its reply.
    backend_command = [command[0], "-machine", "none", "-nodefaults", "-display", "none",
                       "-serial", "none", "-monitor", "none", "-qmp",
                       f"unix:{backend_monitor},server=on,wait=off",
                       "-drive", f"file={disk},if=none,format=raw,id=timeoutdisk,readonly=on"]
    command += ["-S", "-qmp", f"unix:{monitor},server=on,wait=off",
                "-drive", f"file=nbd:unix:{proxy}:exportname=timeoutdisk,if=none,format=raw,id=timeoutdisk,readonly=on",
                "-device", "virtio-blk-pci,drive=timeoutdisk,disable-legacy=on"]
    serial = output / "serial.log"
    backend_log = output / "backend.log"
    stop_marker = "[STORAGE] VirtIO submitted request timed out"
    expected = (stop_marker, "[DEVICE] DMA domain stopped",
                "[DEVICE] DMA domain reclaimed", "[M5 STORAGE] scratch disk offline",
                "[M6 STORAGE] timeout statistics passed",
                "[M5 STORAGE] full completion queue passed", "[M0 STORAGE] PASS")
    failure_markers = ("[M0 STORAGE] FAIL", "[SERVICE] startup/exit failure: users",
                       "unrecoverable kernel exception")
    armed = Event()
    suspended = Event()
    resume = Event()
    resumed = False
    controller_error = None
    relay_errors = []
    host_timeout_stage = None
    try:
        with ExitStack() as stack:
            backend = stack.enter_context(launch(backend_command, backend_log))
            control = stack.enter_context(connect_monitor(
                backend, backend_monitor, output / "backend-qmp.log", time.monotonic() + 10))
            control("nbd-server-start", addr={"type": "unix", "data": {"path": str(export)}})
            control("nbd-server-add", device="timeoutdisk", name="timeoutdisk")
            relay_errors = stack.enter_context(delay_reply(proxy, export, armed, suspended, resume))
            process = stack.enter_context(launch(command, serial))
            deadline = time.monotonic() + args.seconds
            guest = stack.enter_context(connect_monitor(process, monitor, output / "qmp.log", deadline))
            guest("cont")
            while process.poll() is None and time.monotonic() < deadline:
                log = serial.read_text(errors="replace")
                if any(marker in log for marker in failure_markers):
                    break
                if "[M6 STORAGE] scratch ready for timeout" in log:
                    armed.set()
                if stop_marker in log and suspended.is_set() and not resumed:
                    resume.set()
                    resumed = True
                if all(marker in log for marker in expected):
                    break
                time.sleep(0.02)
            else:
                if process.poll() is None:
                    if resumed:
                        host_timeout_stage = "after-release"
                    elif suspended.is_set():
                        host_timeout_stage = "waiting-for-guest-timeout"
                    elif armed.is_set():
                        host_timeout_stage = "waiting-for-scratch-reply"
                    else:
                        host_timeout_stage = "waiting-for-scratch-ready"
    except (OSError, RuntimeError, ValueError) as error:
        controller_error = str(error)

    log = serial.read_text(errors="replace") if serial.exists() else ""
    missing = [marker for marker in expected if marker not in log]
    failures = [marker for marker in failure_markers if marker in log]
    passed = (armed.is_set() and suspended.is_set() and resumed and not missing and not failures
              and controller_error is None and not relay_errors)
    (output / "result.json").write_text(json.dumps(
        {"status": "PASS" if passed else "FAIL", "missing": missing, "guest_failures": failures,
         "fault_armed": armed.is_set(), "fault_suspended": suspended.is_set(), "fault_resumed": resumed,
         "host_wait_seconds": args.seconds, "host_timeout_stage": host_timeout_stage if not passed else None,
         "controller_error": controller_error, "relay_errors": relay_errors,
         "argv": command, "backend_argv": backend_command},
        indent=2))
    print("PASS VirtIO timeout, stop and reclaim" if passed else
          f"FAIL armed={armed.is_set()}, suspended={suspended.is_set()}, resumed={resumed}, "
          f"host_timeout={host_timeout_stage}, "
          f"controller={controller_error}, relay={relay_errors}, guest failures={failures}, missing={missing}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
