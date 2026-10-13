# msil-lock.ps1 - dot-source me. Cross-process reader/writer lock for one build directory.
# The lock is an open handle on <BuildDir>\.msil-build.lock:
#   exclusive (ninja, cmake):  FileAccess.ReadWrite + FileShare.Read
#   shared    (lit, gtest):    FileAccess.Read      + FileShare.Read
# Windows sharing rules then give "many readers XOR one writer", and the OS drops the handle
# the instant the holding process exits or is killed: no stale locks, nothing to clean up.
function Enter-MsilBuildLock {
  param(
    [Parameter(Mandatory)][string]$BuildDir,
    [string]$What = 'unspecified',
    [double]$WaitMinutes = 180,
    [switch]$Shared
  )
  New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
  $path = Join-Path $BuildDir '.msil-build.lock'
  $deadline = (Get-Date).AddMinutes($WaitMinutes)
  $announced = $false
  $access = if ($Shared) { [System.IO.FileAccess]::Read } else { [System.IO.FileAccess]::ReadWrite }
  while ($true) {
    try {
      $fs = [System.IO.FileStream]::new($path, [System.IO.FileMode]::OpenOrCreate, $access,
              [System.IO.FileShare]::Read)
      if (-not $Shared) {
        $info = "pid=$PID`nstarted=$(Get-Date -Format o)`nwhat=$What`n"
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($info)
        $fs.SetLength(0); $fs.Write($bytes, 0, $bytes.Length); $fs.Flush()
      }
      break
    } catch [System.IO.IOException] {
      if (-not $announced) {
        $holder = try { (Get-Content -Raw -LiteralPath $path -ErrorAction Stop) -replace "`n", ' ' } catch { '?' }
        Write-Host "[msil-lock] waiting for $path (last writer: $holder)"
        $announced = $true
      }
      if ((Get-Date) -gt $deadline) { throw "[msil-lock] timed out after $WaitMinutes min waiting for $path" }
      Start-Sleep -Seconds 3
    }
  }
  # Belt and braces: a killed wrapper releases the lock, but on Windows its ninja/cmake child
  # can outlive it. Never proceed while one of those still runs on this build dir.
  $needle = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd('\').ToLowerInvariant()
  while ($true) {
    $orphans = @(Get-CimInstance Win32_Process -Filter "Name='ninja.exe' OR Name='cmake.exe'" |
      Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and
                     $_.CommandLine.ToLowerInvariant().Replace('/', '\').Contains($needle) })
    if ($orphans.Count -eq 0) { break }
    if ((Get-Date) -gt $deadline) { $fs.Dispose(); throw "[msil-lock] orphaned build processes still running: $($orphans.ProcessId -join ',')" }
    Write-Host "[msil-lock] waiting for orphaned process(es) $($orphans.ProcessId -join ',') on $needle"
    Start-Sleep -Seconds 5
  }
  return $fs
}
