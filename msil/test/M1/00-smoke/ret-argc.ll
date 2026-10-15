; main(i32, ptr) through the generated entry point returns argc: run without
; arguments, argc is 1 (argv[0] names the program), at -O0 and -O2. Only argc
; is read; msil/test/M1/05-memory/argv.c reads argv and its NULL terminator.
; The argv-building body itself is checked in llvm/test/CodeGen/MSIL/entrypoint.ll.
; RUN: %{msil_ll}

define i32 @main(i32 %argc, ptr %argv) {
  ret i32 %argc
}
