cmake_minimum_required(VERSION 3.20)
project(msildemo C)
add_library(util STATIC util.c)              # llvm-ar archive
add_executable(app main.c)                   # app.exe, an EXE-image assembly
target_link_libraries(app PRIVATE util)
target_compile_options(app PRIVATE -ffreestanding)
target_compile_options(util PRIVATE -ffreestanding)
add_library(shlib SHARED shlib.c)            # shlib.dll, a library assembly (P2-C)
target_compile_options(shlib PRIVATE -ffreestanding)
target_link_options(shlib PRIVATE "-Wl,--export-class=Demo.Native")
# Like Roslyn, the compiler writes no runtimeconfig; the build system does.
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:app>/app.runtimeconfig.json" CONTENT
  "{ \"runtimeOptions\": { \"tfm\": \"net8.0\", \"framework\": { \"name\": \"Microsoft.NETCore.App\", \"version\": \"8.0.0\" } } }\n")
