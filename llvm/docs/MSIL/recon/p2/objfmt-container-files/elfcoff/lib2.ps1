. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p2\objfmt-container\elfcoff
foreach ($f in 'cil-coff.o','cil-coff-unk.o','cil-coff-cee.o') {
  "== llvm-lib $f"
  & "$MsilLlvmBin\llvm-lib.exe" /out:ll_$f.lib $f 2>&1 | Select-Object -First 2
  "rc=$LASTEXITCODE; index: " + ((& "$MsilLlvmBin\llvm-nm.exe" --print-armap ll_$f.lib 2>&1 | Select-Object -Skip 1 -First 3) -join '; ')
}
