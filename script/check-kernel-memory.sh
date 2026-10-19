#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host-tests
c++ -std=c++20 -O2 -g -pthread -Iinclude src/kernel/memory/physical/buddy.cc tests/kernel-memory.cc -o build/host-tests/kernel-memory
timeout 30s build/host-tests/kernel-memory
c++ -std=c++20 -O2 -g -pthread -Iinclude src/kernel/memory/physical/buddy.cc src/kernel/memory/object/slab.cc src/kernel/memory/object/pool.cc src/kernel/memory/object/per_cpu.cc src/kernel/memory/heap.cc src/kernel/memory/object/page_object.cc tests/kernel-object-cache.cc -o build/host-tests/kernel-object-cache
timeout 30s build/host-tests/kernel-object-cache
c++ -std=c++20 -O2 -g -Iuser/mm user/mm/region.cc tests/mm-regions.cc -o build/host-tests/mm-regions
timeout 30s build/host-tests/mm-regions
