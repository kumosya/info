#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
stage=$(mktemp -d build/config-bootstrap.XXXXXX)
original=0
cleanup() {
    status=$?
    trap - EXIT
    if [ -d config ]; then mv config "$stage/generated-config"; fi
    if [ "$original" = 1 ]; then mv "$stage/original-config" config; fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [ -d config ]; then mv config "$stage/original-config"; original=1; fi
test ! -e config
./configure --disable-image-tools --build-dir "$stage/out" --object-dir "$stage/obj"
for name in services layout checks; do test -s "config/$name.conf"; done
test "$(find config -type f | wc -l)" -eq 3
test ! -e config/applications.conf
printf '\n# preserve-local-settings\n' >>config/services.conf
./configure --disable-image-tools --build-dir "$stage/out" --object-dir "$stage/obj"
grep -q '^# preserve-local-settings$' config/services.conf
test -f "$stage/out/generated/grub.cfg"
echo 'configure independently creates absent config/ and preserves existing settings'
