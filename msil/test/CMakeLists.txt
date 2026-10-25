# Execution tests for the MSIL target (msil/test). See msil/test/README.md.

# Configure-time tool discovery. lit.cfg.py repeats the search at test time
# (and --param msil_<tool>=PATH overrides it), so installing .NET later only
# needs a re-run of lit, not of CMake. The default ilasm on Windows is the
# .NET Framework one (plan D31).
set(_msil_ilasm_hints)
if(WIN32)
  file(TO_CMAKE_PATH "$ENV{WINDIR}" _msil_windir)
  list(APPEND _msil_ilasm_hints "${_msil_windir}/Microsoft.NET/Framework64/v4.0.30319")
endif()
find_program(MSIL_ILASM_EXECUTABLE NAMES ilasm HINTS ${_msil_ilasm_hints}
  DOC "ilasm used by the MSIL execution tests")
find_program(MSIL_DOTNET_EXECUTABLE NAMES dotnet HINTS ENV DOTNET_ROOT
  DOC "dotnet host used by the MSIL execution tests")
set(_msil_dotnet_tools)
foreach(_msil_home_var HOME USERPROFILE)
  if(DEFINED ENV{${_msil_home_var}})
    file(TO_CMAKE_PATH "$ENV{${_msil_home_var}}" _msil_home)
    list(APPEND _msil_dotnet_tools "${_msil_home}/.dotnet/tools")
  endif()
endforeach()
find_program(MSIL_ILVERIFY_EXECUTABLE NAMES ilverify HINTS ${_msil_dotnet_tools}
  DOC "ILVerify (dotnet tool dotnet-ilverify) used by the MSIL execution tests")
set(_msil_peverify "")
if(WIN32)
  set(_msil_pf86_var "ProgramFiles(x86)")
  file(TO_CMAKE_PATH "$ENV{${_msil_pf86_var}}" _msil_pf86)
  file(GLOB _msil_peverify_hits
    "${_msil_pf86}/Microsoft SDKs/Windows/v*/bin/NETFX 4.* Tools/x64/PEVerify.exe")
  if(_msil_peverify_hits)
    list(SORT _msil_peverify_hits COMPARE NATURAL ORDER DESCENDING)
    list(GET _msil_peverify_hits 0 _msil_peverify)
  endif()
endif()
set(MSIL_PEVERIFY_EXECUTABLE "${_msil_peverify}" CACHE FILEPATH
  "PEVerify used by the MSIL execution tests (Windows)")

# Only what RUN lines and the lit configurations of the check-msil paths use,
# so check-msil does not drag in every tool. Targets that are not part of this
# configuration (clang without LLVM_ENABLE_PROJECTS=clang, say) are left out
# instead of breaking the configure step.
set(MSIL_TEST_DEPS)
# msil/test/M2 and the lto pipeline link with msil-ld (lld) and archive with
# the tree's llvm-ar; %msil_nm is llvm-nm (M14). %msil_canon is llvm-cil-dump
# --canonical (M17), and check-msil's llvm/test/tools/llvm-cil-dump and
# llvm-cil-copy run both tools on images that yaml2obj makes. check-msil's
# llvm/test/MC/MSIL runs llvm-mc (M18).
foreach(_msil_dep clang llc opt FileCheck not count split-file llvm-config lld
    llvm-ar llvm-nm llvm-cil-dump llvm-cil-copy yaml2obj llvm-mc)
  if(TARGET ${_msil_dep})
    list(APPEND MSIL_TEST_DEPS ${_msil_dep})
  endif()
endforeach()
# check-msil also runs lld/test/MSIL, which needs lld (it makes msil-ld too),
# llvm-as, llvm-ar and the llvm-lib and llvm-dlltool copies of llvm-ar for its
# inputs, and llvm/test/CodeGen/MSIL/irsymtab-linker-options.ll, which needs
# llvm-as and llvm-bcanalyzer (M13, M14). M15's native-library tests in
# lld/test/MSIL also need yaml2obj (in MSIL_TEST_DEPS since M17), llvm-mc (in
# MSIL_TEST_DEPS since M18) and llvm-readobj to make and inspect their native
# inputs.
set(_msil_check_deps ${MSIL_TEST_DEPS})
foreach(_msil_dep llvm-as llvm-bcanalyzer llvm-lib llvm-dlltool llvm-readobj)
  if(TARGET ${_msil_dep})
    list(APPEND _msil_check_deps ${_msil_dep})
  endif()
endforeach()

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
  "LLVM_LIT_TOOLS_DIR"
  "MSIL_SOURCE_DIR"
  "MSIL_BINARY_DIR"
  )

# check-msil-exec: this suite only. Part of check-all; without .NET (or the
# MSIL target, clang, ilasm) its tests are UNSUPPORTED.
add_lit_testsuite(check-msil-exec "Running the MSIL execution tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil-harness, check-msil-m1, check-msil-m1-<dir>: one target per
# directory.
add_lit_testsuites(MSIL ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${MSIL_TEST_DEPS}
  )

# check-msil: every MSIL-specific lit test in the tree with a lean dependency
# list (the upstream check-clang-cir pattern). Source paths work because
# llvm-lit maps each lit.cfg.py to its generated lit.site.cfg.py. Paths that
# do not exist yet (llvm/test/CodeGen/MSIL arrives in M2) only make lit warn,
# and listing them always means no reconfigure is needed when they appear.
set(_msil_lit_paths ${LLVM_MAIN_SRC_DIR}/test/CodeGen/MSIL)
if(TARGET clang AND LLVM_EXTERNAL_CLANG_SOURCE_DIR)
  list(APPEND _msil_lit_paths
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/CodeGen/MSIL
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/Preprocessor/init-msil.c
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/Driver/msil-toolchain.c
    # The MSIL toolchain's link, LTO and header-search tests (M14).
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/Driver/msil-link.c
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/Driver/msil-lto.c
    ${LLVM_EXTERNAL_CLANG_SOURCE_DIR}/test/Driver/msil-header-search.c
    )
endif()
# msil-ld's tests (M13, M14): the lld flavor msil.
if(TARGET lld AND LLVM_EXTERNAL_LLD_SOURCE_DIR)
  list(APPEND _msil_lit_paths ${LLVM_EXTERNAL_LLD_SOURCE_DIR}/test/MSIL)
endif()
# The tests of the ECMA-335 tools llvm-cil-dump and llvm-cil-copy (M17).
foreach(_msil_tool llvm-cil-dump llvm-cil-copy)
  if(TARGET ${_msil_tool})
    list(APPEND _msil_lit_paths ${LLVM_MAIN_SRC_DIR}/test/tools/${_msil_tool})
  endif()
endforeach()
# The MC tests of the MSIL target: encodings, fixups and object-form operand
# spellings (M18).
list(APPEND _msil_lit_paths ${LLVM_MAIN_SRC_DIR}/test/MC/MSIL)
list(APPEND _msil_lit_paths ${CMAKE_CURRENT_BINARY_DIR})
add_lit_target(check-msil "Running all MSIL tests"
  ${_msil_lit_paths}
  DEPENDS ${_msil_check_deps}
  )
