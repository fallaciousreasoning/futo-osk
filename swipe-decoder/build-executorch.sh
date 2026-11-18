#!/bin/sh
# Builds the parts of ExecuTorch that futo-swipe-decoder links, the way
# swipe-library's Makefile does (same flags, patch and selected operators).
#   build-executorch.sh EXECUTORCH_DIR SWIPE_LIBRARY_DIR BUILD_DIR
# torchgen (from PyTorch's source tree) must be importable by python3.
# Set KLEIDIAI_SOURCE_DIR to a KleidiAI checkout so XNNPACK doesn't download
# it on Arm.
# -D_GNU_SOURCE: XNNPACK builds with -std=c99, where musl hides POSIX
# functions such as clock_gettime() unless they're asked for.
set -eu
et=$(realpath "$1")
swipe=$(realpath "$2")
out=$3

for p in "$swipe"/patches/executorch-*.patch "$(dirname "$0")"/patches/executorch-*.patch; do
    [ -e "$p" ] || continue
    # Skip patches already applied (a rebuild); fail on any that don't apply.
    patch -d "$et" -p1 -R -s -f --dry-run < "$p" > /dev/null 2>&1 && continue
    patch -d "$et" -p1 -s < "$p"
done
ops=$(cat "$swipe"/models/*/ops.txt | sort -u | grep -v '^$' | paste -sd, -)

cmake -B "$out" -S "$et" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPYTHON_EXECUTABLE="$(command -v python3)" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_FLAGS="${CFLAGS:-} -D_GNU_SOURCE" \
    -DEXECUTORCH_BUILD_XNNPACK=ON \
    -DEXECUTORCH_BUILD_EXTENSION_MODULE=ON \
    -DEXECUTORCH_BUILD_EXTENSION_TENSOR=ON \
    -DEXECUTORCH_BUILD_EXTENSION_DATA_LOADER=ON \
    -DEXECUTORCH_BUILD_EXTENSION_FLAT_TENSOR=ON \
    -DEXECUTORCH_BUILD_EXTENSION_NAMED_DATA_MAP=ON \
    -DEXECUTORCH_BUILD_KERNELS_QUANTIZED=OFF \
    -DEXECUTORCH_BUILD_VULKAN=OFF \
    -DEXECUTORCH_BUILD_MPS=OFF \
    -DEXECUTORCH_BUILD_COREML=OFF \
    -DEXECUTORCH_BUILD_QNN=OFF \
    -DEXECUTORCH_BUILD_SDK=OFF \
    -DEXECUTORCH_BUILD_TESTS=OFF \
    -DEXECUTORCH_BUILD_EXAMPLES=OFF \
    -DEXECUTORCH_OPTIMIZE_SIZE=ON \
    -DEXECUTORCH_SELECT_OPS_LIST="$ops" \
    -DXNNPACK_BUILD_ALL_MICROKERNELS=OFF \
    -DXNNPACK_ENABLE_SPARSE=OFF \
    -DXNNPACK_BUILD_TESTS=OFF \
    -DXNNPACK_BUILD_BENCHMARKS=OFF \
    -DXNNPACK_ENABLE_ARM_SME=OFF \
    -DXNNPACK_ENABLE_ARM_SME2=OFF \
    ${KLEIDIAI_SOURCE_DIR:+-DKLEIDIAI_SOURCE_DIR="$KLEIDIAI_SOURCE_DIR"}
cmake --build "$out"
