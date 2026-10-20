#!/bin/sh
set -eu
cd "$(dirname -- "$0")/.."
stage=$(mktemp -d build/configure-test.XXXXXX)
trap 'rm -rf -- "$stage"' EXIT HUP INT TERM
mkdir -p "$stage/assets" "$stage/packages" "$stage/bin" "$stage/root"
c++ -std=c++20 -O2 -Wall -Wextra -Werror tests/zip-fixture.cc -o "$stage/zip-fixture"
while IFS='|' read -r kind path; do
    [ "$kind" = archive ] || continue
    case "$(basename -- "$path")" in
        coreutils.inpk) "$stage/zip-fixture" "$stage/assets/osinfo-base-tools.zip" coreutils.inpk "$path";;
        *) cp "$path" "$stage/assets/";;
    esac
done <build/packages.conf
cp "$stage/assets/"* "$stage/packages/"
./configure --disable-image-tools --build-dir "$stage/out" --object-dir "$stage/obj" --package-dir "$stage/packages"
test -f "$stage/packages/coreutils.inpk"
test ! -d "$stage/packages/osinfo-base-tools"
tools/inpkg catalog --config "$stage/out/packages.conf" --package-dir packages --check --sysroot "$stage/root"
test -f "$stage/root/var/lib/inpkg/coreutils.state"
test -f "$stage/root/usr/bin/inpkg"
# GitHub fetching is exercised without depending on public releases or credentials.
cat >"$stage/bin/curl" <<'SH'
#!/bin/sh
set -eu
destination=
while [ "$#" -gt 0 ]; do
    case "$1" in --output) destination=$2; shift 2;; https://github.com/*) url=$1; shift;; *) shift;; esac
done
test "${FAIL_DOWNLOAD:-0}" = 0 || exit 22
cp "$CONFIGURE_TEST_ASSETS/${url##*/}" "$destination"
SH
chmod +x "$stage/bin/curl"
CONFIGURE_TEST_ASSETS=$(realpath "$stage/assets"); export CONFIGURE_TEST_ASSETS
OSINFO_RELEASE_URLS=
for asset in "$stage/assets/"*; do OSINFO_RELEASE_URLS="$OSINFO_RELEASE_URLS
https://github.com/test/releases/latest/download/$(basename -- "$asset")"; done
export OSINFO_RELEASE_URLS
PATH="$(realpath "$stage/bin"):$PATH"; export PATH
./configure --disable-image-tools --build-dir "$stage/download-out" --object-dir "$stage/download-obj" --package-dir "$stage/downloaded"
test -f "$stage/downloaded/coreutils.inpk"
if FAIL_DOWNLOAD=1 ./configure --disable-image-tools --build-dir "$stage/failed-out" --object-dir "$stage/failed-obj" --package-dir "$stage/not-downloaded"; then exit 1; fi
test ! -e "$stage/not-downloaded"
"$stage/zip-fixture" "$stage/packages/bad.zip" ../escaped.inpk packages/inpkg.inpk
if ./configure --disable-image-tools --build-dir "$stage/out" --object-dir "$stage/obj" --package-dir "$stage/packages"; then echo 'accepted ZIP traversal' >&2; exit 1; fi
test ! -e "$stage/escaped.inpk"
test ! -e "$stage/out/config.mk"
if make CONFIGURE_BUILD="$stage/out" -n all; then echo 'make accepted failed configuration' >&2; exit 1; fi
echo 'configure: offline package scan, direct ZIP extraction, deployment, download bootstrap and failure isolation passed'
