#!/bin/bash
# judge-cost-risk: re-checks behind llvm/docs/MSIL/recon/p2/judge-cost-risk.md.
# Expects the scratch layout C:\llvm-msil-scratch\p2\judge-cost-risk (d/, b2/, e/, initonly/, tables/, hdr/)
# and the shared build D:\llvm-msil\build. Run from Git Bash.
export MSYS_NO_PATHCONV=1
S=/c/llvm-msil-scratch/p2/judge-cost-risk
B=/d/llvm-msil/build/bin
V="/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
ILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
PV="/c/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64/PEVerify.exe"
ILV=$(command -v ilverify || echo "$USERPROFILE/.dotnet/tools/ilverify")
REF="C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8/mscorlib.dll"
RC8='{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}'

echo "### J1 option D: CIL in ELF ET_REL with an unknown e_machine (objfmt-container 6.2 re-run)"
cd $S/d
$B/clang --target=x86_64-unknown-linux-gnu -c cil-elf.s -o x86.o
$B/clang --target=x86_64-unknown-linux-gnu -c linkorder.s -o lo.o
python - <<'PY'
import struct
for src,dst,em in [('x86.o','em0c11.o',0xC11),('x86.o','emnone.o',0),('lo.o','lo0c11.o',0xC11)]:
    b=bytearray(open(src,'rb').read()); struct.pack_into('<H',b,18,em); open(dst,'wb').write(b)
PY
"$V/llvm-readobj" -h em0c11.o | grep -E 'Format|Machine'
"$V/llvm-nm" em0c11.o
rm -f libd.a; "$V/llvm-ar" rcs libd.a em0c11.o lo0c11.o; "$V/llvm-nm" --print-armap libd.a | sed -n 1,11p
"$V/llvm-objdump" -r em0c11.o | sed -n 3,9p
"$V/llvm-readobj" --section-groups lo0c11.o | grep -E 'Signature|\.cil|\.msil'
"$V/ld.lld" -r em0c11.o -o out.o 2>&1 | head -1

echo; echo "### J2 option B/2: two ECMA-335 module objects linked by name (objfmt-binding E2 re-run)"
cd $S/b2/zoo
bash ../tools/mkobj.sh x86_64-pc-windows-msvc-msil a.c stub maybe=memberref
bash ../tools/mkobj.sh x86_64-pc-windows-msvc-msil b.c
dotnet ../tools/msilobj/out/msilobj.dll link --exe -o zoo.exe a.o b.o | grep -E 'token sites|constructors'
./zoo.exe; echo "Framework 4.8 exit=$?"
dotnet ../tools/msilobj/out/msilobj.dll link -o zoo.dll a.o b.o > /dev/null
echo "$RC8" > rc8.json; dotnet exec --runtimeconfig rc8.json zoo.dll; echo ".NET 8 exit=$?"
"$V/llvm-nm" a.o 2>&1 | head -1
rm -f libab.a; "$V/llvm-ar" rcs libab.a a.o b.o; "$V/llvm-nm" --print-armap libab.a 2>&1 | head -2
"$V/llvm-readobj" -h a.o | grep -E 'Format'
"$V/llvm-objdump" -d a.o 2>&1 | sed -n 6,7p
"$V/lld-link" /out:x.exe a.o 2>&1 | head -1; "$V/ld.lld" a.o -o x 2>&1 | head -1

echo; echo "### J3 option E: bitcode objects today"
cd $S/e
$B/clang --target=x86_64-pc-windows-msvc-msil -O2 -c a.c -o a.o 2>&1 | head -1
$B/clang --target=x86_64-pc-windows-msvc-msil -O2 -flto -c a.c -o a.o
$B/clang --target=x86_64-pc-windows-msvc-msil -O2 -flto -c m.c -o m.o
head -c 4 a.o | od -An -tx1
$B/clang --target=x86_64-pc-windows-msvc-msil -O2 -S -x ir m.o -Xclang -mlink-bitcode-file -Xclang a.o -o am.il
"$ILASM" -nologo -quiet -exe -pe64 -x64 am.il -output=am.exe > /dev/null; ./am.exe; echo "Framework 4.8 exit=$?"

