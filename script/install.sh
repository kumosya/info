#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
mode=directory
if [ "${1:-}" = --image ]; then mode=image; shift; fi
target=${1:?installation directory or image required}
build=${2:-build}
packages=${3:-packages}
install_grub=${4:-0}
grub_disk=${5:-}
case "$install_grub" in 0|1) ;; *) echo 'INSTALL_GRUB must be 0 or 1' >&2; exit 2;; esac
if [ "$install_grub" = 1 ] && [ "$mode" = directory ]; then
    test -n "$grub_disk" && test -b "$grub_disk" || { echo 'INSTALL_GRUB=1 requires GRUB_DISK block device' >&2; exit 2; }
fi
mount_path=
loop_device=
cleanup() {
    status=$?
    trap - EXIT
    if [ -n "$mount_path" ]; then
        if mountpoint -q "$mount_path"; then umount "$mount_path" || status=1; fi
        if ! mountpoint -q "$mount_path"; then
            if [ -n "$loop_device" ]; then losetup -d "$loop_device" || status=1; fi
            rmdir "$mount_path" 2>/dev/null || true
        fi
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

tools/inpkg catalog --config "$build/packages.conf" --package-dir "$packages" --check
if [ "$mode" = image ]; then
    test "$(id -u)" -eq 0 || { echo 'image installation requires root' >&2; exit 1; }
    test -f "$target" && test ! -L "$target" || { echo "missing regular disk image: $target (run 'make mkimg' first)" >&2; exit 1; }
    test -z "$grub_disk" || { echo 'image installation selects its own GRUB device; omit GRUB_DISK' >&2; exit 2; }
    exec 9<>"$target"
    flock -n 9 || { echo "disk image is in use: $target" >&2; exit 1; }
    test -z "$(losetup -j "$target")" || { echo "disk image is already attached: $target" >&2; exit 1; }
    mount_path=$(mktemp -d /tmp/osinfo-install.XXXXXX)
    loop_device=$(losetup --find --show --partscan "$target")
    partprobe "$loop_device"
    if command -v udevadm >/dev/null 2>&1; then udevadm settle; fi
    partition="${loop_device}p1"
    attempt=0
    while ! blockdev --getsize64 "$partition" >/dev/null 2>&1 && [ "$attempt" -lt 50 ]; do
        sleep 0.1; attempt=$((attempt + 1))
    done
    blockdev --getsize64 "$partition" >/dev/null 2>&1 || { echo "missing root partition: $partition" >&2; exit 1; }
    mount "$partition" "$mount_path"
    destination=$mount_path
    grub_disk=$loop_device
else destination=$target; fi
mkdir -p "$destination"
destination=$(realpath "$destination")
test "$destination" != / || { echo 'refusing to install into host /' >&2; exit 2; }
tools/osinfo-host deploy --root "$destination" --build "$build" --packages "$packages"
tools/osinfo-host layout --root "$destination"
tools/inpkg catalog --config "$build/packages.conf" --package-dir "$packages" --check --sysroot "$destination"
tools/osinfo-host deploy --root "$destination" --build "$build" --packages "$packages"
tools/osinfo-host layout --root "$destination"
if [ "$install_grub" = 1 ]; then
    grub-install --target=i386-pc --boot-directory="$destination/boot" --modules='part_msdos ext2 multiboot2' --no-floppy "$grub_disk"
fi
if [ "$mode" = image ]; then
    mountpoint -q "$destination"
    sync -f "$destination"
fi
printf 'Installed OSInfo to %s (GRUB=%s)\n' "$target" "$install_grub"
