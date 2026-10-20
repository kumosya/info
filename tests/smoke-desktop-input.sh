#!/bin/sh
set -eu
repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"
stage=
command='echo gui alive'
inpkg="$repository/tools/inpkg"
host="$repository/tools/osinfo-host"
# Resolve the stage before accepting package updates.
if [ "${1:-}" != --stage ] || [ "$#" -lt 2 ]; then echo 'usage: smoke-desktop-input.sh --stage STAGE [--command TEXT] [--package FILE]' >&2; exit 2; fi
stage=$(realpath "$2"); shift 2
test -f "$stage/.osinfo-stage"
if [ -f "$stage/root/usr/bin/terminal.real" ]; then cp "$stage/root/usr/bin/terminal.real" "$stage/root/usr/bin/terminal"; fi
while [ "$#" -gt 0 ]; do
    case "$1" in
        --command) command=$2; shift 2;;
        --package) "$inpkg" upgrade "$2" --sysroot "$stage/root" --install-missing -y; shift 2;;
        *) echo "unknown input option: $1" >&2; exit 2;;
    esac
done
"$host" deploy --root "$stage/root"
mkfs.ext3 -F -b 4096 -L OSINFO_ROOT -d "$stage/root" "$stage/root.ext3"
"$host" layout --root "$stage/root" --image "$stage/root.ext3"
"$host" image --output "$stage" --argument splice
"$host" input --root "$stage" --argument "$command"
