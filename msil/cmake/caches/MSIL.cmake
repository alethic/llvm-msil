# Developer and CI configuration for the MSIL fork.
#   cmake -G Ninja -S llvm -B build -C msil/cmake/caches/MSIL.cmake
# Anything here can be overridden with -D on the same command line.
# (On the development host msil/utils/build/msil-configure.ps1 is used instead;
# it adds lld, WebAssembly and the clang-cl toolchain.)
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
# lld is not needed until phase 2 (msil-ld); the native oracle links with the
# system linker (ld on Linux, link.exe from the VS environment on Windows).
set(LLVM_ENABLE_PROJECTS "clang" CACHE STRING "")
# X86 stays built: clang passes x86 -mllvm options for x86 triples
# (registry-tm.md 2.5), and the native oracle needs a host backend.
set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "")

get_filename_component(_msil_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# MSIL is experimental, so it never goes in LLVM_TARGETS_TO_BUILD (fatal). It is
# enabled once llvm/lib/Target/MSIL exists (milestone M2), so this cache file
# also configures the tree before that.
if(EXISTS "${_msil_repo}/llvm/lib/Target/MSIL/CMakeLists.txt")
  set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD "MSIL" CACHE STRING "")
endif()

# The msil/ directory (execution tests, check-msil*) as an LLVM external project.
set(LLVM_EXTERNAL_PROJECTS "msil" CACHE STRING "")
set(LLVM_EXTERNAL_MSIL_SOURCE_DIR "${_msil_repo}/msil" CACHE PATH "")

# OFF: with assertions, ON builds a second, native tablegen (LLVM_USE_HOST_TOOLS,
# llvm/CMakeLists.txt), and a Release tablegen is optimized already.
set(LLVM_OPTIMIZED_TABLEGEN OFF CACHE BOOL "")
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "")
# CLANG_ENABLE_ARCMT is deprecated (clang/CMakeLists.txt); this is its successor.
set(CLANG_ENABLE_OBJC_REWRITER OFF CACHE BOOL "")
set(LLVM_LIT_ARGS "-sv --time-tests" CACHE STRING "")
