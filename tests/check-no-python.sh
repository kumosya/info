#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
repository=$(pwd -P)
failed=0
while IFS='|' read -r kind project; do
    [ "$kind" = project ] || continue
    project=$(CDPATH= cd -- "$project" && pwd -P)
    list=$(mktemp)
    find "$project" -type d -name .git -prune -o -type f \( -name '*.py' -o -name '*.pyc' \) -print >"$list"
    while IFS= read -r file; do
        printf 'unexpected Python file: %s\n' "$file" >&2
        failed=1
    done <"$list"
    rm -f "$list"
done <config/checks.conf
test "$failed" -eq 0
echo 'Python inventory: no Python files remain'
