foreach ($k in 'attr','resource','section','pdb','stream','enc') {
  $p = Join-Path $PSScriptRoot "carrier-$k.o"
  try {
    $bytes = [IO.File]::ReadAllBytes($p)
    $a = [Reflection.Assembly]::Load($bytes)
    $m = $a.ManifestModule.GetMethod('f')
    "{0,-8} {1}: f() = {2}" -f $k, $PSVersionTable.PSEdition, $m.Invoke($null, @())
  } catch { "{0,-8} {1}: {2}" -f $k, $PSVersionTable.PSEdition, $_.Exception.InnerException.Message + $_.Exception.Message }
}
