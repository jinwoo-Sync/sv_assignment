#!/bin/bash
set -e

BUILD_TYPE=${1:-Debug}

if [ "${BUILD_TYPE}" = "Debug" ]; then
    TESTS=ON
elif [ "${BUILD_TYPE}" = "Release" ]; then
    TESTS=OFF
else
    echo "Usage: $0 [Debug|Release]"
    exit 1
fi

echo "[build.sh] BUILD_TYPE=${BUILD_TYPE}  BUILD_TESTS=${TESTS}"
cmake -S . -B build -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DBUILD_TESTS=${TESTS}
cmake --build build -- -j$(nproc)
echo "[build.sh] Done -> bin/${BUILD_TYPE}/"
