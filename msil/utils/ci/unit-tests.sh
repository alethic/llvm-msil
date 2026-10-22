#!/usr/bin/env bash
# unit-tests.sh - the MSIL unit tests: step 3 of the MSIL CI (Linux job).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/unit-tests.sh [BUILD_DIR]       (default <repo>/build)
#
# The same checks as unit-tests.ps1 (msil-gtest.ps1 -NoSkip -MinTests 1): TargetParserTests,
# MCTests and MSILTests with the filter below, and from M13 CLITests with the filter '*' (its
# suites are named CLI<Topic> without 'MSIL', plan 5.7.13); a binary fails on a non-zero exit,
# on a '[  SKIPPED ]' line (GTEST_SKIP exits 0) and when no '[  PASSED  ] N test(s).' line with
# N >= 1 is printed. The filter excludes TargetRegistry.MSILLookupWithoutTarget, which M1.e
# requires to skip whenever the msil target is registered (always, from M2 on; plan section
# 12). Every binary runs even after a failure.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"
build=${1:-$MSIL_REPO/build}

msil_filter='*MSIL*:TargetRegistry.*:-TargetRegistry.MSILLookupWithoutTarget'
# <binary> <gtest filter>
binaries=("unittests/TargetParser/TargetParserTests $msil_filter"
          "unittests/MC/MCTests $msil_filter"
          "unittests/Target/MSIL/MSILTests $msil_filter"
          "unittests/CLI/CLITests *")
logs=$(mktemp -d)
trap 'rm -rf "$logs"' EXIT

for entry in "${binaries[@]}"; do
  b=${entry%% *}
  filter=${entry#* }
  name=$(basename "$b")
  log=$logs/$name.log
  start=$SECONDS
  rc=0
  echo "::group::$build/$b --gtest_filter=$filter"
  "$build/$b" "--gtest_filter=$filter" --gtest_color=no 2>&1 | tee "$log" || rc=$?
  echo "::endgroup::"
  problems=()
  ((rc == 0)) || problems+=("exit code $rc")
  passed=$(sed -n 's/^\[  PASSED  \] \([0-9]*\) tests\{0,1\}\..*/\1/p' "$log" | tail -n 1)
  if [[ -z $passed ]]; then
    problems+=("no '[  PASSED  ] N tests.' line")
  elif ((passed < 1)); then
    problems+=("no test passed")
  fi
  skipped=$(grep -c '^\[  SKIPPED \]' "$log" || true)
  ((skipped == 0)) || problems+=("$skipped '[  SKIPPED ]' line(s)")
  if ((${#problems[@]} == 0)); then
    ci_result unit-tests "$name" PASS $((SECONDS - start)) "$passed test(s) passed"
  else
    detail=$(printf '%s; ' "${problems[@]}")
    detail=${detail%; }
    ci_result unit-tests "$name" FAIL $((SECONDS - start)) "$detail"
  fi
done

ci_summary 'MSIL CI: unit tests'
