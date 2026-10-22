#!/usr/bin/env bash
# build.sh - build what the MSIL tests need, then jitall: step 2 of the MSIL CI (Linux job).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/build.sh [BUILD_DIR]            (default <repo>/build)
#
# The ninja targets are those of msil/utils/ci/build-targets.txt, the one build-target list
# (plan 5.6.11), which build.ps1 and 'msil-build.ps1 -Standard' build on Windows: from M13 the
# M11 set, then lld and its copies, llvm-ar and its three copies, the binary tools, CLITests
# and LLDAsLibAllTests. One target per line; '#' starts a comment that runs to the end of the
# line, and blank lines are skipped. jitall (msil/utils/jitall/build-jitall.py) is then
# compiled offline into <build>/tools/msil/jitall.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"
build=${1:-$MSIL_REPO/build}

list_file=$MSIL_REPO/msil/utils/ci/build-targets.txt
targets=()
problem=
if [[ ! -r $list_file ]]; then
  problem="cannot read $list_file"
else
  n=0
  while IFS= read -r line || [[ -n $line ]]; do
    n=$((n + 1))
    line=${line%$'\r'}
    line=${line%%#*}
    # Trim leading and trailing blanks.
    line=${line#"${line%%[![:space:]]*}"}
    line=${line%"${line##*[![:space:]]}"}
    [[ -n $line ]] || continue
    if [[ $line =~ [[:space:]] ]]; then
      problem="$list_file:$n: one target per line, not '$line'"
      break
    fi
    targets+=("$line")
  done < "$list_file"
  if [[ -z $problem && ${#targets[@]} -eq 0 ]]; then
    problem="$list_file names no target"
  fi
fi

start=$SECONDS
rc=0
if [[ -n $problem ]]; then
  rc=1
  ci_result build ninja FAIL $((SECONDS - start)) "$problem"
else
  ninja -C "$build" "${targets[@]}" || rc=$?
  status=PASS
  ((rc == 0)) || status=FAIL
  ci_result build ninja "$status" $((SECONDS - start)) "build-targets.txt: ${targets[*]} (exit $rc)"
fi

if ((rc == 0)); then
  start=$SECONDS
  jrc=0
  python3 "$MSIL_REPO/msil/utils/jitall/build-jitall.py" --build-dir "$build" || jrc=$?
  status=PASS
  ((jrc == 0)) || status=FAIL
  ci_result build jitall "$status" $((SECONDS - start)) "build-jitall.py (exit $jrc)"
fi

ci_summary 'MSIL CI: build'
