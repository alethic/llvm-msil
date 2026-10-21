# Native Windows oracle: this tree's VS-bundled clang (x86_64-pc-windows-msvc) + lld-link, VS dev environment.
. D:\llvm-msil\msil\utils\build\msil-env.ps1 *> $null
Set-Location C:\llvm-msil-scratch\p2\judge-c-fidelity\probes
New-Item -ItemType Directory -Force native-win | Out-Null
$cl = "$MsilLlvmBin\clang.exe"
foreach ($v in @(@("p1ab","p1a.c","p1b.c"), @("p1ac","p1a.c","p1c.c"), @("p1mwb","p1m.c","p1w.c","p1b.c"),
                 @("p2","p2a.c","p2b.c"), @("sig","caller.c","callee.c"), @("p4","p4a.c","p4b.c"), @("p5","p5a.c","p5b.c"),
                 @("p6","p6a.c","p6b.c"), @("p8","p8.c"), @("p9ab","p9a.c","p9b.c"), @("p9ba","p9b.c","p9a.c"))) {
  $n = $v[0]; $srcs = $v[1..($v.Length-1)]
  & $cl --target=x86_64-pc-windows-msvc -O1 -std=gnu17 -fcommon -w -fuse-ld=lld @srcs -o "native-win\$n.exe" 2>&1 | Out-Null
  $o = (& ".\native-win\$n.exe" | Out-String).Trim(); "native windows ${n}: out=[$o] exit $LASTEXITCODE"
}
