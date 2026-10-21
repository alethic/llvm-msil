#!/usr/bin/env bash
# build.sh - build what the MSIL tests need, then jitall: step 2 of the MSIL CI (Linux job).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/build.sh [BUILD_DIR]            (default <repo>/build)
#
# The same targets as build.ps1 (the two scripts keep the same list): the M11 build targets
# (plan M11), clang llc opt FileCheck not count split-file llvm-config llvm-profdata
# TargetParserTests MCTests MSILTests. TargetParserTests and MCTests are for unit-tests.sh;
# llvm-profdata is for the upstream lit tests that need it (upstream-hooks.ps1 runs them in the
# Windows job). jitall (msil/utils/jitall/build-jitall.py) is then compiled offline into
# <build>/tools/msil/jitall.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"
build=${1:-$MSIL_REPO/build}

targets=(clang llc opt FileCheck not count split-file llvm-config llvm-profdata TargetParserTests MCTests
         MSILTests)

start=$SECONDS
rc=0
ninja -C "$build" "${targets[@]}" || rc=$?
status=PASS
((rc == 0)) || status=FAIL
ci_result build ninja "$status" $((SECONDS - start)) "${targets[*]} (exit $rc)"

if ((rc == 0)); then
  start=$SECONDS
  jrc=0
  python3 "$MSIL_REPO/msil/utils/jitall/build-jitall.py" --build-dir "$build" || jrc=$?
  status=PASS
  ((jrc == 0)) || status=FAIL
  ci_result build jitall "$status" $((SECONDS - start)) "build-jitall.py (exit $jrc)"
fi

ci_summary 'MSIL CI: build'
