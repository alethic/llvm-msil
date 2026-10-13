# Execution tests for the MSIL target (msil/test). See msil/test/README.md.

# Configure-time discovery. lit.cfg.py repeats the search at test time, so
# installing .NET later only needs a re-run of lit, not of CMake.
set(_msil_hints)
if(WIN32)
  list(APPEND _msil_hints "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319")
endif()
find_program(MSIL_ILASM_EXECUTABLE NAMES ilasm HINTS ${_msil_hints}
  DOC "ilasm used by the MSIL execution tests")
find_program(MSIL_DOTNET_EXECUTABLE NAMES dotnet HINTS ENV DOTNET_ROOT
  DOC "dotnet host used by the MSIL execution tests")
find_program(MSIL_ILVERIFY_EXECUTABLE NAMES ilverify
  HINTS "$ENV{HOME}/.dotnet/tools" "$ENV{USERPROFILE}/.dotnet/tools"
  DOC "ILVerify (dotnet tool dotnet-ilverify)")
if(WIN32)
  file(GLOB _msil_peverify
    "$ENV{ProgramFiles\(x86\)}/Microsoft SDKs/Windows/v*/bin/NETFX 4.* Tools/x64/PEVerify.exe")
  list(SORT _msil_peverify ORDER DESCENDING)
  list(GET _msil_peverify 0 _msil_peverify_first)
  set(MSIL_PEVERIFY_EXECUTABLE "${_msil_peverify_first}" CACHE FILEPATH "PEVerify")
endif()

# Only what the RUN lines invoke, so check-msil does not drag in every tool.
set(MSIL_TEST_DEPS clang llc FileCheck not count split-file llvm-config)

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  PATHS
  "LLVM_SOURCE_DIR"
  "LLVM_BINARY_DIR"
  "LLVM_TOOLS_DIR"
  "LLVM_LIBS_DIR"
  "MSIL_SOURCE_DIR"
  "MSIL_BINARY_DIR"
  )

# check-msil-exec: this suite only. Part of check-all; UNSUPPORTED without .NET.
add_lit_testsuite(check-msil-exec "Running the MSIL execution tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil-m1, check-msil-m1-03-memory, ...: one target per directory.
add_lit_testsuites(MSIL ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil: every MSIL-specific lit test in the tree, with a lean
# dependency list. Source paths work because llvm-lit maps each lit.cfg.py
# to its generated lit.site.cfg.py.
set(_clang_src "${LLVM_EXTERNAL_CLANG_SOURCE_DIR}")
set(_msil_lit_paths
  ${LLVM_MAIN_SRC_DIR}/test/CodeGen/MSIL
  ${_clang_src}/test/CodeGen/MSIL
  ${_clang_src}/test/Preprocessor/init-msil.c
  ${_clang_src}/test/Driver/msil-toolchain.c
  ${CMAKE_CURRENT_BINARY_DIR}
  )
# Phase 2 adds ${LLVM_MAIN_SRC_DIR}/test/MC/MSIL and lld/test/MSIL here.
add_lit_target(check-msil "Running all MSIL tests"
  ${_msil_lit_paths}
  DEPENDS ${MSIL_TEST_DEPS} llvm-mc
  )
