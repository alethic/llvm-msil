# Toolchain file sketch (recon driver-toolchain): clang + msil-ld for
# x86_64-unknown-linux-gnu-msil. Usage:
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=<this> -DMSIL_LLVM_BIN=<dir with clang, msil-ld, llvm-ar> <src>
set(CMAKE_SYSTEM_NAME MSIL)                  # Platform/MSIL.cmake below, not Windows-Clang
set(CMAKE_SYSTEM_PROCESSOR x86_64)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
if(NOT MSIL_LLVM_BIN)
  set(MSIL_LLVM_BIN "$ENV{MSIL_LLVM_BIN}")
endif()
set(MSIL_TRIPLE x86_64-unknown-linux-gnu-msil CACHE STRING "MSIL target triple")
set(CMAKE_C_COMPILER "${MSIL_LLVM_BIN}/clang.exe")
set(CMAKE_C_COMPILER_TARGET ${MSIL_TRIPLE})
set(CMAKE_AR "${MSIL_AR}" CACHE FILEPATH "llvm-ar")
# The triple goes into the flags as well: CMake identifies the compiler before
# it knows the --target= spelling (CMAKE_C_COMPILE_OPTIONS_TARGET), and for a
# windows-msvc triple it never learns it without Platform/MSIL-Clang-C.cmake.
# Until P2-A provides CIL objects, objects are bitcode and msil-ld runs LTO.
set(CMAKE_C_FLAGS_INIT "--target=${MSIL_TRIPLE} -flto")
# Variables try_compile must see (the toolchain file is re-read there).
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MSIL_LLVM_BIN MSIL_AR)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# glibc headers and libraries (libc6-dev, linux-libc-dev) for hosted builds
set(CMAKE_SYSROOT "$ENV{MSIL_GLIBC_SYSROOT}")
