#!/usr/bin/env bash
set -e
{
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build -- -j$(nproc)
ctest --test-dir build --output-on-failure
} 2>&1 | tee asset/asan_ubsan.log
