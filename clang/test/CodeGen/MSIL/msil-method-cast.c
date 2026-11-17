// A managed call made through a cast pointer that changes only the
// signedness of a narrow result (ImplementationPlan.md 5.20.3 T-M17, 5.20.5's
// P0 rule as amended in section 12, the rows '5.20.5 (P0's T-M10 rule) and
// MC-1.f's Provides amended' of 2026-11-04 and 'fix-loop ownership:
// `llvm/include/llvm/CLI/ManagedCallIR.h` -> MC-1.d' of 2026-11-11; milestone
// MC-1). The ABI of msil64-unknown-dotnet marks a char result 'signext' or
// 'zeroext', and clang writes a call's attributes from the function type that
// it calls through. sign.c calls 'unsigned char g(int)' as
// '((signed char (*)(int))g)(1)': a 'call signext i8' of 'declare zeroext i8
// @__msil_mc.g(i32 noundef)', the declaration's own function type with
// another extension, which would give g's stub another result conversion
// (conv.i1, not conv.u1), so the caller would read the stub's zero-extended
// value as a sign-extended one. It is the IR that the IR linker leaves of two
// units that declare g with an 'unsigned char' and a 'signed char' result, so
// slot P0 reports T-M17's first form for it, as for type.c's call of 'int
// g(int)' through 'long long (*)(int)', a call of another function type, in
// clang's code generation as in llc (llvm/test/CodeGen/MSIL/
// managed-call-errors.ll). sign.c gives exactly that line and '1 error
// generated.' at -O0 and -O2, with -S and with -c, and type.c with -S at -O0
// and with -c at -O2; neither prints a 'taking the address' line.
// Accepted: direct.c, sign.c's g called directly, whose stub keeps the
// declaration's conv.u1; and sign.c on x86_64-unknown-linux-gnu-msil, whose
// ABI gives a char result neither attribute, so the call and the declaration
// are both 'i8' and the stub is the same.

// REQUIRES: msil-registered-target

// RUN: rm -rf %t && split-file %s %t
// DEFINE: %{cc} = %clang --target=msil64-unknown-dotnet -ffreestanding
// DEFINE: %{tm17} = FileCheck %s --implicit-check-not=error: --implicit-check-not=warning: \
// DEFINE:   --implicit-check-not="taking the address" --implicit-check-not="LLVM ERROR" \
// DEFINE:   --implicit-check-not="PLEASE submit"

// RUN: not %{cc} -O0 -S -o %t/sign.O0.il %t/sign.c 2>&1 | %{tm17}
// RUN: not %{cc} -O2 -S -o %t/sign.O2.il %t/sign.c 2>&1 | %{tm17}
// RUN: not %{cc} -O0 -c -o %t/sign.O0.o %t/sign.c 2>&1 | %{tm17}
// RUN: not %{cc} -O2 -c -o %t/sign.O2.o %t/sign.c 2>&1 | %{tm17}
// RUN: not %{cc} -O0 -S -o %t/type.O0.il %t/type.c 2>&1 | %{tm17}
// RUN: not %{cc} -O2 -c -o %t/type.O2.o %t/type.c 2>&1 | %{tm17}

// CHECK:      {{^}}error: managed call 'g' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
// CHECK-NEXT: {{^}}1 error generated.{{$}}

// RUN: %{cc} -O0 -S -o %t/direct.O0.il %t/direct.c 2>&1 | count 0
// RUN: FileCheck %s --check-prefix=DIRECT --input-file=%t/direct.O0.il
// RUN: %{cc} -O2 -S -o %t/direct.O2.il %t/direct.c 2>&1 | count 0
// RUN: FileCheck %s --check-prefix=DIRECT --input-file=%t/direct.O2.il
// RUN: %{cc} -O2 -c -o %t/direct.O2.o %t/direct.c 2>&1 | count 0
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -ffreestanding -O0 -S -o %t/sign.lnx.il \
// RUN:   %t/sign.c 2>&1 | count 0

// DIRECT-LABEL: {{^}}.method assembly static int32 '__msil_mc.g'(int32) cil managed{{$}}
// DIRECT:       {{^}}  ldarg.0{{$}}
// DIRECT-NEXT:  {{^}}  call uint8 {{\[}}mscorlib]System.Convert::ToByte(int32){{$}}
// DIRECT-NEXT:  {{^}}  conv.u1{{$}}
// DIRECT-NEXT:  {{^}}  ret{{$}}

//--- sign.c
unsigned char g(int) __attribute__((msil_method("System.Convert", "ToByte", "uint8(int32)")));
int use(void) { return ((signed char (*)(int))g)(1); }

//--- type.c
int g(int) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
long long use(void) { return ((long long (*)(int))g)(1); }

//--- direct.c
unsigned char g(int) __attribute__((msil_method("System.Convert", "ToByte", "uint8(int32)")));
int use(void) { return g(1); }
