# Runs the IL prototype on .NET Framework 4.8 (the EXE directly), .NET 8 and .NET 10 (dotnet exec with a
# runtimeconfig), stderr and stdout merged, and prints the exit code.
param([string]$Modes)
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($v in '8.0.0', '10.0.0') {
  $tfm = 'net' + $v.Substring(0, $v.LastIndexOf('.'))
  Set-Content -LiteralPath "$root\pprog.$tfm.rc.json" -Value ('{ "runtimeOptions": { "tfm": "' + $tfm + '", "framework": { "name": "Microsoft.NETCore.App", "version": "' + $v + '" } } }')
}
foreach ($rt in 'net48', 'net8.0', 'net10.0') {
  foreach ($m in $Modes.Split(',')) {
    $cmd = if ($rt -eq 'net48') { "`"$root\pprog.exe`" $m" } else { "dotnet exec --runtimeconfig `"$root\pprog.$rt.rc.json`" `"$root\pprog.exe`" $m" }
    "=== $rt $m"
    $out = cmd /d /c "$cmd 2>&1"
    $code = $LASTEXITCODE
    $out
    "status=$code"
  }
}
