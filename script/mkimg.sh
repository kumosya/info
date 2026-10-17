#!/bin/sh
set -eu

imagePath=${1:-build/osinfo.img}
buildPath=${2:-build}
imageSize=${OSINFO_IMAGE_SIZE:-512M}
mountPath="/tmp/osinfo-mkimg-$$"
loopDevice=""

cleanup() {
    for target in "$mountPath/mnt/fat32" "$mountPath/mnt/ext4" \
        "$mountPath"; do
        if mountpoint -q "$target"; then
            umount "$target"
        fi
    done
    if [ -n "$loopDevice" ]; then
        losetup -d "$loopDevice"
    fi
    rmdir "$mountPath" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

for tool in truncate sfdisk losetup blockdev partprobe mkfs.ext3 mkfs.ext4 \
    mkfs.fat mount mountpoint umount grub-install sync flock; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required" >&2
        exit 1
    }
done

test "$(id -u)" -eq 0 || {
    echo "mkimg.sh must run as root" >&2
    exit 1
}

mkdir -p "$(dirname "$imagePath")" "$mountPath"
exec 9<>"$imagePath"
flock -n 9 || { echo "disk image is in use: $imagePath" >&2; exit 1; }
if [ -n "$(losetup -j "$imagePath")" ]; then
    echo "disk image is already attached; finish or clean up the previous image operation: $imagePath" >&2
    exit 1
fi
truncate -s "$imageSize" "$imagePath"
printf 'label: dos\nunit: sectors\n\n2048,524288,83,*\n526336,262144,83\n788480,,c\n' |
    sfdisk "$imagePath"

loopDevice=$(losetup --find --show --partscan "$imagePath")
partprobe "$loopDevice"
if command -v udevadm >/dev/null 2>&1; then
    udevadm settle
fi
rootPartition="${loopDevice}p1"
ext4Partition="${loopDevice}p2"
fat32Partition="${loopDevice}p3"
attempt=0
while ! blockdev --getsize64 "$fat32Partition" >/dev/null 2>&1 && \
      [ "$attempt" -lt 50 ]; do
    sleep 0.1
    attempt=$((attempt + 1))
done
blockdev --getsize64 "$rootPartition" >/dev/null 2>&1 &&
blockdev --getsize64 "$ext4Partition" >/dev/null 2>&1 &&
blockdev --getsize64 "$fat32Partition" >/dev/null 2>&1 || {
    echo "one or more partition devices were not created" >&2
    exit 1
}

mkfs.ext3 -F -b 4096 -L OSINFO_ROOT "$rootPartition"
mkfs.ext4 -F -b 4096 -L OSINFO_EXT4 \
    -O ^has_journal,^metadata_csum,^64bit "$ext4Partition"
mkfs.fat -F 32 -n FAT32 "$fat32Partition"
mount "$rootPartition" "$mountPath"
mkdir -p "$mountPath/boot/grub" "$mountPath/mnt/ext4" \
    "$mountPath/mnt/fat32"
mount "$ext4Partition" "$mountPath/mnt/ext4"
mount "$fat32Partition" "$mountPath/mnt/fat32"
cp script/grub.cfg "$mountPath/boot/grub/grub.cfg"
grub-install --target=i386-pc --boot-directory="$mountPath/boot" \
    --modules="part_msdos ext2 multiboot2" --no-floppy "$loopDevice"
for target in "$mountPath/mnt/fat32" "$mountPath/mnt/ext4" "$mountPath"; do
    sync -f "$target"
done
echo "created partitioned ext3/ext4/FAT32 image: $imagePath"
