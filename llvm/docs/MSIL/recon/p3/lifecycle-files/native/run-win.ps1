# Runs a native Windows probe in each mode with stderr and stdout merged into one pipe (cmd 2>&1) and prints
# the exit code in decimal and hex.
param([string]$Prog, [string]$Modes)
foreach ($m in $Modes.Split(",")) {
  "=== $Prog $m"
  $out = cmd /c "`"$Prog`" $m 2>&1"
  $code = $LASTEXITCODE
  $out
  "status=$code (0x{0:X8})" -f $code
}
