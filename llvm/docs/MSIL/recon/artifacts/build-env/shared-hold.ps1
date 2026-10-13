param([string]$Dir,[int]$Hold,[switch]$Shared,[string]$Name)
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
$t0=Get-Date; $l = Enter-MsilBuildLock -BuildDir $Dir -Shared:$Shared -What $Name -WaitMinutes 2
"$Name acquired after $([math]::Round(((Get-Date)-$t0).TotalSeconds,1))s"; Start-Sleep $Hold; $l.Dispose(); "$Name released"
