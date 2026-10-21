#!/bin/bash
# Rebuilds and reruns every exports-multiasm experiment on Windows (.NET 8, .NET 10,
# .NET Framework 4.8). Linux legs: e1/lnx-run.sh, e4/lnx-run.sh, e4b/lnx-run.sh and
# e5/lnx-run.sh under 'MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash <path>'.
cd "$(dirname "$0")"; . ./common.sh
filt() { grep -v "^   at \|^     at \|WRN:\|Fusion\|registry value\|performance penalty\|^  $"; }
echo "######## E1 export class, missing members, versioning"
(cd e1 && asm_dll zlibx.il zlibx.dll && sed 's/@VER@/.ver 1:2:3:4/' prog.il.in > prog.il && asm_exe prog.il prog.exe
 for t in ok missing badsig noclass noasm global cold; do echo "===== $t"; runall . prog.exe $t | filt; done
 for d in ver-eq ver-lower ver-rev ver-major ver-none ver-pkt name-a name-b; do echo "===== $d"; runall $d prog.exe ok | filt; done)
echo "######## E2 exported data and function addresses"
(cd e2 && asm_dll lib2.il lib2.dll && asm_exe prog2.il prog2.exe && asm_exe prog2c.il prog2c.exe
 echo "===== prog2 (target first called directly)"; runall . prog2.exe
 echo "===== prog2c (target first called through calli)"; runall . prog2c.exe)
(cd e2b && asm_exe ident.il ident.exe && asm_exe ident2.il ident2.exe; echo "===== e2b ident (call first)"; runall . ident.exe; echo "===== e2b ident2 (calli first)"; runall . ident2.exe)
echo "######## E3 symbol table encodings (1000 functions + 50 data)"
(cd e3 && for v in base attr-a attr-b attr-t attr-n res-a fnacc fnfld proposed; do echo "===== $v"; ../tools/symtab/bin/symtab.exe stats out-$v/symlib.dll; done
 for v in a b t n; do echo "===== read attr-$v"; ../tools/symtab/bin/symtab.exe read out-attr-$v/symlib.dll x86_64-pc-windows-msvc19.33.0-msil $v; done
 echo "===== read res-a"; ../tools/symtab/bin/symtab.exe read out-res-a/symlib.dll x86_64-pc-windows-msvc-msil a
 for t in x86_64-unknown-linux-gnu-msil x86_64-w64-windows-gnu-msil aarch64-pc-windows-msvc-msil; do ../tools/symtab/bin/symtab.exe read out-attr-n/symlib.dll $t n; done
 ../tools/symtab/bin/symtab.exe read out-base/symlib.dll x86_64-pc-windows-msvc-msil n
 echo "===== #MSIL metadata stream"; ../tools/symtab/bin/symtab.exe stats stream/zlibx.dll | head -8; runall stream prog.exe ok
 echo "===== eager address publication, 1000 exports (ms, three runs)"
 for m in none ldftn delegate; do for k in 1 2 3; do echo "$m: $(runall time/$m tprog.exe | grep -o '[0-9.]* ms' | tr '\n' ' ')"; done; done)
echo "######## E4 module initializer timing"
(cd e4 && asm_dll lib4.il lib4.dll && asm_exe prog4.il prog4.exe; for t in pure data ldftn field typeof load cold reflect none; do echo "===== $t"; runall . prog4.exe $t; done)
(cd e4b && for v in 1 2; do echo "===== e4b variant $v"; runall v$v prog.exe; done)
(cd e4c && for p in none ab ba; do echo "===== e4c program touches: $p"; runall . prog_$p.exe; done; echo "===== e4c ReadyToRun images (net8)"; (cd r2r && dotnet exec --runtimeconfig ../../rc8.json prog_ab.exe))
echo "######## E5 C library state across assemblies"
(cd e5 && for d in win-ucrtbase-ucrtbase win-msvcrt-ucrtbase; do for t in heap stdio errno; do echo "===== $d $t"; runall $d crtprog.exe $t | filt; done; done)
(cd e5c && for v in separate shared; do echo "===== e5c $v"; runall $v dprog.exe; done)
echo "######## E6 C# consumers and C# libraries"
(cd e6 && echo "===== C# (net8 build) on net8/net10"; (cd net8 && dotnet exec --runtimeconfig ../../rc8.json use.dll && dotnet exec --runtimeconfig ../../rc10.json use.dll); echo "===== C# (net48 build)"; (cd net48 && ./use.exe))
(cd e6b && echo "===== IL calling a C# library"; runall . cprog.exe)
(cd e6r2r/pub && echo "===== ReadyToRun C# app + ReadyToRun C library"; ./r2r.exe)
echo "######## E7 end-to-end prototype"
(cd e7 && runall . main.exe; echo "===== missing library"; runall nolib main.exe | filt | cut -c1-200; echo "===== accessor bench"; runall . bench.exe | grep -v constructor; echo "===== accessor bench TC=0"; DOTNET_TieredCompilation=0 runall . bench.exe | grep -v constructor)
