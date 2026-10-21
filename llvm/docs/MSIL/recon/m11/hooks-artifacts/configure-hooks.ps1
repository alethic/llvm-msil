# Recon (M11 items 6/7): configure a private build tree that can compile
# llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp and
# lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp.
param(
  [string]$BuildDir = 'C:\llvm-msil-hooks-build',
  [string]$Projects = 'clang;lldb',
  [string]$Targets = 'ARM'
)
$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$cmakeArgs = @(
  '-G', 'Ninja',
  '-S', 'D:/llvm-msil/llvm',
  '-B', $BuildDir,
  '-DCMAKE_BUILD_TYPE=Release',
  '-DLLVM_ENABLE_ASSERTIONS=ON',
  "-DLLVM_ENABLE_PROJECTS=$Projects",
  "-DLLVM_TARGETS_TO_BUILD=$Targets",
  '-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=',
  '-DLLVM_EXTERNAL_PROJECTS=',
  '-DLLVM_OPTIMIZED_TABLEGEN=OFF',
  '-DLLVM_PARALLEL_COMPILE_JOBS=8',
  '-DLLVM_PARALLEL_LINK_JOBS=2',
  '-DLLVM_ENABLE_PDB=OFF',
  '-DLLVM_INCLUDE_BENCHMARKS=OFF',
  '-DLLVM_INCLUDE_EXAMPLES=OFF',
  '-DLLVM_INCLUDE_TESTS=OFF',
  '-DLLVM_INCLUDE_DOCS=OFF',
  '-DLLVM_BUILD_TESTS=OFF',
  '-DCLANG_INCLUDE_TESTS=OFF',
  '-DCLANG_INCLUDE_DOCS=OFF',
  '-DLLDB_INCLUDE_TESTS=OFF',
  '-DLLVM_APPEND_VC_REV=OFF',
  '-DLLVM_ENABLE_ZLIB=OFF',
  '-DLLVM_ENABLE_ZSTD=OFF',
  '-DLLVM_ENABLE_LIBXML2=OFF',
  '-DLLVM_ENABLE_DIA_SDK=OFF',
  '-DLLVM_ENABLE_TELEMETRY=OFF',
  '-DCLANG_ENABLE_STATIC_ANALYZER=OFF',
  '-DCLANG_ENABLE_OBJC_REWRITER=OFF',
  '-DLLDB_ENABLE_PYTHON=OFF',
  '-DLLDB_ENABLE_LUA=OFF',
  '-DLLDB_ENABLE_SWIG=OFF',
  '-DLLDB_ENABLE_LIBXML2=OFF',
  '-DLLDB_ENABLE_LIBEDIT=OFF',
  '-DLLDB_ENABLE_CURSES=OFF',
  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
  "-DPython3_EXECUTABLE=$MsilPython",
  "-DCMAKE_C_COMPILER=$MsilLlvmBin\clang-cl.exe",
  "-DCMAKE_CXX_COMPILER=$MsilLlvmBin\clang-cl.exe",
  "-DCMAKE_LINKER=$MsilLlvmBin\lld-link.exe",
  "-DCMAKE_AR=$MsilLlvmBin\llvm-lib.exe",
  '-DLLVM_ENABLE_LLD=ON'
)
$sw = [Diagnostics.Stopwatch]::StartNew()
& cmake @cmakeArgs
$rc = $LASTEXITCODE
"configure exit=$rc elapsed=$([math]::Round($sw.Elapsed.TotalSeconds,1))s"
exit $rc
