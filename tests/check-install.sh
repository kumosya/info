#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
test "$(id -u)" -eq 0 || { echo 'run this isolated image test as root' >&2; exit 2; }
stage=$(mktemp -d "$PWD/build/install-test.XXXXXX")
image="$stage/osinfo.img"
printf '%s\n' "$stage" >build/last-install-stage
sh script/mkimg.sh "$image" build >"$stage/mkimg.log" 2>&1
dd if="$image" of="$stage/boot-before" bs=1M count=1 status=none
mkdir "$stage/bin"
cat >"$stage/bin/grub-install" <<'SH'
#!/bin/sh
echo 'unexpected GRUB reinstall during default installation' >&2
exit 99
SH
chmod +x "$stage/bin/grub-install"
PATH="$stage/bin:$PATH" make install DISK_IMAGE="$image" >"$stage/install.log" 2>&1
grep -Fq '(GRUB=0)' "$stage/install.log"
dd if="$image" of="$stage/boot-after" bs=1M count=1 status=none
cmp "$stage/boot-before" "$stage/boot-after"
test -z "$(losetup -j "$image")"
dd if="$image" of="$stage/root.ext3" bs=1M skip=1 count=256 status=none
debugfs -R 'cat /lib/version.1' "$stage/root.ext3" >"$stage/version" 2>/dev/null
cmp script/lib/version.1 "$stage/version"
debugfs -R 'stat /usr/bin/inpkg' "$stage/root.ext3" >"$stage/inpkg-stat" 2>&1
grep -q 'Inode:' "$stage/inpkg-stat"
# Cooperating image operations must fail before attaching a busy image.
exec 8<>"$image"
flock -n 8
if sh script/install.sh --image "$image" build packages >"$stage/locked.log" 2>&1; then echo 'accepted locked image' >&2; exit 1; fi
grep -Fq 'disk image is in use' "$stage/locked.log"
flock -u 8
exec 8>&-
test -z "$(losetup -j "$image")"
# A deployment failure after mounting still releases its loop and mountpoint.
mkdir "$stage/incomplete-build"
cp build/packages.conf "$stage/incomplete-build/packages.conf"
if sh script/install.sh --image "$image" "$stage/incomplete-build" packages >"$stage/failed.log" 2>&1; then echo 'accepted missing build files' >&2; exit 1; fi
test -z "$(losetup -j "$image")"
# Directory installation shares the same deployment path and skips GRUB too.
PATH="$stage/bin:$PATH" make install DESTDIR="$stage/directory" >"$stage/directory.log" 2>&1
test -f "$stage/directory/boot/kernel.elf"
test -f "$stage/directory/var/lib/inpkg/coreutils.state"
grep -Fq '(GRUB=0)' "$stage/directory.log"
set +e
timeout 40s qemu-system-x86_64 -m 512 -smp 4 -boot order=c,strict=on \
    -drive "file=$image,if=none,format=raw,id=osdisk" -device virtio-blk-pci,drive=osdisk,disable-legacy=on,bootindex=0 \
    -display none -serial stdio -monitor none -no-reboot >"$stage/serial.log" 2>&1
status=$?
set -e
case "$status" in 0|124) ;; *) exit "$status";; esac
grep -Fq '[FS] root resolved to /dev/vda1' "$stage/serial.log"
grep -Fq '[INIT] tty1-tty6 shells started' "$stage/serial.log"
echo "make install: image/directory deployment, GRUB preservation, locking, failure cleanup and disk boot passed: $stage"
