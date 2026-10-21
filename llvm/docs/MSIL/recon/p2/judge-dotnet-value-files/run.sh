#!/bin/bash
# judge-dotnet-value: re-runs every experiment of the report (Git Bash, Windows host).
# Inputs: the objfmt-binding scratch (zoo objects a.o/b.o built by its tools/mkobj.sh and the
# msilobj prototype linker), the objfmt-container scratch (cil-elf-em.o).
set -u
S=C:/llvm-msil-scratch/p2/judge-dotnet-value
B=C:/llvm-msil-scratch/p2/objfmt-binding
L=$B/tools/msilobj/out/msilobj.dll
V="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
ILASM48=C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
ILASM10=D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe
ILD48="C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64/ildasm.exe"
ILD10=D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ildasm/10.0.12/runtimes/win-x64/native/ildasm.exe
PEV="C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64/PEVerify.exe"
ILSPY=$S/tools/ilspycmd.exe        # dotnet tool install ilspycmd --tool-path $S/tools
CSC="C:/Program Files/dotnet/sdk/10.0.402/Roslyn/bincore/csc.dll"
NETFX_MSCORLIB="C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8/mscorlib.dll"
export MSYS_NO_PATHCONV=1

echo "== J0 re-run objfmt-binding zoo link (option 2)"
mkdir -p $S/zoo && cd $S/zoo && cp $B/zoo/{a,b}.o $B/zoo/rc8.json $B/zoo/rc10.json .
dotnet $L link -o zoo.dll a.o b.o | tail -1
dotnet $L link --exe -o zoo.exe a.o b.o > /dev/null
dotnet exec --runtimeconfig rc8.json zoo.dll; echo "net8 exit $?"
dotnet exec --runtimeconfig rc10.json zoo.dll; echo "net10 exit $?"
./zoo.exe; echo "netfx exit $?"

echo "== J1 ILSpy on a B object and on the linked output"
"$ILSPY" --version | head -1
"$ILSPY" a.o | head -40
"$ILSPY" zoo.dll | grep -c 'static'

echo "== J2 B object + COFF symbol-table view: llvm-nm, llvm-ar, GNU nm/ar, .NET readers, linkers"
mkdir -p $S/coffsym2 && cd $S/coffsym2
python $S/coffsym/coffsym.py ../zoo/a.o a.o T:main T:weakfn t:ctor_a t:helper D:table D:pcounter D:msg t:va t:local_g C:common_c:4 'T:??_C@_05CJBACGMB@hello?$AA@' 'T:??_C@_04GHJNJNPO@main?$AA@' U:add3 U:twice U:alias_twice U:b_extra U:counter U:puts U:maybe
python $S/coffsym/coffsym.py ../zoo/b.o b.o T:add3 T:twice T:weakfn T:b_extra T:alias_twice t:helper t:ctor_b D:counter D:msg_b 'T:??_C@_05CJBACGMB@hello?$AA@' 'T:??_C@_04JCHODAIH@add3?$AA@' t:local_g t:vb t:idx C:common_c:4
"$V/llvm-nm.exe" ../zoo/a.o
"$V/llvm-nm.exe" a.o
rm -f libab.a; "$V/llvm-ar.exe" rcs libab.a a.o b.o && "$V/llvm-nm.exe" --print-armap libab.a | sed -n '1,17p'
dotnet $L link -o z.dll a.o libab.a | grep extracted
dotnet exec --runtimeconfig ../zoo/rc8.json z.dll; echo "net8 exit $?"
"$ILD10" a.o | grep -c '\.method'; "$ILD48" /text a.o | grep -c '\.method'; "$ILSPY" a.o | head -3
"$V/lld-link.exe" a.o b.o /out:x.exe 2>&1 | head -1; "$V/ld.lld.exe" a.o 2>&1 | head -1
"$V/llvm-lib.exe" /out:ab.lib a.o b.o 2>&1 | head -1
wsl -d Ubuntu -- bash -c "cd /mnt/c/llvm-msil-scratch/p2/judge-dotnet-value/coffsym2 && nm a.o | head -4 && rm -f g.a && ar rcs g.a a.o b.o && nm --print-armap g.a | sed -n '1,6p'"

echo "== J3 ildasm/ilasm round trip of objects, relink, run"
for o in a b; do mkdir -p $S/rt2/$o; (cd $S/rt2/$o && "$ILD48" /nobar /utf8 /out=$o.rt.il ../../zoo/$o.o > /dev/null && "$ILASM48" /nologo /quiet /dll /pe64 /x64 /output=$o.o $o.rt.il); done
cd $S/rt2 && dotnet $L link -o rt.dll a/a.o b/b.o | tail -1
dotnet exec --runtimeconfig ../zoo/rc8.json rt.dll; echo "net8 exit $?"
dotnet $L link --exe -o rt.exe a/a.o b/b.o > /dev/null; ./rt.exe; echo "netfx exit $?"

echo "== J4 C# netmodule with the B placeholder convention"
cd $S/csmod && dotnet "$CSC" -nologo -noconfig -nostdlib -deterministic -target:module -r:"$NETFX_MSCORLIB" -out:cslib.netmodule cslib.cs
"$ILSPY" -il cslib.netmodule | grep -E 'pinvokeimpl|call '
dotnet $L link -o t.dll ../zoo/a.o ../zoo/b.o cslib.netmodule 2>&1 | head -1

echo "== J5 per-object Portable PDB through ilasm 10 (object-shaped module)"
cd $S/pdb && "$ILASM10" -nologo -quiet -dll -pe64 -x64 -debug=opt -det -output=obj.o obj.il && head -c 4 obj.pdb; echo
dotnet $S/tools-src/pdbdump/out/pdbdump.dll obj.o obj.pdb

echo "== J6 initonly stores outside .cctor (per-object init methods)"
cd $S/initonly && "$ILASM48" /nologo /quiet /exe /output=initchk.exe init.il && ./initchk.exe; echo "netfx exit $?"
dotnet exec --runtimeconfig rc8.json initchk.exe; echo "net8 exit $?"
ilverify initchk.exe -r "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/mscorlib.dll" | head -2
"$PEV" /nologo /il initchk.exe | head -2

echo "== J7 typed link-semantics attributes rendered by ILSpy and ildasm"
cd $S/ca && "$ILASM48" /nologo /quiet /dll /pe64 /x64 /output=obj.o obj.il && "$ILSPY" obj.o | head -24

echo "== J8 D object (objfmt-container cil-elf-em.o) in .NET and LLVM tools"
E=C:/llvm-msil-scratch/p2/objfmt-container/elfcoff/cil-elf-em.o
"$V/llvm-nm.exe" $E | head -4; "$ILSPY" $E 2>&1 | head -1; "$ILD10" $E 2>&1 | head -1
