# PROTOTYPE (recon M11 item 6c) of msil/utils/ci/hooks-compile.ps1: compile coverage for the
# fork's hooks in upstream files that the main build never compiles (ARM is not a built target,
# lldb is not an enabled project):
#   llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp              (case MCContext::IsMSIL)
#   lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp (case Triple::MSIL)
# Steps: (1) configure the private tree if it has no build.ninja (cmake re-runs by itself later,
# through ninja's build.ninja regeneration rule, when a CMake input changes); (2) ninja the ARM
# object (pulls llvm-tblgen and the ARM .inc files; real compile with the tree's own flags);
# (3) msil-syntax.ps1 /Zs of both TUs with -Werror=switch, every run (does not depend on build
# history; the lldb TU needs only configure-time headers, so its ninja object, which waits for
# ~1000 unrelated TUs through CMake's order-only edges, is never built).
[CmdletBinding()]
param(
  [string]$HooksBuildDir = $(if ($env:MSIL_HOOKS_BUILD_DIR) { $env:MSIL_HOOKS_BUILD_DIR } else { 'C:\llvm-msil-hooks-build' }),
  [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$repo = 'D:\llvm-msil'
. "$repo\msil\utils\build\msil-env.ps1"
$files = @('llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp',
           'lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp')
$armObj = 'lib\Target\ARM\AsmParser\CMakeFiles\LLVMARMAsmParser.dir\ARMAsmParser.cpp.obj'
$total = [Diagnostics.Stopwatch]::StartNew()
$fail = @()

if (-not (Test-Path (Join-Path $HooksBuildDir 'build.ninja'))) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & cmake -G Ninja -S "$repo/llvm" -B $HooksBuildDir `
    -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON `
    '-DLLVM_ENABLE_PROJECTS=clang;lldb' -DLLVM_TARGETS_TO_BUILD=ARM `
    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD= -DLLVM_EXTERNAL_PROJECTS= `
    -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF `
    -DLLVM_INCLUDE_DOCS=OFF -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF -DLLDB_INCLUDE_TESTS=OFF `
    -DLLVM_APPEND_VC_REV=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF `
    -DLLVM_ENABLE_DIA_SDK=OFF -DLLVM_ENABLE_TELEMETRY=OFF -DLLVM_ENABLE_PDB=OFF `
    -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_OBJC_REWRITER=OFF `
    -DLLDB_ENABLE_PYTHON=OFF -DLLDB_ENABLE_LUA=OFF -DLLDB_ENABLE_SWIG=OFF -DLLDB_ENABLE_LIBXML2=OFF `
    -DLLDB_ENABLE_LIBEDIT=OFF -DLLDB_ENABLE_CURSES=OFF `
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "-DPython3_EXECUTABLE=$MsilPython" `
    "-DCMAKE_C_COMPILER=$MsilLlvmBin\clang-cl.exe" "-DCMAKE_CXX_COMPILER=$MsilLlvmBin\clang-cl.exe" `
    "-DCMAKE_LINKER=$MsilLlvmBin\lld-link.exe" "-DCMAKE_AR=$MsilLlvmBin\llvm-lib.exe" -DLLVM_ENABLE_LLD=ON `
    "-DLLVM_PARALLEL_COMPILE_JOBS=$Jobs" -DLLVM_PARALLEL_LINK_JOBS=2 | Out-Host
  if ($LASTEXITCODE -ne 0) { $fail += "configure exit $LASTEXITCODE" }
  "[hooks-compile] configure $([int]$sw.Elapsed.TotalSeconds)s"
}
if (-not $fail) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & ninja -C $HooksBuildDir "-j$Jobs" $armObj | Out-Host
  if ($LASTEXITCODE -ne 0) { $fail += "ninja $armObj exit $LASTEXITCODE" }
  "[hooks-compile] ninja ARM object $([int]$sw.Elapsed.TotalSeconds)s"
}
if (-not $fail) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & pwsh -NoProfile -File "$repo\msil\utils\build\msil-syntax.ps1" -BuildDir $HooksBuildDir `
      -ExtraFlags '/clang:-Werror=switch' @($files | ForEach-Object { Join-Path $repo $_ }) | Out-Host
  if ($LASTEXITCODE -ne 0) { $fail += "msil-syntax exit $LASTEXITCODE" }
  "[hooks-compile] /Zs -Werror=switch of both TUs $([int]$sw.Elapsed.TotalSeconds)s"
}
$status = if ($fail) { 'FAIL' } else { 'PASS' }
"[hooks-compile] $status total $([int]$total.Elapsed.TotalSeconds)s $($fail -join '; ')"
exit $(if ($fail) { 1 } else { 0 })
