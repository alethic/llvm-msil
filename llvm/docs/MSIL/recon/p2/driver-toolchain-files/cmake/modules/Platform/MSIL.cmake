# Platform/MSIL.cmake (sketch, recon driver-toolchain): .NET assemblies built by
# clang --target=<native triple>-msil and msil-ld. Selected by
# CMAKE_SYSTEM_NAME=MSIL in the toolchain file. Not part of the compiler.
set(MSIL 1)
set(CMAKE_EXECUTABLE_SUFFIX ".exe")         # dotnet needs .exe or .dll; .exe also runs on Framework
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")      # a library assembly; also what others link against
set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_SHARED_MODULE_SUFFIX ".dll")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")        # llvm-ar archives of CIL objects or bitcode
set(CMAKE_FIND_LIBRARY_PREFIXES "lib" "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dll" ".so" ".lib")
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
set(CMAKE_SHARED_LIBRARY_C_FLAGS "")         # no PIC
set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-shared")
set(CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG "")  # no rpath
set(CMAKE_DL_LIBS "")
