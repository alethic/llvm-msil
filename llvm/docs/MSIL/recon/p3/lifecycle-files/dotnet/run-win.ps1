# Runs the lifecycle probe on .NET Framework 4.8 (the EXE), .NET 8 and .NET 10 (dotnet <dll>) in each mode, with
# stderr and stdout merged into one pipe by cmd (2>&1), a 60 s limit, and prints the trace, the exit code and the
# wall time of the whole process.
param([string]$Modes, [string]$Runtimes = 'net48,net8.0,net10.0')
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($rt in $Runtimes.Split(',')) {
  foreach ($m in $Modes.Split(',')) {
    $cmd = if ($rt -eq 'net48') { "`"$root\bin\Release\net48\lifecycle.exe`" $m" }
           else { "dotnet `"$root\bin\Release\$rt\lifecycle.dll`" $m" }
    "=== $rt $m"
    $psi = [System.Diagnostics.ProcessStartInfo]::new('cmd.exe', "/d /c `"$cmd 2>&1`"")
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync()
    if (-not $p.WaitForExit(60000)) { $p.Kill($true); "TIMEOUT (killed after 60 s)" }
    $p.WaitForExit()
    $o.Result.TrimEnd()
    "status=$($p.ExitCode) (0x{0:X8}) wall={1} ms" -f $p.ExitCode, $sw.ElapsedMilliseconds
  }
}
