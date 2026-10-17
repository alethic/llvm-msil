#!/usr/bin/env bash
# lit-legs.sh - the test legs of the MSIL CI (Linux job): step 4 of msil-ci.yml.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   bash msil/utils/ci/lit-legs.sh [--full] [--leg NAME[,NAME...]] [--list] [BUILD_DIR]
#
# Legs, in order (lit-legs.ps1 has the Windows legs and the WSL legs of the development host,
# the M10 gate steps 5-12 of plan section 6); the triple is x86_64-unknown-linux-gnu-msil, the
# triple of Design.md section 12:
#   net8        llc/clang FileCheck tests, harness and msil/test/M1 on .NET 8 (tiered), -O0 and
#               -O2, native oracle required (check-msil's paths)
#   net10       msil/test/M1 on .NET 10
#   net8-tc0    msil/test/M1 on .NET 8 with DOTNET_TieredCompilation=0 (optimizing JIT)
#   net10-tc0   msil/test/M1 on .NET 10 with DOTNET_TieredCompilation=0
#   roundtrip   msil/test/M1/11-programs through ildasm -> ilasm, then run (Design.md 5.5)
#   expected    update-expected.py --check msil/test/M1 (.expected files vs a native build)
#   all-levels  --full: msil/test at O0,O1,O2,O3,Os,Oz
#   upstream    --full: ninja check-llvm check-clang (regressions our hooks cause upstream;
#               CI only: they build every tool, which the development host never does)
# ilasm and ildasm are the NuGet runtime.linux-x64.Microsoft.NETCore.ILAsm/ILDAsm packages
# ($MSIL_ILASM and $MSIL_ILDASM, which lit.cfg.py reads, or found in the NuGet global-packages
# folder); the roundtrip leg takes both from that folder at the version that
# msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion, read by lit.cfg.py), like
# lit-legs.ps1. ILVerify is the dotnet tool; there is no PEVerify on Linux.
#
# Every lit leg passes --param msil_require_verify=1 (plan D38) and --param msil_require_exec=1,
# writes <build>/lit-results/<leg>.xml (xunit) and <leg>.log, and passes when lit exits 0 and
# at least one test passed. Every selected leg runs even after a failure; the exit status is 1
# if any failed.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/ci-common.sh"

full=0
list=0
selected=()
build=
while (($#)); do
  case $1 in
    --full) full=1 ;;
    --list) list=1 ;;
    --leg)
      shift
      IFS=',' read -r -a more <<< "${1:?--leg needs a name}"
      selected+=("${more[@]}")
      ;;
    --leg=*)
      IFS=',' read -r -a more <<< "${1#--leg=}"
      selected+=("${more[@]}")
      ;;
    -*)
      echo "lit-legs.sh: unknown option $1" >&2
      exit 2
      ;;
    *) build=$1 ;;
  esac
  shift
done
build=${build:-$MSIL_REPO/build}

check_msil=("$MSIL_REPO/llvm/test/CodeGen/MSIL" "$MSIL_REPO/clang/test/CodeGen/MSIL"
  "$MSIL_REPO/clang/test/Preprocessor/init-msil.c" "$MSIL_REPO/clang/test/Driver/msil-toolchain.c"
  "$MSIL_REPO/msil/test")
m1=("$MSIL_REPO/msil/test/M1")
programs=("$MSIL_REPO/msil/test/M1/11-programs")
tc0=msil_dotnet_env=DOTNET_TieredCompilation=0

# name:kind (kind: lit, expected, upstream; a 'full' suffix marks --full legs)
legs=(net8:lit net10:lit net8-tc0:lit net10-tc0:lit roundtrip:lit expected:expected
  all-levels:lit:full upstream:upstream:full)

leg_paths() {
  case $1 in
    net8) printf '%s\n' "${check_msil[@]}" ;;
    net10 | net8-tc0 | net10-tc0) printf '%s\n' "${m1[@]}" ;;
    roundtrip) printf '%s\n' "${programs[@]}" ;;
    all-levels) echo "$MSIL_REPO/msil/test" ;;
  esac
}

