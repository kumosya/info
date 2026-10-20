#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p "build/host-tests"
c++ -std=c++20 -O2 -Wall -Wextra -Werror -I"include/os-c++" \
    "src/fs/sysfs.cc" "tests/system-config.cc" -o "build/host-tests/system-config"
"build/host-tests/system-config"
