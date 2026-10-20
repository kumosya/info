#!/bin/sh
set -eu
repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"
build=${OSINFO_BUILD:-build}
packages=${OSINFO_PACKAGES:-packages}
host="$repository/tools/osinfo-host"
inpkg="$repository/tools/inpkg"
disk="$build/osinfo.img"
smp=4
seconds=45
mode=common
version=
hostname=
set_mode() {
    if [ "$mode" != common ]; then echo 'select only one smoke test mode' >&2; exit 2; fi
    mode=$1
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --disk) disk=$2; shift 2;;
        --smp) smp=$2; shift 2;;
        --seconds) seconds=$2; shift 2;;
        --memory-test) set_mode memory; shift;;
        --desktop-test) set_mode desktop; shift;;
        --base-tools-test) set_mode base-tools; shift;;
        --lifecycle-test) set_mode lifecycle; shift;;
        --inpkg-test) set_mode inpkg; shift;;
        --system-version) version=$2; shift 2;;
        --hostname) hostname=$2; shift 2;;
        *) echo "unknown smoke option: $1" >&2; exit 2;;
    esac
done
case "$smp:$seconds" in *[!0-9:]*|:*) echo 'invalid SMP or duration' >&2; exit 2;; esac
if [ "$mode" = lifecycle ] && [ "$smp" -lt 3 ]; then echo 'lifecycle requires at least three CPUs' >&2; exit 2; fi
stage=$(mktemp -d '/tmp/osinfo-smoke.XXXXXX')
printf '%s\n' "$stage" >"$build/last-smoke-stage"
printf 'Staging isolated boot artifacts at %s\n' "$stage"
mkdir -p "$stage/root"
"$host" image --image "$disk" --output "$stage" --argument prepare
"$host" deploy --root "$stage/root" --build "$build" --packages "$packages"
"$host" layout --root "$stage/root"
"$inpkg" catalog --config "$build/packages.conf" --package-dir "$packages" --check --sysroot "$stage/root"
"$host" deploy --root "$stage/root" --build "$build" --packages "$packages"
if [ "$mode" = inpkg ]; then
    mkdir -p "$stage/cross-payload" "$stage/root/tmp"
    printf 'cross-platform bytes\n' >"$stage/cross-payload/probe"
    printf 'name=cross-platform-probe\nversion=1\nbin=/usr/bin/cross-platform-probe\ninstall=probe -> /usr/bin/cross-platform-probe\n' >"$stage/cross.conf"
    "$inpkg" pack "$stage/cross-payload" --manifest "$stage/cross.conf" --output "$stage/root/tmp/cross-platform-probe.inpk"
    "$inpkg" upgrade "$stage/root/tmp/cross-platform-probe.inpk" --sysroot "$stage/root" --install-missing -y
fi
if [ -n "$version" ]; then printf '%s\n' "$version" >"$stage/root/lib/version.1"; fi
if [ -n "$hostname" ]; then printf '%s\n' "$hostname" >"$stage/root/etc/hostname"; fi
executable=
case "$mode" in
    memory) executable=mm-runtime;; desktop) executable=desktop-runtime;;
    inpkg) executable=inpkg-runtime;; base-tools) executable=base-tools-runtime;;
esac
if [ -n "$executable" ]; then
    cp "$stage/root/usr/bin/terminal" "$stage/root/usr/bin/terminal.real"
    cp "$build/tests/$executable" "$stage/root/usr/bin/terminal"
fi
if [ "$mode" = desktop ]; then
    mkdir -p "$stage/root/usr/lib/osinfo-test"
    cp "$build/tests/linker/"*.so "$stage/root/usr/lib/osinfo-test/"
    cp "$build/tests/linker/probe" "$stage/root/usr/bin/ld-probe"
    cp "$build/tests/linker/undefined" "$stage/root/usr/bin/ld-undefined"
    "$host" image --image "$build/tests/linker/probe" --output "$stage" --argument missing-library
fi
if [ "$mode" = lifecycle ]; then cp "$build/tests/kernel-lifecycle.elf" "$stage/root/boot/kernel.elf"; fi
mkfs.ext3 -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
"$host" layout --root "$stage/root" --image "$stage/root.ext3"
"$host" image --output "$stage" --argument splice
set +e
timeout --signal=TERM "$seconds" qemu-system-x86_64 -m 512M -smp "$smp" -boot order=c,strict=on \
    -drive "file=$stage/smoke.img,if=none,format=raw,id=osdisk" -device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
    -display none -serial stdio -monitor none -no-reboot >"$stage/serial.log" 2>&1
status=$?
set -e
cat "$stage/serial.log"
printf 'Serial log: %s/serial.log\n' "$stage"
if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then exit "$status"; fi
require_marker() { grep -Fq -- "$1" "$stage/serial.log" || { echo "missing smoke marker: $1" >&2; exit 1; }; }
require_marker '[MM] Ring 3 memory policy and pager service ready'
require_marker '[PCI] service ready'
require_marker '[INIT] console and virtual TTY service started'
require_marker '[INIT] tty1-tty6 shells started'
require_marker '[PIPE] Ring 3 service ready'
require_marker '[MSGQ] Ring 3 service ready'
case "$mode" in
    memory)
        require_marker '[SYSTEM TEST] configuration and mapping authority passed'
        require_marker '[MM TEST] anonymous, protection, partial unmap, COW, shared, file and signal passed'
        require_marker '[MM TEST] service publication and RPC deadlines passed'
        require_marker '[MM TEST] exec resets memory policy generation passed'
        require_marker '[MM TEST] Ring3 pipe and msgq passed';;
    desktop)
        require_marker '[DESKTOP TEST] saturated window notifications passed'
        require_marker '[DESKTOP TEST] shell argv conventions passed'
        require_marker '[DESKTOP TEST] XDG path and environment semantics passed'
        require_marker '[DESKTOP TEST] real user login and XDG session lifecycle passed'
        require_marker '[DESKTOP TEST] dynamic dependencies and explicit loader failures passed'
        require_marker '[LD TEST] transitive dependency and constructor passed'
        require_marker '[LD TEST] shared-library destructor passed'
        require_marker '[LD] library not found: liblost0.so'
        require_marker '[LD] undefined symbol: osinfoMissingProbeSymbol';;
    lifecycle)
        require_marker '[TASK TEST] killed cleanup requester passed (32 rounds)'
        require_marker '[TASK TEST] remote CR3 exec and concurrent kill passed (64 rounds)';;
    inpkg) require_marker '[INPKG TEST] target install, upgrade, missing, local scope and removal passed';;
    base-tools) require_marker '[BASE TOOLS TEST] configured sysinfo, mount, umount, mkfs rejection and coreutils passed';;
esac
if grep -Fq 'unrecoverable kernel exception' "$stage/serial.log"; then exit 1; fi
