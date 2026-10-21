#!/bin/bash
# Run every reader over every variant; one summary line per tool and file.
# Tools: NETFX 4.8 ildasm and PEVerify, ildasm 10.0.12 (NuGet), dotnet-ilverify 10.0.12,
# System.Reflection.Metadata (mdinfo), VS-bundled LLVM 22.1.3 llvm-readobj/llvm-objdump/llvm-nm.
export MSYS_NO_PATHCONV=1
FWILDASM="C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64/ildasm.exe"
PEVERIFY="C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64/PEVerify.exe"
MILDASM=D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ildasm/10.0.12/runtimes/win-x64/native/ildasm.exe
RT8="C:/Program Files/dotnet/shared/Microsoft.NETCore.App/8.0.31"
L="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
MD=../tools/bin/mdinfo/mdinfo.exe
for f in "$@"; do
  echo "=== $f"
  out=$("$FWILDASM" /text /nobar "$f" 2>&1); echo "ildasm 4.8: rc=$? .method lines=$(echo "$out" | grep -c '\.method')"
  out=$("$MILDASM" "$f" 2>&1); echo "ildasm 10.0.12: rc=$? .method lines=$(echo "$out" | grep -c '\.method')"
  out=$(ilverify "$f" -r "$RT8/*.dll" 2>&1); echo "ilverify: rc=$? :: $(echo "$out" | grep -v '^$' | sed 's/\[[A-Z]:[^]]*: /[/' | head -4 | tr '\n' '|' | cut -c1-400)"
  out=$("$PEVERIFY" /nologo /il "$f" 2>&1); echo "peverify /il: rc=$? :: $(echo "$out" | grep -v '^$' | sed 's/\[IL\]: Error: \[[^]]*\]//' | head -4 | tr '\n' '|' | cut -c1-400)"
  out=$($MD pe "$f" 2>&1); echo "System.Reflection.Metadata: rc=$? :: $(echo "$out" | grep -E '^kind|^rows|EXCEPTION|stream .#MSIL' | tr '\n' '|' | cut -c1-400)"
  out=$("$L/llvm-readobj" --file-headers "$f" 2>&1); echo "llvm-readobj: rc=$? :: $(echo "$out" | grep -E 'Format|Machine:|error' | tr '\n' '|')"
  out=$("$L/llvm-objdump" -h "$f" 2>&1); echo "llvm-objdump -h: rc=$? :: $(echo "$out" | grep -E '^ +[0-9]+ ' | awk '{print $2}' | tr '\n' ' ')"
  out=$("$L/llvm-nm" "$f" 2>&1); echo "llvm-nm: rc=$? :: $(echo "$out" | head -2 | tr '\n' '|')"
done
