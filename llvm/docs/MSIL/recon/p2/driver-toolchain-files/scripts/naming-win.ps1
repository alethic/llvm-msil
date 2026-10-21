# E1/E2: PE kind (EXE vs DLL image) x file name x host, on Windows.
$ErrorActionPreference = 'Continue'
$ilasm = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe'
$d = 'C:\llvm-msil-scratch\p2\driver-toolchain\naming'
Set-Location $d
$rc8  = '{ "runtimeOptions": { "tfm": "net8.0", "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }'
$rc10 = '{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "10.0.0" } } }'
Set-Content -Encoding ascii rc8.json $rc8
Set-Content -Encoding ascii rc10.json $rc10
function Build($il, $kind, $out) {
  & $ilasm -nologo -quiet "-$kind" -pe64 -x64 "-output=$out" $il | Out-Null
  if ($LASTEXITCODE -ne 0) { "ilasm failed for $out" }
}
function Run($label, [scriptblock]$sb) {
  $global:LASTEXITCODE = $null
  try { $o = & $sb 2>&1 | Out-String } catch { $o = 'cannot start: ' + $_.Exception.Message.Split([char]10)[0] }
  $first = ($o -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
  "{0,-58} exit={1,-11} {2}" -f $label, $LASTEXITCODE, $first
}
foreach ($kind in 'exe','dll') {
  foreach ($name in 'p.exe','p.dll','a.out','prog') {
    $dir = Join-Path $d "$kind-$($name.Replace('.','_'))"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $f = Join-Path $dir $name
    Build 'hello-win.il' $kind $f
    $stem = [IO.Path]::GetFileNameWithoutExtension($name)
    Set-Content -Encoding ascii (Join-Path $dir "$stem.runtimeconfig.json") $rc8
    Run "$kind image as $name : dotnet exec --runtimeconfig rc8" { dotnet exec --runtimeconfig (Join-Path $d rc8.json) $f }
    Run "$kind image as $name : dotnet <file> (rc next to it)" { dotnet $f }
    if ($name -like '*.exe') { Run "$kind image as $name : direct (Framework)" { & $f } }
  }
}
# E2: API-set DLL name for the UCRT
$dir = Join-Path $d 'apiset'; New-Item -ItemType Directory -Force $dir | Out-Null
Build 'hello-apiset.il' 'exe' (Join-Path $dir 'p.exe')
Run "api-ms-win-crt-stdio-l1-1-0 puts: .NET 8" { dotnet exec --runtimeconfig (Join-Path $d rc8.json) (Join-Path $dir 'p.exe') }
Run "api-ms-win-crt-stdio-l1-1-0 puts: .NET 10" { dotnet exec --runtimeconfig (Join-Path $d rc10.json) (Join-Path $dir 'p.exe') }
Run "api-ms-win-crt-stdio-l1-1-0 puts: Framework 4.8" { & (Join-Path $dir 'p.exe') }
