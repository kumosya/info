#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
clicks=${1:-400}
stage=$(mktemp -d /var/tmp/osinfo-desktop-stress.XXXXXX)
printf '%s\n' "$stage" >build/last-desktop-stress-stage
tools/osinfo-host image --image build/export/osinfo.img --output "$stage" --argument prepare
sh script/install.sh "$stage/root" build packages 0
printf 'LABEL=OSINFO_ROOT / ext3 defaults 0 1\n' >"$stage/root/etc/fstab"
mkfs.ext3 -q -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
tools/osinfo-host layout --root "$stage/root" --image "$stage/root.ext3"
tools/osinfo-host image --output "$stage" --argument splice
tools/osinfo-host input --root "$stage" --clicks "$clicks" --pattern windows --argument 'killall sh'
