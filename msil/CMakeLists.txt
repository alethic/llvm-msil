# The MSIL project: the execution tests and their harness, CI and build scripts.
# Built as an LLVM external project, so no upstream CMake file changes:
#   cmake -S llvm -B build -DLLVM_EXTERNAL_PROJECTS=msil \
#         -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil ...
# or simply: cmake -S llvm -B build -C msil/cmake/caches/MSIL.cmake
# (msil/utils/build/msil-configure.ps1 passes both variables on this host.)
set(LLVM_SUBPROJECT_TITLE "MSIL")
set(MSIL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(MSIL_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

# LLVM_TARGETS_TO_BUILD already includes LLVM_EXPERIMENTAL_TARGETS_TO_BUILD here.
if(NOT "MSIL" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(STATUS "MSIL project: the MSIL target is not built; "
                 "execution tests will report UNSUPPORTED")
endif()

if(LLVM_INCLUDE_TESTS)
  add_subdirectory(test)
endif()
