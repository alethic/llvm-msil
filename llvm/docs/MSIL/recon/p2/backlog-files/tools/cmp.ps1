# cmp.ps1 -Src x.c [-Opt -O2] [-Extra "..."] : native oracle vs MSIL (.NET 8, .NET 10, Framework 4.8)
param([string]$Src, [string]$Opt = '-O2', [string]$Extra = '', [string]$Triple = 'x86_64-pc-windows-msvc-msil', [switch]$NoNative)
$ErrorActionPreference = 'Continue'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$bin = 'D:\llvm-msil\build\bin'
$stem = [IO.Path]::GetFileNameWithoutExtension($Src)
$dir = Split-Path -Parent (Resolve-Path $Src)
$tools = 'C:\llvm-msil-scratch\p2\backlog\tools'
Push-Location $dir
if (-not $NoNative) {
  & "$bin\clang.exe" $Opt -ffreestanding -o "$stem-native.exe" $Src 'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\lib\clang\22\lib\windows\clang_rt.builtins-x86_64.lib' 2>&1 | Out-Host
  $nat = & ".\$stem-native.exe"; $natExit = $LASTEXITCODE
  "native exit=$natExit"; $nat
}
$extraArgs = @(); if ($Extra) { $extraArgs = $Extra.Split(' ') }
$sw = [Diagnostics.Stopwatch]::StartNew()
& "$bin\clang.exe" --target=$Triple $Opt -ffreestanding -S -o "$stem.il" @extraArgs $Src 2>&1 | Out-Host
"clang exit=$LASTEXITCODE ($($sw.ElapsedMilliseconds) ms)"
& C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe -nologo -quiet -exe -pe64 -x64 "-output=$stem.exe" "$stem.il" | Out-Host
"ilasm exit=$LASTEXITCODE"
foreach ($rc in 'rc8','rc10') {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $out = & dotnet exec --runtimeconfig "$tools\$rc.json" "$stem.exe"; $e = $LASTEXITCODE
  "$rc exit=$e ($($sw.ElapsedMilliseconds) ms) same=$((($out -join "`n") -eq ($nat -join "`n")))"
  if (($out -join "`n") -ne ($nat -join "`n")) { $out }
}
$sw = [Diagnostics.Stopwatch]::StartNew()
$out = & ".\$stem.exe"; $e = $LASTEXITCODE
"netfx exit=$e ($($sw.ElapsedMilliseconds) ms) same=$((($out -join "`n") -eq ($nat -join "`n")))"
if (($out -join "`n") -ne ($nat -join "`n")) { $out }
Pop-Location
