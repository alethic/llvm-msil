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
# the gate steps 5-12 (M10 and M11) and 20-21 (M11) of plan section 6, and M12's legs; this
# job has no WSL legs: it runs the Linux triple natively, with msil_linux_sysroot=/, lit.cfg.py's
# default on Linux); the triple is x86_64-unknown-linux-gnu-msil, the triple of Design.md
# section 12:
#   net8         llc/clang FileCheck tests, harness and msil/test/M1 on .NET 8 (tiered), -O0 and
#                -O2, native oracle required (check-msil's paths)
#   net10        msil/test/M1 on .NET 10
#   net8-tc0     msil/test/M1 on .NET 8 with DOTNET_TieredCompilation=0 (optimizing JIT)
#   net10-tc0    msil/test/M1 on .NET 10 with DOTNET_TieredCompilation=0
#   more-levels  msil/test/M1 on .NET 8 at O1,O3,Os,Oz (O0 and O2 run in the legs above; .ll
#                tests run at O1 and O3 only); the Windows leg of the same name is M11 gate
#                step 21, and step 22 runs this triple at these levels under WSL (.NET 10)
#   roundtrip    msil/test/M1/11-programs through ildasm -> ilasm, then run (Design.md 5.5)
#   expected     update-expected.py --check msil/test/M1: the Linux job's native Linux oracle
#                (a native build of each test with the tree's clang and the system linker, run
#                on the runner) against <stem>.linux.expected where the Linux transcript
#                differs, else <stem>.expected (plan 5.12.11); on the development host,
#                lit-legs.ps1's expected-linux leg makes the same check through the E7 oracle
#   utc          run-utc-tests.py with the six update_llc_test_checks tests of lit-legs.ps1's
#                utc leg (M11 gate step 20): the same list, so both jobs check the same files,
#                and a test whose target the build lacks fails instead of being UNSUPPORTED
#   all-levels   --full: msil/test at O0,O1,O2,O3,Os,Oz
#   upstream     --full: ninja check-llvm check-clang (regressions our hooks cause upstream;
#                CI only: they build every tool, which the development host never does)
# ilasm and ildasm are the NuGet runtime.linux-x64.Microsoft.NETCore.ILAsm/ILDAsm packages
# ($MSIL_ILASM and $MSIL_ILDASM, which lit.cfg.py reads, or found in the NuGet global-packages
# folder); the roundtrip leg takes both from that folder at the version that
# msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion, read by lit.cfg.py), like
# lit-legs.ps1. ILVerify is the dotnet tool; there is no PEVerify on Linux.
#
# Every lit leg passes --param msil_require_verify=1 (plan D38) and --param msil_require_exec=1,
# writes <build>/lit-results/<leg>.xml (xunit) and <leg>.log, and passes when lit exits 0 and
# at least one test passed. The other legs write <build>/lit-results/<leg>.log and pass when
# their command exits 0. Every selected leg runs even after a failure; the exit status is 1 if
# any failed.
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

# name:kind (kind: lit, expected, utc, upstream; a 'full' suffix marks --full legs)
legs=(net8:lit net10:lit net8-tc0:lit net10-tc0:lit more-levels:lit roundtrip:lit
  expected:expected utc:utc all-levels:lit:full upstream:upstream:full)

# The tests of the utc leg; lit-legs.ps1 has the same list (keep the two the same).
# wasm-function-name is left out: it needs WebAssembly, which msil/cmake/caches/MSIL.cmake does
# not build.
utc_tests=(msil-basic x86-basic x86-mir-only x86-asm-mir-mixed x86-constant-pool asm-show-inst)

leg_paths() {
  case $1 in
    net8) printf '%s\n' "${check_msil[@]}" ;;
    net10 | net8-tc0 | net10-tc0 | more-levels) printf '%s\n' "${m1[@]}" ;;
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
    more-levels) printf '%s\n' msil_dotnet_runtime=8 msil_opt_levels=O1,O3,Os,Oz ;;
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

run_utc_leg() {
  # run-utc-tests.py, not lit, as on Windows (where lit reports these tests UNSUPPORTED): the
  # fixed list runs in both jobs, and a missing target fails the test.
  local log=$results/utc.log rc=0 summary
  python3 "$MSIL_REPO/msil/utils/run-utc-tests.py" --llc "$build/bin/llc" "${utc_tests[@]}" 2>&1 |
    tee "$log" || rc=$?
  summary=$(grep -E '^[0-9]+ of [0-9]+ tests passed$' "$log" | tail -n 1 || true)
  LEG_DETAIL="run-utc-tests.py: ${summary:-no summary} (exit $rc)"
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
    utc) run_utc_leg || status=FAIL ;;
    upstream) run_upstream_leg || status=FAIL ;;
  esac
  ci_result lit-legs "$name" "$status" $((SECONDS - start)) "$LEG_DETAIL"
done

ci_summary 'MSIL CI: test legs'
