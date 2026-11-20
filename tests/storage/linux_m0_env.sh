#!/bin/sh
# Apply private dependencies only to this child process.
set -eu
repository=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output="$repository/tests/output/v0.1.5/M0"
private="$output/linux-deps/sysroot"
test -d "$private" || { echo 'run prepare_linux_dependencies.py first' >&2; exit 2; }
exec env PATH="$output/host-bin:$private/usr/bin:$PATH" \
    LD_LIBRARY_PATH="$private/usr/lib/x86_64-linux-gnu:$private/usr/lib64:$private/compat${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    QEMU_MODULE_DIR="$private/usr/lib64/qemu" "$@"
