#!/usr/bin/env bash
# configure.sh - configure the build tree: step 1 of the MSIL CI (Linux job).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/configure.sh [BUILD_DIR]        (default <repo>/build)
#
# cmake -C msil/cmake/caches/MSIL.cmake (Release + assertions, clang and lld, X86 + experimental MSIL,
# the msil/ external project) with clang/clang++ ($CC/$CXX if set), lld when ld.lld is on PATH,
# PCH off and, when sccache is on PATH, sccache launchers (sccache cannot cache PCHs; upstream
# CI turns PCH off too). configure.ps1 is the Windows counterpart.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"
build=${1:-$MSIL_REPO/build}

args=(-G Ninja -S "$MSIL_REPO/llvm" -B "$build"
  -C "$MSIL_REPO/msil/cmake/caches/MSIL.cmake"
  "-DCMAKE_C_COMPILER=${CC:-clang}" "-DCMAKE_CXX_COMPILER=${CXX:-clang++}"
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON)
if command -v ld.lld > /dev/null; then
  args+=(-DLLVM_USE_LINKER=lld)
fi
if command -v sccache > /dev/null; then
  args+=(-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache)
fi

start=$SECONDS
echo "[msil-ci] cmake ${args[*]}"
rc=0
cmake "${args[@]}" || rc=$?
status=PASS
((rc == 0)) || status=FAIL
ci_result configure ci "$status" $((SECONDS - start)) "cmake -C msil/cmake/caches/MSIL.cmake (exit $rc)"
ci_summary 'MSIL CI: configure'
