#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build=${1:-build}
packages=${2:-packages}
output=${3:-$build/export/osinfo.img}
profile=${4:-development}
case "$profile" in development|system) ;; *) echo 'unknown account profile' >&2; exit 2;; esac
stage=$(mktemp -d "${TMPDIR:-/tmp}/osinfo-system-image.XXXXXX")
temporary="$output.tmp.$$"
trap 'rm -rf -- "$stage"; rm -f -- "$temporary"' EXIT HUP INT TERM
sh script/install.sh "$stage/root" "$build" "$packages" 0
if [ "$profile" = development ]; then tools/osinfo-host accounts --root "$stage/root" --argument development; fi
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
mkdir -p "$stage/root/boot/grub/fonts" "$(dirname -- "$output")"
cp /usr/share/grub/unicode.pf2 "$stage/root/boot/grub/fonts/unicode.pf2"
cat >"$stage/early.cfg" <<'GRUB'
set root=(hd0,msdos1)
set prefix=($root)/boot/grub
set gfxmode=1024x768x32
set gfxpayload=keep
loadfont /boot/grub/fonts/unicode.pf2
terminal_output gfxterm
GRUB
grub-mkimage -O i386-pc -p /boot/grub -c "$stage/early.cfg" -o "$stage/core.img" \
    biosdisk part_msdos ext2 normal multiboot2 configfile all_video gfxterm font
cp /usr/lib/grub/i386-pc/boot.img "$stage/boot.img"
# 根据已安装内容计算根分区容量，额外保留编译和安装软件的空间。
used_kib=$(du -sk "$stage/root" | awk '{print $1}')
required_mib=$(((used_kib + 1023) / 1024 + 64))
root_mib=128
while [ "$root_mib" -lt "$required_mib" ]; do root_mib=$((root_mib + 64)); done
disk_mib=$((root_mib + 32))
root_sectors=$((root_mib * 2048))
truncate -s "${root_mib}M" "$stage/root.ext3"
mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
tools/osinfo-host layout --root "$stage/root" --image "$stage/root.ext3"
truncate -s "${disk_mib}M" "$stage/osinfo.img"
printf 'label: dos\nunit: sectors\n\n2048,%s,83,*\n' "$root_sectors" | sfdisk "$stage/osinfo.img" >/dev/null
dd if="$stage/root.ext3" of="$stage/osinfo.img" bs=1M seek=1 conv=notrunc status=none
tools/osinfo-host image --argument embed-grub --output "$stage" --image "$stage/osinfo.img"
cp "$stage/osinfo.img" "$temporary"
mv -f "$temporary" "$output"
printf 'OSInfo system image: %s\n' "$output"
