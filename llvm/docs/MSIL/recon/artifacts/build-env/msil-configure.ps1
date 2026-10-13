# msil-configure.ps1 - configure the llvm-msil build tree (Ninja, Release+Assertions).
#   pwsh -NoProfile -File msil-configure.ps1                       # today: no MSIL target yet
#   pwsh -NoProfile -File msil-configure.ps1 -Experimental MSIL    # once llvm/lib/Target/MSIL exists
#   pwsh -NoProfile -File msil-configure.ps1 -Compiler msvc -BuildDir <dir>   # cl.exe + link.exe
param(
  [string]$BuildDir = 'D:\llvm-msil\build',
  [ValidateSet('clangcl','msvc')][string]$Compiler = 'clangcl',
  [string]$Targets = 'X86;WebAssembly',
  [string]$Experimental = '',
  [int]$CompileJobs = 8,
  [int]$LinkJobs = 2,
  [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')

$cmakeArgs = @(
  '-G', 'Ninja',
  '-S', (Join-Path $MsilRepo 'llvm'),
  '-B', $BuildDir,
  '-DCMAKE_BUILD_TYPE=Release',
  '-DLLVM_ENABLE_ASSERTIONS=ON',
  '-DLLVM_ENABLE_PROJECTS=clang;lld',
  "-DLLVM_TARGETS_TO_BUILD=$Targets",
  "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=$Experimental",
  '-DLLVM_OPTIMIZED_TABLEGEN=OFF',
  "-DLLVM_PARALLEL_COMPILE_JOBS=$CompileJobs",
  "-DLLVM_PARALLEL_LINK_JOBS=$LinkJobs",
  '-DLLVM_ENABLE_PDB=OFF',
  '-DLLVM_INCLUDE_BENCHMARKS=OFF',
  '-DLLVM_INCLUDE_EXAMPLES=OFF',
  '-DLLVM_INCLUDE_TESTS=ON',
  '-DLLVM_BUILD_TESTS=OFF',
  '-DLLVM_APPEND_VC_REV=OFF',
  '-DLLVM_ENABLE_ZLIB=OFF',
  '-DLLVM_ENABLE_ZSTD=OFF',
  '-DLLVM_ENABLE_LIBXML2=OFF',
  '-DLLVM_ENABLE_DIA_SDK=OFF',
  '-DLLVM_ENABLE_TELEMETRY=OFF',
  '-DCLANG_ENABLE_STATIC_ANALYZER=OFF',
  '-DCLANG_ENABLE_OBJC_REWRITER=OFF',
  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
  "-DPython3_EXECUTABLE=$MsilPython"
)
if ($Compiler -eq 'clangcl') {
  $cmakeArgs += @(
    "-DCMAKE_C_COMPILER=$MsilLlvmBin\clang-cl.exe",
    "-DCMAKE_CXX_COMPILER=$MsilLlvmBin\clang-cl.exe",
    "-DCMAKE_LINKER=$MsilLlvmBin\lld-link.exe",
    "-DCMAKE_AR=$MsilLlvmBin\llvm-lib.exe",
    '-DLLVM_ENABLE_LLD=ON'
  )
} else {
  $cl = (Get-Command cl.exe).Source
  $cmakeArgs += @("-DCMAKE_C_COMPILER=$cl", "-DCMAKE_CXX_COMPILER=$cl",
                  "-DCMAKE_LINKER=$((Get-Command link.exe).Source)")
}
if ($Fresh) { $cmakeArgs += '--fresh' }

$lock = Enter-MsilBuildLock -BuildDir $BuildDir -What "configure $Experimental"
try {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & cmake @cmakeArgs
  $rc = $LASTEXITCODE
} finally { $lock.Dispose() }
"configure exit=$rc elapsed=$([math]::Round($sw.Elapsed.TotalSeconds,1))s"
exit $rc