echo; echo "### J4 initonly address fields stored by a per-object init method vs by .cctor"
cd $S/initonly
for f in perobj cctor; do
  "$ILASM" -nologo -quiet -exe -pe64 -x64 $f.il -output=$f.exe > /dev/null
  ./$f.exe; echo "$f Framework exit=$?"
  echo "$RC8" > rc8.json; dotnet exec --runtimeconfig rc8.json $f.exe; echo "$f .NET 8 exit=$?"
  "$PV" /nologo /il $f.exe 2>&1 | grep -E 'Error|Verified' | sed 's/^.*\] Cannot/... Cannot/'
  "$ILV" $f.exe -r "$REF" 2>&1 | grep -E 'Error|Verified' | sed 's/^.*\] Cannot/... Cannot/'
done

echo; echo "### J5 metadata tables in a Phase-1 object vs a Phase-3/4-shaped object"
cd $S/tables
"$ILASM" -nologo -quiet -dll -pe64 -x64 p3shape.il -output=p3shape.o > /dev/null
dotnet tblcount/out/tblcount.dll ../b2/zoo/a.o ../b2/zoo/b.o p3shape.o

echo; echo "### J6 header dependents in the shared build (read-only parse of .ninja_deps/.ninja_log)"
python C:/llvm-msil-scratch/p2/judge-cost-risk/hdr/deps.py D:/llvm-msil/build llvm/binaryformat/elf.h llvm/binaryformat/coff.h llvm/object/elfobjectfile.h llvm/object/elf.h llvm/binaryformat/magic.h llvm/object/binary.h llvm/object/objectfile.h llvm/object/symbolicfile.h llvm/mc/mcsectionmsil.h llvm/mc/mccontext.h llvm/mc/mcsymbol.h llvm/mc/mcobjectfileinfo.h llvm/targetparser/triple.h

echo; echo "### J7 upstream churn of hook files (commits since 2024-10-21 / 2025-10-21; HEAD merged upstream 2026-10-19)"
cd /d/llvm-msil
for f in llvm/include/llvm/BinaryFormat/ELF.h llvm/include/llvm/Object/ELFObjectFile.h llvm/lib/Object/ELF.cpp llvm/lib/ObjectYAML/ELFYAML.cpp llvm/tools/llvm-readobj/ELFDumper.cpp llvm/lib/Object/SymbolicFile.cpp llvm/lib/Object/ObjectFile.cpp llvm/include/llvm/Object/Binary.h llvm/include/llvm/BinaryFormat/Magic.h llvm/lib/MC/TargetRegistry.cpp llvm/lib/MC/MCAsmBackend.cpp llvm/lib/MC/MCContext.cpp; do
  echo "$(git log --oneline --since=2024-10-21 -- $f | wc -l) $(git log --oneline --since=2025-10-21 -- $f | wc -l) $f"; done
echo "commits that changed EM_ constant lines in ELF.h (2y): $(git log --oneline --since=2024-10-21 -G'EM_[A-Z0-9]+ *=' -- llvm/include/llvm/BinaryFormat/ELF.h | wc -l)"
echo "commits that changed 'case ELF::EM_' lines in ELFObjectFile.h (2y): $(git log --oneline --since=2024-10-21 -G'case ELF::EM_' -- llvm/include/llvm/Object/ELFObjectFile.h | wc -l)"
echo "commits that changed EM_ lines in ELFYAML.cpp / ELF.cpp / ELFDumper.cpp (2y): $(git log --oneline --since=2024-10-21 -G'ECase\(EM_' -- llvm/lib/ObjectYAML/ELFYAML.cpp | wc -l) / $(git log --oneline --since=2024-10-21 -G'EM_' -- llvm/lib/Object/ELF.cpp | wc -l) / $(git log --oneline --since=2024-10-21 -G'EM_' -- llvm/tools/llvm-readobj/ELFDumper.cpp | wc -l)"
