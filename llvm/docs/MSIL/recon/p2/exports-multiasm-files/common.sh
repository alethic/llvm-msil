# Shared helpers for the exports-multiasm experiments (bash, Git Bash on Windows).
FWILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
NEWILASM=/d/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe
SCR=/c/llvm-msil-scratch/p2/exports-multiasm
# asm_dll <il> <out.dll>   asm_exe <il> <out.exe>
asm_dll() { "$FWILASM" -nologo -quiet -dll -pe64 -x64 -output="$2" "$1" >/dev/null || { echo "ILASM FAILED: $1"; "$FWILASM" -nologo -dll -pe64 -x64 -output="$2" "$1" | tail -5; }; }
asm_exe() { "$FWILASM" -nologo -quiet -exe -pe64 -x64 -output="$2" "$1" >/dev/null || { echo "ILASM FAILED: $1"; "$FWILASM" -nologo -exe -pe64 -x64 -output="$2" "$1" | tail -5; }; }
# runall <dir> <prog.exe> [args...]: .NET 8, .NET 10 and .NET Framework 4.8
runall() {
  local d=$1 p=$2; shift 2
  ( cd "$d" || exit 1
    echo "--- net8";  timeout 60 dotnet exec --runtimeconfig "$SCR/rc8.json"  "$p" "$@" 2>&1 | tr -d '\r' | sed 's/^/  /'; echo "  [exit ${PIPESTATUS[0]}]"
    echo "--- net10"; timeout 60 dotnet exec --runtimeconfig "$SCR/rc10.json" "$p" "$@" 2>&1 | tr -d '\r' | sed 's/^/  /'; echo "  [exit ${PIPESTATUS[0]}]"
    echo "--- net48"; timeout 60 ./"$p" "$@" 2>&1 | tr -d '\r' | sed 's/^/  /'; echo "  [exit ${PIPESTATUS[0]}]"
  )
}