leg_params() {
  case $1 in
    net8) printf '%s\n' msil_dotnet_runtime=8 msil_native_oracle=required ;;
    net10) echo msil_dotnet_runtime=10 ;;
    net8-tc0) printf '%s\n' msil_dotnet_runtime=8 "$tc0" ;;
    net10-tc0) printf '%s\n' msil_dotnet_runtime=10 "$tc0" ;;
    roundtrip) echo msil_roundtrip=1 ;;
    all-levels) echo msil_opt_levels=O0,O1,O2,O3,Os,Oz ;;
  esac
}

known=()
for l in "${legs[@]}"; do known+=("${l%%:*}"); done
for s in "${selected[@]}"; do
  if [[ " ${known[*]} " != *" $s "* ]]; then
    echo "lit-legs.sh: unknown leg '$s' (legs: ${known[*]})" >&2
    exit 2
  fi
done
if ((list)); then
  for l in "${legs[@]}"; do
    [[ $l == *:full ]] && echo "${l%%:*} (--full)" || echo "${l%%:*}"
  done
  exit 0
fi

results=$build/lit-results
mkdir -p "$results"
lit=$build/bin/llvm-lit

run_lit_leg() {
  local name=$1 log=$results/$1.log
  local -a paths params args
  mapfile -t paths < <(leg_paths "$name")
  mapfile -t params < <(leg_params "$name")
  params+=(msil_require_verify=1 msil_require_exec=1)
  args=(-sv "--xunit-xml-output=$results/$name.xml")
  for p in "${params[@]}"; do args+=(--param "$p"); done
  echo "[msil-ci] leg $name: llvm-lit ${args[*]} ${paths[*]}"
  local rc=0
  python3 "$lit" "${args[@]}" "${paths[@]}" 2>&1 | tee "$log" || rc=$?
  local counts passed
  counts=$(sed -n 's/^  \([A-Za-z][A-Za-z ]*[A-Za-z]\) *: *\([0-9]*\) (.*/\1 \2/p' "$log" |
    tr '[:upper:]' '[:lower:]' | paste -sd ',' - | sed 's/,/, /g')
  passed=$(sed -n 's/^  Passed *: *\([0-9]*\) (.*/\1/p' "$log" | tail -n 1)
  LEG_DETAIL="${counts:-no summary}; lit exit $rc"
  if ((rc == 0)) && ((${passed:-0} >= 1)); then
    return 0
  fi
  if ((rc == 0)); then
    LEG_DETAIL+="; no test passed"
  else
    # A configuration error (lit_config.fatal, e.g. a missing tool under msil_require_verify=1).
    local fatal
    fatal=$(sed -n 's/.*: fatal: //p' "$log" | head -n 1)
    [[ -z $fatal ]] || LEG_DETAIL+="; fatal: $fatal"
  fi
  return 1
}

run_expected_leg() {
  local log=$results/expected.log rc=0
  python3 "$MSIL_REPO/msil/utils/update-expected.py" --clang "$build/bin/clang" --check \
    "$MSIL_REPO/msil/test/M1" 2>&1 | tee "$log" || rc=$?
  LEG_DETAIL="update-expected.py --check msil/test/M1 (exit $rc)"
  return $((rc != 0))
}

run_upstream_leg() {
  local log=$results/upstream.log rc=0
  ninja -C "$build" check-llvm check-clang 2>&1 | tee "$log" || rc=$?
  LEG_DETAIL="ninja check-llvm check-clang (exit $rc)"
  return $((rc != 0))
}

for l in "${legs[@]}"; do
  name=${l%%:*}
  kind=${l#*:}
  kind=${kind%%:*}
  if ((${#selected[@]})); then
    [[ " ${selected[*]} " == *" $name "* ]] || continue
  elif [[ $l == *:full ]] && ((!full)); then
    continue
  fi
  start=$SECONDS
  LEG_DETAIL=
  status=PASS
  case $kind in
    lit) run_lit_leg "$name" || status=FAIL ;;
    expected) run_expected_leg || status=FAIL ;;
    upstream) run_upstream_leg || status=FAIL ;;
  esac
  ci_result lit-legs "$name" "$status" $((SECONDS - start)) "$LEG_DETAIL"
done

ci_summary 'MSIL CI: test legs'
