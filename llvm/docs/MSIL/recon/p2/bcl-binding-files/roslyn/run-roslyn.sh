#!/bin/bash
# Roslyn (csc from the .NET SDK 10.0.402, and the VS 2026 Framework-hosted
# csc.exe) against several reference sets: which AssemblyRefs it emits, and
# its diagnostics for missing, ambiguous and duplicate references.
CSC="C:/Program Files/dotnet/sdk/10.0.402/Roslyn/bincore/csc.dll"
VSCSC="/c/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/Roslyn/csc.exe"
FX="C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8"
R8="C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0"
R10="C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/10.0.12/ref/net10.0"
NS20="D:/packages/NuGet/netstandard.library/2.0.3/build/netstandard2.0/ref"
A8=(); for f in "$R8"/*.dll; do A8+=("-r:$f"); done
A8NOFAC=(); for f in "$R8"/*.dll; do case "$f" in */mscorlib.dll) ;; *) A8NOFAC+=("-r:$f");; esac; done
AFX=(); for f in "$FX"/*.dll "$FX"/Facades/*.dll; do case "$f" in *EnterpriseServices.Wrapper*|*EnterpriseServices.Thunk*) ;; *) AFX+=("-r:$f");; esac; done
ANS=(); for f in "$NS20"/*.dll; do ANS+=("-r:$f"); done
run() { local n="$1"; shift; echo "### $n"; dotnet "$CSC" -nologo -noconfig -unsafe -out:$n.exe "$@" 2>&1 | sed 's/^.*roslyn[\/]//' | sort | uniq -c | sort -rn | head -6; echo "    (exit ${PIPESTATUS[0]}; output $( [ -f $n.exe ] && echo written || echo none ))"; }
rm -f r*.exe
run r1-net8 -nostdlib "${A8[@]}" p1.cs
run r2-net48 -nostdlib "${AFX[@]}" p1.cs
run r3-ns20 -nostdlib "${ANS[@]}" p1.cs
run r4-net8-mscorlib-facade-only -nostdlib "-r:$R8/mscorlib.dll" p1.cs
run r5-net8-System.Runtime-only -nostdlib "-r:$R8/System.Runtime.dll" p1.cs
run r6-net48-mscorlib+net8-SR-SRI-ST -nostdlib "-r:$FX/mscorlib.dll" "-r:$R8/System.Runtime.dll" "-r:$R8/System.Runtime.InteropServices.dll" "-r:$R8/System.Threading.dll" p1.cs
run r7-fma-net48 -nostdlib "${AFX[@]}" fma.cs
run r8-dotnet-csc-no-refs p1.cs
run r9-nostdlib-no-refs -nostdlib p1.cs
run r10-net8-SR+net10-SR -nostdlib "-r:$R8/System.Runtime.dll" "-r:$R10/System.Runtime.dll" "-r:$R8/System.Runtime.InteropServices.dll" "-r:$R8/System.Threading.dll" p1.cs
run r13-net48-mscorlib+net8-SRI -nostdlib "-r:$FX/mscorlib.dll" "-r:$R8/System.Runtime.InteropServices.dll" p1.cs
run r14-net8+net48-mscorlib-last -nostdlib "${A8[@]}" "-r:$FX/mscorlib.dll" p1.cs
run r14b-net48-mscorlib-first+net8 -nostdlib "-r:$FX/mscorlib.dll" "${A8NOFAC[@]}" "-r:$R8/mscorlib.dll" p1.cs
run r15-net8+net10-SRI -nostdlib "${A8[@]}" "-r:$R10/System.Runtime.InteropServices.dll" p1.cs
run r17-fma-net8 -nostdlib "${A8[@]}" fma.cs
echo "### r11-vs-csc.exe-no-refs (Framework-hosted csc, default mscorlib)"; "$VSCSC" -nologo -noconfig -unsafe -out:r11-vscsc-default.exe p1.cs; echo "    (exit $?)"
