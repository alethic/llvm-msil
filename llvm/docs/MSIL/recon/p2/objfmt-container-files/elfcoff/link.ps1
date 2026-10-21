. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p2\objfmt-container\elfcoff
foreach ($f in 'cil-coff.o','cil-coff-unk.o','cil-coff-cee.o') {
  "== link.exe $f"
  link /nologo /dll /noentry /force:unresolved $f /out:x_$($f -replace '\.o$','').dll 2>&1 | Select-Object -First 4
  "rc=$LASTEXITCODE"
  "== lib.exe $f"
  lib /nologo $f /out:l_$($f -replace '\.o$','').lib 2>&1 | Select-Object -First 3
  "rc=$LASTEXITCODE"
}
