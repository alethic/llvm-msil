# Developer and CI configuration for the MSIL fork.
#   cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
# Anything here can be overridden with -D on the same command line.
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
# lld is not needed until phase 2 (msil-ld); the native oracle links with the
# system linker (ld on Linux, link.exe from the VS environment on Windows).
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "")
# X86 stays built: clang passes x86 -mllvm options for x86 triples
# (registry-tm.md 2.5), and the native oracle needs a host backend.
set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "")
set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD "MSIL" CACHE STRING "")
set(LLVM_EXTERNAL_PROJECTS "msil" CACHE STRING "")
set(LLVM_EXTERNAL_MSIL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "")
set(LLVM_OPTIMIZED_TABLEGEN ON CACHE BOOL "")
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "")
set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "")
set(LLVM_LIT_ARGS "-sv --time-tests" CACHE STRING "")
