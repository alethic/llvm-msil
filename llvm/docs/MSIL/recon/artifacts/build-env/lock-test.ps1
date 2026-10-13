param([string]$Dir, [int]$HoldSeconds = 20, [double]$WaitMinutes = 1, [string]$Name = 'x')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
$t0 = Get-Date
try { $l = Enter-MsilBuildLock -BuildDir $Dir -What $Name -WaitMinutes $WaitMinutes }
catch { "$Name FAILED after $([int]((Get-Date)-$t0).TotalSeconds)s: $_"; exit 2 }
"$Name ACQUIRED after $([math]::Round(((Get-Date)-$t0).TotalSeconds,1))s pid=$PID"
Start-Sleep -Seconds $HoldSeconds
$l.Dispose()
"$Name RELEASED"
