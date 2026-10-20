#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
disk=${1:-build/export/osinfo.img}
stage=$(mktemp -d /var/tmp/osinfo-service-runtime.XXXXXX)
printf '%s\n' "$stage" >build/last-service-runtime-stage
tools/osinfo-host image --image "$disk" --output "$stage" --argument prepare
make build/tests/service-runtime
sh script/install.sh "$stage/root" build packages 0
if [ -n "${OSINFO_LIFECYCLE_KERNEL:-}" ]; then cp "$OSINFO_LIFECYCLE_KERNEL" "$stage/root/boot/kernel.elf"; fi
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
printf 'test-sleeper|init|0|/bin/sleep|60|fs|50|-|1000|0\n' >>"$stage/root/etc/services"
cp "$stage/root/usr/bin/terminal" "$stage/root/usr/bin/terminal.real"
cp build/tests/service-runtime "$stage/root/usr/bin/terminal"
mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
tools/osinfo-host layout --root "$stage/root" --image "$stage/root.ext3"
tools/osinfo-host image --output "$stage" --argument splice
qemu-system-x86_64 -m 512M -smp 4 -boot order=c,strict=on \
    -drive "file=$stage/smoke.img,if=none,format=raw,id=osdisk" -device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
    -display none -serial stdio -monitor none -gdb "unix:$stage/gdb.sock,server=on,wait=off" -no-reboot >"$stage/serial.log" 2>&1 &
running=$!
trap 'kill "$running" 2>/dev/null || :; wait "$running" 2>/dev/null || :' EXIT HUP INT TERM
elapsed=0
while kill -0 "$running" 2>/dev/null && [ "$elapsed" -lt 90 ]; do
    if grep -Eq '\[RUNTIME TEST\] (ALL PASSED|FAIL)' "$stage/serial.log"; then break; fi
    sleep 1; elapsed=$((elapsed + 1))
done
kill "$running" 2>/dev/null || :
wait "$running" 2>/dev/null || :
trap - EXIT HUP INT TERM
cat "$stage/serial.log"
printf 'Evidence: %s\n' "$stage"
grep -Fq '[RUNTIME TEST] ALL PASSED' "$stage/serial.log"
if [ -n "${OSINFO_LIFECYCLE_KERNEL:-}" ]; then
    grep -Fq '[TASK TEST] joined thread records reclaimed (128 rounds)' "$stage/serial.log"
    grep -Fq '[TASK TEST] killed cleanup requester passed (32 rounds)' "$stage/serial.log"
    grep -Fq '[TASK TEST] remote CR3 exec and concurrent kill passed (64 rounds)' "$stage/serial.log"
fi
if grep -Eq 'page fault|unrecoverable kernel exception|\[RUNTIME TEST\] FAIL' "$stage/serial.log"; then exit 1; fi
