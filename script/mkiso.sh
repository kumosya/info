#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build=${1:-build}
packages=${2:-packages}
output=${3:-$build/export/osinfo.iso}
mkdir -p "$build" "$(dirname -- "$output")"
stage=$(mktemp -d "${TMPDIR:-/tmp}/osinfo-iso.XXXXXX")
temporary="$output.tmp.$$"
trap 'rm -rf -- "$stage"; rm -f -- "$temporary"' EXIT HUP INT TERM
sh script/install.sh "$stage/root" "$build" "$packages" 0
# The entire runtime comes from the CD. Writes stay in the boot module in RAM.
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
mkdir -p "$stage/cd/boot"
cp -a "$stage/root/boot/." "$stage/cd/boot/"
truncate -s 128M "$stage/cd/boot/rootfs.ext3"
mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/cd/boot/rootfs.ext3"
tools/osinfo-host layout --root "$stage/root" --image "$stage/cd/boot/rootfs.ext3"
sed '/^[[:space:]]*boot$/i\    module2 /boot/rootfs.ext3 rootfs' "$build/generated/grub.cfg" >"$stage/cd/boot/grub/grub.cfg"
grub-mkrescue -o "$stage/osinfo.iso" "$stage/cd"
cp -- "$stage/osinfo.iso" "$temporary"
mv -f -- "$temporary" "$output"
printf 'CD-only image: %s\n' "$output"
