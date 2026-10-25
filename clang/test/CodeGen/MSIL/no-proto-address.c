// A prototype-less import whose address is used but that is never called
// directly is an error (ImplementationPlan.md section 12, the row 'M9.b amended
// (prototype-less functions whose address is used)'). MSILAddMissingPrototypes
// derives the signature of a "no-prototype" declaration only from its direct
// calls, and a managed calli does not check signatures, so a P/Invoke declared
// with WebAssembly's fallback signature 'T ()' would receive garbage arguments.
// The error is a module-level LLVMContext::emitError, so by design it has no
// source location; only its text is checked.
//
// The pass runs after the IR optimizer, so whether this valid C compiles
// depends on the optimization level. At -O0 'puts' is only passed to call(),
// which calls it through the pointer: the error. At -O2 the middle end (IPSCCP
// propagates the constant argument into the noinline call(), or inlining does
// the same) turns the call through the pointer into a direct call of 'puts',
// from which the signature is derived, and the program compiles to a direct
// call of the P/Invoke. That -O2 success is pinned to document the level
// dependence and to notice if the middle end stops doing it. A pointer held in
// a volatile local can never be resolved, so the VOLATILE variant gets the
// error at -O0 and at -O2. It is a separate compile because in the same TU the
// direct call of 'puts' that -O2 makes in call() would derive the signature and
// suppress the error.
//
// -std=gnu17 is explicit because in C23 'int puts();' is a prototype.

// REQUIRES: msil-registered-target

// DEFINE: %{flags} = -std=gnu17 -Wno-deprecated-non-prototype -ffreestanding -S
// DEFINE: %{err} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
// DEFINE:   --implicit-check-not="PLEASE submit"
// DEFINE: %{opt} = FileCheck %s --check-prefix=OPT --implicit-check-not=calli --implicit-check-not=ldftn

// RUN: not %clang --target=x86_64-pc-windows-msvc-msil %{flags} -O0 -o /dev/null %s 2>&1 | %{err}
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil %{flags} -O0 -o /dev/null %s 2>&1 | %{err}

// RUN: %clang --target=x86_64-pc-windows-msvc-msil %{flags} -O2 -o %t.win.il %s 2>&1 | count 0
// RUN: %{opt} -DCLIB=ucrtbase --input-file=%t.win.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil %{flags} -O2 -o %t.lnx.il %s 2>&1 | count 0
// RUN: %{opt} -DCLIB=libc --input-file=%t.lnx.il

// RUN: not %clang --target=x86_64-pc-windows-msvc-msil %{flags} -DVOLATILE -O0 -o /dev/null %s 2>&1 | %{err}
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil %{flags} -DVOLATILE -O0 -o /dev/null %s 2>&1 | %{err}
// RUN: not %clang --target=x86_64-pc-windows-msvc-msil %{flags} -DVOLATILE -O2 -o /dev/null %s 2>&1 | %{err}
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil %{flags} -DVOLATILE -O2 -o /dev/null %s 2>&1 | %{err}

// ERR: error: the signature of prototype-less function 'puts' cannot be derived: its address is used, but it is never called directly; declare it with a prototype
// ERR-NEXT: 1 error generated.

// The call through the pointer in call() is a direct call of the P/Invoke, and
// no address of 'puts' is left (no ldftn, no calli).
// OPT-LABEL: {{^}}.method {{.*}} 'call'(
// OPT:       {{^}}  call int32 'puts'(native int){{$}}
// OPT:       {{^}}} // end of method 'call'{{$}}
// OPT:       {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}

int puts();

#ifndef VOLATILE
__attribute__((noinline)) static int call(int (*f)(const char *), const char *s) { return f(s); }
int main(void) { return call(puts, "via pointer") < 0; }
#else
int main(void) {
  int (*volatile f)(const char *) = puts;
  return f("x") < 0;
}
#endif
