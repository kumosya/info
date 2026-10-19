#!/bin/sh
set -eu

scriptPath=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$scriptPath/.."

imagePath=${1:-build/osinfo.img}
buildPath=${2:-build}
packagePath=${3:-packages}
mountPath=""
loopDevice=""

cleanup() {
    status=$?
    trap - EXIT
    if [ -n "$mountPath" ] && mountpoint -q "$mountPath"; then
        umount "$mountPath" || status=1
    fi
    if [ -n "$loopDevice" ] && ! mountpoint -q "$mountPath"; then
        losetup -d "$loopDevice" || status=1
    fi
    if [ -n "$mountPath" ]; then
        rmdir "$mountPath" 2>/dev/null || true
    fi
    exit "$status"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

for tool in losetup blockdev partprobe mountpoint mount umount python3 mktemp sync flock; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required" >&2
        exit 1
    }
done

test "$(id -u)" -eq 0 || {
    echo "cpfiles.sh must run as root" >&2
    exit 1
}

test -f "$imagePath" || {
    echo "missing disk image: $imagePath (run 'make mkimg' first)" >&2
    exit 1
}

exec 9<>"$imagePath"
flock -n 9 || { echo "disk image is in use: $imagePath" >&2; exit 1; }
if [ -n "$(losetup -j "$imagePath")" ]; then
    echo "disk image is already attached; finish or clean up the previous image operation: $imagePath" >&2
    exit 1
fi

for requiredPath in "$buildPath/kernel.elf" \
    "$buildPath/services/init.elf" "$buildPath/services/task.elf" \
    "$buildPath/services/fs.elf" "$buildPath/services/pci.elf" "$buildPath/services/device.elf" \
    "$buildPath/services/storage.elf" "$buildPath/services/tty.elf" \
    "$buildPath/services/console.elf" \
    "$buildPath/services/users.elf" "$buildPath/services/mm.elf" "$buildPath/services/pipe.elf" "$buildPath/services/msgq.elf" \
    "$packagePath/libos-c++/libos-c++.so" \
    "$packagePath/libc++/libc.so" "$packagePath/libc++/libc++.so" \
    "$packagePath/libosgui-c++/libosgui-c++.so" \
    script/grub.cfg script/etc/fstab script/etc/passwd script/etc/group \
    script/etc/shadow script/etc/sudoers; do
    test -f "$requiredPath" || {
        echo "missing build input: $requiredPath" >&2
        exit 1
    }
done

# Catch missing payloads and conflicting package configurations before touching the image.
python3 "$scriptPath/packages.py" --package-dir "$packagePath" --check

mountPath=$(mktemp -d /tmp/osinfo-cpfiles.XXXXXX)
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

mount "$partitionPath" "$mountPath"
mkdir -p "$mountPath/bin" "$mountPath/boot/grub" "$mountPath/dev/pts" \
    "$mountPath/etc/inpkg" "$mountPath/home" \
    "$mountPath/mnt/cdrom" "$mountPath/mnt/ext4" \
    "$mountPath/mnt/fat32" "$mountPath/sbin/sys" "$mountPath/sys" \
    "$mountPath/tmp" "$mountPath/usr/bin" "$mountPath/usr/sbin" \
    "$mountPath/usr/lib" "$mountPath/usr/include" \
    "$mountPath/root" "$mountPath/home/user"
chmod 0700 "$mountPath/root"
chown -R 1000:1000 "$mountPath/home/user"

cp "$buildPath/kernel.elf" "$mountPath/boot/kernel.elf"
cp "$buildPath/services/init.elf" "$buildPath/services/task.elf" \
    "$buildPath/services/fs.elf" "$buildPath/services/pci.elf" "$buildPath/services/device.elf" \
    "$buildPath/services/storage.elf" "$buildPath/services/tty.elf" \
    "$buildPath/services/console.elf" \
    "$buildPath/services/users.elf" "$buildPath/services/mm.elf" "$buildPath/services/pipe.elf" "$buildPath/services/msgq.elf" "$mountPath/boot/"
cp "$packagePath/libos-c++/libos-c++.so" "$packagePath/libc++/libc.so" \
    "$packagePath/libc++/libc++.so" "$packagePath/libosgui-c++/libosgui-c++.so" "$mountPath/boot/"
cp script/grub.cfg "$mountPath/boot/grub/grub.cfg"
cp -R script/etc/. "$mountPath/etc/"
python3 "$scriptPath/layout.py" --root "$mountPath"
chmod 0600 "$mountPath/etc/shadow"
chmod 0440 "$mountPath/etc/sudoers"
for service in init task fs pci device storage tty console users mm pipe msgq; do
    cp "$buildPath/services/$service.elf" "$mountPath/sbin/sys/"
done
python3 "$scriptPath/packages.py" --package-dir "$packagePath" --install-root "$mountPath"
cp "$mountPath/sbin/sys/guiw.elf" "$mountPath/boot/guiw.elf"
mountpoint -q "$mountPath" || exit 1
# Remove the obsolete embedded package repository from earlier image layouts.
if [ -d "$mountPath/usr/share/inpkg" ] && [ ! -L "$mountPath/usr/share" ] && [ ! -L "$mountPath/usr/share/inpkg" ]; then
    rm -rf "$mountPath/usr/share/inpkg"
    rmdir "$mountPath/usr/share" 2>/dev/null || true
fi
chmod 0755 "$mountPath/sbin/sys"/*.elf
chmod 1777 "$mountPath/tmp"
sync -f "$mountPath"
echo "copied boot files into $imagePath"
