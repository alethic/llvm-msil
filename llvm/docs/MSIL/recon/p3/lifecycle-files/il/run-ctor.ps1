# Rebuilds the IL prototype and runs the exit-from-a-constructor cases (program and library constructor) plus the
# normal modes on .NET Framework 4.8, .NET 8 and .NET 10, with a 30 s deadlock limit.
param([string]$Modes = 'ret,exit3,libexit,nested,exitinhandler,thread')
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)
python gen.py
$ilasm = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe'
& $ilasm -nologo -quiet -dll -pe64 -x64 plib.il '-output=plib.dll'
& $ilasm -nologo -quiet -exe -pe64 -x64 pprog.il '-output=pprog.exe'
$cases = @()
foreach ($m in $Modes.Split(',')) { $cases += , @($m, '') }
$cases += , @('ret', 'PPROG_EXIT_IN_CTOR')
$cases += , @('ret', 'PLIB_EXIT_IN_CTOR')
foreach ($rt in 'net48', 'net8.0', 'net10.0') {
  foreach ($c in $cases) {
    $m = $c[0]; $envName = $c[1]
    if ($envName) { Set-Item "env:$envName" '1' }
    "=== $rt $m $envName"
    $cmd = if ($rt -eq 'net48') { ".\pprog.exe $m" } else { "dotnet exec --runtimeconfig pprog.$rt.rc.json pprog.exe $m" }
    $p = Start-Process -FilePath cmd.exe -ArgumentList "/d /c $cmd > run.tmp 2>&1" -NoNewWindow -PassThru
    if (-not $p.WaitForExit(30000)) {
      "TIMEOUT 30 s (deadlock)"
      Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'pprog' -and $_.Name -ne 'cmd.exe' } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    }
    $p.WaitForExit()
    Get-Content run.tmp
    "status=$($p.ExitCode)"
    if ($envName) { Remove-Item "env:$envName" }
  }
}
