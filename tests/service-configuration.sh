#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
stage=$(mktemp -d /var/tmp/osinfo-service-configuration.XXXXXX)
printf '%s\n' "$stage" >build/last-service-configuration-stage
tools/osinfo-host image --image build/export/osinfo.img --output "$stage" --argument prepare
sh script/install.sh "$stage/root" build packages 0
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
running=
trap 'if [ -n "$running" ]; then kill "$running" 2>/dev/null || :; wait "$running" 2>/dev/null || :; fi' EXIT HUP INT TERM
for kind in missing duplicate-order nul path arguments cycle; do
    cp script/etc/services "$stage/root/etc/services"
    case "$kind" in
        missing) rm "$stage/root/etc/services";;
        duplicate-order) printf 'invalid|init|0|/bin/sleep|60|fs|10|-|100|0\n' >>"$stage/root/etc/services";;
        nul) printf '\000' >>"$stage/root/etc/services";;
        path) printf 'invalid|init|0|/bin/../bin/sleep|60|fs|50|-|100|0\n' >>"$stage/root/etc/services";;
        arguments) printf 'invalid|init|0|/bin/sleep|,60|fs|50|-|100|0\n' >>"$stage/root/etc/services";;
        cycle) printf 'cycle-a|init|0|/bin/sleep|60|cycle-b|50|-|100|0\ncycle-b|init|0|/bin/sleep|60|cycle-a|60|-|100|0\n' >>"$stage/root/etc/services";;
    esac
    mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
    tools/osinfo-host layout --root "$stage/root" --image "$stage/root.ext3"
    tools/osinfo-host image --output "$stage" --argument splice
    qemu-system-x86_64 -m 512M -smp 4 -boot order=c,strict=on \
        -drive "file=$stage/smoke.img,if=none,format=raw,id=osdisk" -device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
        -display none -serial stdio -monitor none -no-reboot >"$stage/$kind.log" 2>&1 &
    running=$!; attempt=0
    while kill -0 "$running" 2>/dev/null && [ "$attempt" -lt 300 ]; do
        if grep -Fq '[INIT] service lifecycle control online' "$stage/$kind.log"; then break; fi
        sleep 0.1; attempt=$((attempt + 1))
    done
    kill "$running" 2>/dev/null || :; wait "$running" 2>/dev/null || :; running=
    grep -Fq '[INIT] file service configuration rejected; bootstrap control remains available' "$stage/$kind.log"
    grep -Fq '[INIT] service lifecycle control online' "$stage/$kind.log"
    if grep -Eq '\[USERS\]|page fault|unrecoverable kernel exception' "$stage/$kind.log"; then cat "$stage/$kind.log"; exit 1; fi
    printf 'Configuration rejection passed: %s\n' "$kind"
done
printf 'All invalid configurations rejected before file services started: %s\n' "$stage"
