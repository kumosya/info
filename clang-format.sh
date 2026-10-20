#!/bin/sh
set -eu
cd "$(dirname -- "$0")"
find . -type d \( -name .git -o -name build -o -name obj -o -name packages \) -prune -o \
    -type f \( -name '*.h' -o -name '*.cc' -o -name '*.cpp' \) -exec clang-format -i -style=file -- {} +
