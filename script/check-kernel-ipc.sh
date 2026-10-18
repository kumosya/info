#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
for source in src/kernel/task/task.cc src/kernel/task/reference.cc src/kernel/task/ipc/ipc.cc src/kernel/task/ipc/pipe.cc tests/kernel-ipc.cc; do
    output="build/host-tests/$(basename "$source").o"
    c++ -std=c++20 -O1 -g -pthread -ffunction-sections -fdata-sections \
        -Iinclude -Iinclude/os-c++ -c "$source" -o "$output"
done
c++ -pthread -Wl,--gc-sections build/host-tests/task.cc.o \
    build/host-tests/reference.cc.o build/host-tests/ipc.cc.o build/host-tests/pipe.cc.o \
    build/host-tests/kernel-ipc.cc.o -o build/host-tests/kernel-ipc
timeout 20s build/host-tests/kernel-ipc
