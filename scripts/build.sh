#!/bin/bash
set -e

BUILD_TYPE="Debug"
BUILD_TESTS="ON"
RUN_TESTS=true
ASAN=""
UBSAN=""
CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release"; BUILD_TESTS="OFF"; RUN_TESTS=false ;;
        --asan)    ASAN="-DENABLE_ASAN=ON" ;;
        --ubsan)   UBSAN="-DENABLE_UBSAN=ON" ;;
        --tests)   BUILD_TESTS="ON"; RUN_TESTS=true ;;
        --no-tests) BUILD_TESTS="OFF"; RUN_TESTS=false ;;
        --clean)   CLEAN=true ;;
    esac
done

if [ "$CLEAN" = true ]; then
    rm -rf build
fi

cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DBUILD_TESTS=${BUILD_TESTS} \
    ${ASAN} ${UBSAN}

cmake --build build -- -j$(nproc)

if [ "$RUN_TESTS" = true ]; then
    ctest --test-dir build --output-on-failure
fi
