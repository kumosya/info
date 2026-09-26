#!/bin/sh
set -eu

imagePath=${1:-build/osinfo.img}
buildPath=${2:-build}
imageSize=${OSINFO_IMAGE_SIZE:-256M}
mountPath="/tmp/osinfo-mkimg-$$"
loopDevice=""

cleanup() {
    if mountpoint -q "$mountPath"; then
        umount "$mountPath"
    fi
    if [ -n "$loopDevice" ]; then
        losetup -d "$loopDevice"
    fi
    rmdir "$mountPath" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

for tool in truncate sfdisk losetup blockdev partprobe mkfs.ext3 mount umount grub-install; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required" >&2
        exit 1
    }
done

test "$(id -u)" -eq 0 || {
    echo "mkimg.sh must run as root" >&2
    exit 1
}

for requiredPath in "$buildPath/kernel.elf" \
    "$buildPath/services/init.elf" "$buildPath/services/task.elf" \
    "$buildPath/services/fs.elf" "$buildPath/services/device.elf" \
    "$buildPath/services/storage.elf" \
    "$buildPath/services/console.elf" "$buildPath/services/window.elf" \
    "$buildPath/lib/libos-c++.so" "$buildPath/lib/libc.so" \
    "$buildPath/lib/libc++.so" script/grub.cfg script/etc/fstab; do
    test -f "$requiredPath" || {
        echo "missing build input: $requiredPath" >&2
        exit 1
    }
done

mkdir -p "$(dirname "$imagePath")" "$mountPath"
truncate -s "$imageSize" "$imagePath"
printf 'label: dos\nunit: sectors\n\n2048,,83,*\n' | sfdisk "$imagePath"

loopDevice=$(losetup --find --show --partscan "$imagePath")
partprobe "$loopDevice"
if command -v udevadm >/dev/null 2>&1; then
    udevadm settle
fi
partitionPath="${loopDevice}p1"
attempt=0
while ! blockdev --getsize64 "$partitionPath" >/dev/null 2>&1 && \
      [ "$attempt" -lt 50 ]; do
    sleep 0.1
    attempt=$((attempt + 1))
done
blockdev --getsize64 "$partitionPath" >/dev/null 2>&1 || {
    echo "partition device was not created: $partitionPath" >&2
    exit 1
}

mkfs.ext3 -F -b 4096 -L OSINFO_ROOT "$partitionPath"
mount "$partitionPath" "$mountPath"

mkdir -p "$mountPath/bin" "$mountPath/boot/grub" "$mountPath/dev/pts" \
    "$mountPath/etc" "$mountPath/home" "$mountPath/lib/x86-64-osinfo" \
    "$mountPath/mnt/cdrom" "$mountPath/system/bin" \
    "$mountPath/system/services" "$mountPath/tmp" "$mountPath/usr/bin"

cp "$buildPath/kernel.elf" "$mountPath/boot/kernel.elf"
cp "$buildPath/services/init.elf" "$buildPath/services/task.elf" \
    "$buildPath/services/fs.elf" "$buildPath/services/device.elf" \
    "$buildPath/services/storage.elf" "$mountPath/boot/"
cp "$buildPath/lib/libos-c++.so" "$mountPath/boot/libos-c++.so"
cp script/grub.cfg "$mountPath/boot/grub/grub.cfg"
cp script/etc/* "$mountPath/etc/"
cp "$buildPath"/bin/* "$mountPath/bin/"
if [ -f "$mountPath/bin/terminal" ]; then
    mv "$mountPath/bin/terminal" "$mountPath/usr/bin/terminal"
fi
cp "$buildPath/services/console.elf" "$buildPath/services/window.elf" \
    "$mountPath/system/services/"
cp "$buildPath/lib/libos-c++.so" "$buildPath/lib/libc.so" \
    "$buildPath/lib/libc++.so" \
    "$mountPath/lib/x86-64-osinfo/"
chmod 0755 "$mountPath/bin"/* "$mountPath/usr/bin"/* \
    "$mountPath/system/services"/*
chmod 1777 "$mountPath/tmp"

grub-install --target=i386-pc --boot-directory="$mountPath/boot" \
    --modules="part_msdos ext2 multiboot2" --no-floppy "$loopDevice"
sync
echo "created bootable ext3 image: $imagePath"
