#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
stage=$(mktemp -d /var/tmp/osinfo-input-pressure.XXXXXX)
printf '%s\n' "$stage" >build/last-input-pressure-stage
tools/osinfo-host image --image build/export/osinfo.img --output "$stage" --argument prepare
make build/tests/input-backpressure build/services/users.elf
sh script/install.sh "$stage/root" build packages 0
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
cp build/tests/input-backpressure "$stage/root/usr/bin/terminal"
if [ "$#" -gt 0 ]; then cp "$1" "$stage/root/boot/device.elf"; fi
mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
tools/osinfo-host layout --root "$stage/root" --image "$stage/root.ext3"
tools/osinfo-host image --output "$stage" --argument splice
tools/osinfo-host input --root "$stage" --pattern pressure --clicks 16
