# Platform/MSIL-Clang-C.cmake (sketch): clang for an MSIL triple uses the GNU
# command line even when the triple is windows-msvc. Clang then predefines
# _MSC_VER, so CMake sets CMAKE_C_SIMULATE_ID=MSVC, and Compiler/Clang.cmake
# leaves the GNU rules (and --target=) to Platform/Windows-Clang.cmake, which
# is not loaded for CMAKE_SYSTEM_NAME=MSIL. Load them here.
include(Compiler/GNU)
__compiler_gnu(C)
set(CMAKE_C_COMPILE_OPTIONS_TARGET "--target=")
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_C_LINK_OPTIONS_PIE "")
set(CMAKE_C_LINK_OPTIONS_NO_PIE "")
set(CMAKE_C_LINKER_WRAPPER_FLAG "-Xlinker" " ")
set(CMAKE_C_LINKER_WRAPPER_FLAG_SEP)
set(_CMAKE_C_IPO_SUPPORTED_BY_CMAKE YES)
set(_CMAKE_C_IPO_MAY_BE_SUPPORTED_BY_COMPILER YES)
set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto")
