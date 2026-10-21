#!/bin/bash
# zoo-all.sh: every binding prototype on the zoo program (zoo/a.c, zoo/b.c, zoo/c.c, zoo/d.c).
# Native oracle (clang + lld-link, x86_64-pc-windows-msvc): stdout "main\nadd3\n", exit 63.
S=/c/llvm-msil-scratch/p2/objfmt-binding
TOOLS=$S/tools; M="dotnet $TOOLS/msilobj/out/msilobj.dll"
ILASM="/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
RC8=$S/zoo/rc8.json; RC10=$S/zoo/rc10.json
run3() {  # $1 = dll, $2 = exe
  o8=$(dotnet exec --runtimeconfig $RC8 $1 2>&1 | tr '\n' ' '); e8=$?
  dotnet exec --runtimeconfig $RC8 $1 > /dev/null 2>&1; e8=$?
  dotnet exec --runtimeconfig $RC10 $1 > /dev/null 2>&1; e10=$?
  ofx=$($2 2>&1 | tr '\n' ' '); $2 > /dev/null 2>&1; efx=$?
  echo "    .NET 8: [$o8] exit $e8; .NET 10: exit $e10; .NET Framework 4.8: [$ofx] exit $efx"
}
for O in -O0 -O1 -O2; do
  W=$S/zoo-all/win$O; mkdir -p $W; cd $W; cp $S/zoo/{a,b,c,d}.c .
  for f in a b c d; do OPTLEVEL=$O $TOOLS/mkobj.sh x86_64-pc-windows-msvc-msil $f.c stub maybe=memberref > /dev/null 2>&1 || echo "mkobj $f failed"; done
  echo "== option 2, windows-msvc $O: a.o + b.o"
  $M link -o zoo.dll a.o b.o | sed 's/^/  /'; $M link --exe -o zoo.exe a.o b.o > /dev/null
  run3 zoo.dll ./zoo.exe
  rm -f libbcd.a; "/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/llvm-ar.exe" rcs libbcd.a b.o c.o d.o
  echo "== option 2, windows-msvc $O: a.o + libbcd.a (b.o c.o d.o; c.o defines the weak-undefined 'maybe')"
  $M link -o zooar.dll a.o libbcd.a 2>&1 | head -2 | sed 's/^/  /'; $M link --exe -o zooar.exe a.o libbcd.a > /dev/null 2>&1
  run3 zooar.dll ./zooar.exe
done
W=$S/zoo-all/win-O1
echo "== option 2, BCL references bound against reference assemblies (-O1)"
cd $W
$M link -o r8.dll --ref "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0" a.o b.o > /dev/null; $M link --exe -o r8.exe --ref "C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0" a.o b.o > /dev/null
echo "  net8.0 reference pack:"; run3 r8.dll ./r8.exe
$M link -o r48.dll --ref "C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8" a.o b.o > /dev/null; $M link --exe -o r48.exe --ref "C:/Program Files (x86)/Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8" a.o b.o > /dev/null
echo "  .NETFramework v4.8 reference assemblies:"; run3 r48.dll ./r48.exe
L=$S/zoo-all/linux-O1; mkdir -p $L; cd $L; cp $S/zoo/{a,b}.c . ; cp $RC8 $RC10 .
for f in a b; do $TOOLS/mkobj.sh x86_64-unknown-linux-gnu-msil $f.c stub maybe=memberref > /dev/null 2>&1; done
echo "== option 2, linux-gnu -O1 (merge_strings on '.str'), run in WSL"
$M link --linux -o zool.dll a.o b.o | sed 's/^/  /'
cat > run.sh <<'RUNEOF'
#!/bin/bash
cd "$(dirname "$0")"
for rc in rc8.json rc10.json; do out=$(dotnet exec --runtimeconfig $rc zool.dll); e=$?; echo "    WSL $rc: [$(echo $out)] exit $e"; done
RUNEOF
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/llvm-msil-scratch/p2/objfmt-binding/zoo-all/linux-O1/run.sh
N=$S/zoo-all/nomerge; mkdir -p $N; cd $N; rm -f *.dll *.exe *.il
echo "== option 4 (no merge): a.dll + b.dll unchanged, facade msil.extern.dll + driver zoo4.exe"
python $TOOLS/nomerge.py . zoo4 $W/a.o.il $W/a.link.json $W/b.o.il $W/b.link.json | sed 's/^/  /'
for x in a b msil.extern; do "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=$x.dll $x.il > /dev/null || echo "ilasm $x failed"; done
"$ILASM" -nologo -quiet -exe -pe64 -x64 -output=zoo4.exe zoo4.il > /dev/null || echo "ilasm zoo4 failed"
run3 zoo4.exe ./zoo4.exe
X=$S/zoo-all/lto; mkdir -p $X; cd $X; cp $S/zoo/{a,b}.c .
echo "== option 3 (IR-level link, clang -Xclang -mlink-bitcode-file), as is:"
CL=/d/llvm-msil/build/bin/clang.exe; T=x86_64-pc-windows-msvc-msil
$CL --target=$T -O1 -fcommon -flto -c b.c -o b.bc
$CL --target=$T -O1 -fcommon -S a.c -Xclang -mlink-bitcode-file -Xclang b.bc -o ab.il 2>&1 | sed 's/^/  /'
sed 's/^int alias_twice(int) __attribute__((alias("twice")));/int alias_twice(int x) { return twice(x); }/' b.c > b2.c
sed 's/^extern int maybe(void) __attribute__((weak));.*/static int (*const maybe)(void) = 0;/' a.c > a2.c
$CL --target=$T -O1 -fcommon -flto -c b2.c -o b2.bc
$CL --target=$T -O1 -fcommon -S a2.c -Xclang -mlink-bitcode-file -Xclang b2.bc -o ab.il
"$ILASM" -nologo -quiet -dll -pe64 -x64 -output=ab.dll ab.il > /dev/null; "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=ab.exe ab.il > /dev/null
echo "== option 3 with the alias replaced by a wrapper and the weak undefined by a null constant (expect 70: alias identity lost, +7):"
run3 ab.dll ./ab.exe
