; The synthesized methods, '.cctor' and '<msil>Main', as MCInsts
; (ImplementationPlan.md M18.a (7)): cli::ILBuilder builds them in Phase 1's
; instruction order, and every instruction is one MCInst that the encoder
; encodes, like the instructions of a compiled function. Since Q26 (plan
; section 12, 2026-10-21) their ldc.i4 constants take the shortest form
; (ldc.i4.s 15, ldc.i4.s 24, ldc.i4.8, ...; ldc.i4 for 447 and 408) and their
; branches are short: Main's 'br.s @argv_cond' and 'blt.s @argv_loop', with
; FK_PCRel_1 fixups on the label minus 1, as branches of compiled code have
; (encoding-branches.ll). The .maxstack values are Phase 1's.
;
; The tokens are fixups of their class with the operand's whole-program text
; as the value: the BCL members Marshal::AllocHGlobal,
; Environment::GetCommandLineArgs and Marshal::StringToHGlobalAnsi (method
; tokens with their [mscorlib] scope, plan 5.11.7), the data fields
; ('<msil>data', the address fields of the globals, the RVA image read with
; ldsflda), the function address field '<msil>fn_init' (filled with ldftn, a
; two-byte opcode whose token is at offset 2), the constructor 'init' and
; 'main'. The module has an initialized global, a pointer to it, a function
; pointer and a zero-initialized array, so the initializer copies the image
; (cpblk), clears the rest (initblk), sets the address fields, applies two
; relocations (stind.i) and calls the constructor; main(int, char**) gives
; Main its argv loop. Every byte was checked against Framework ilasm's image
; of the same text.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

@counter = global i32 5
@ptr = global ptr @counter
@fptr = global ptr @init
@zeros = global [100 x i32] zeroinitializer
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @init, ptr null }]

define void @init() {
  store i32 7, ptr @counter
  ret void
}

define i32 @main(i32 %argc, ptr %argv) {
  %c = load i32, ptr @counter
  %s = add i32 %c, %argc
  ret i32 %s
}

; CHECK-LABEL: .method {{.*}} .cctor()
; CHECK:      .maxstack 3
; CHECK-NEXT: ldc.i4 447 // encoding: [0x20,0xbf,0x01,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int), kind: fixup_cil_method_token
; CHECK-NEXT: ldc.i4.s 15 // encoding: [0x1f,0x0f]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldc.i4.s -16 // encoding: [0x1f,0xf0]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: and // encoding: [0x5f]
; CHECK-NEXT: stsfld native int '<msil>data' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldsflda valuetype '<msil>image_t' '<msil>image' // encoding: [0x7f,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: valuetype '<msil>image_t' '<msil>image', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 24 // encoding: [0x1f,0x18]
; CHECK-NEXT: cpblk // encoding: [0xfe,0x17]
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 24 // encoding: [0x1f,0x18]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldc.i4.0 // encoding: [0x16]
; CHECK-NEXT: ldc.i4 408 // encoding: [0x20,0x98,0x01,0x00,0x00]
; CHECK-NEXT: initblk // encoding: [0xfe,0x18]
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: stsfld native int 'counter' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'counter', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: stsfld native int 'ptr' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'ptr', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 16 // encoding: [0x1f,0x10]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: stsfld native int 'fptr' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'fptr', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 32 // encoding: [0x1f,0x20]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: stsfld native int 'zeros' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'zeros', kind: fixup_cil_field_token
; CHECK-NEXT: ldftn void 'init'() // encoding: [0xfe,0x06,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 2, value: void 'init'(), kind: fixup_cil_method_token
; CHECK-NEXT: stsfld native int '<msil>fn_init' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>fn_init', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'counter' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'counter', kind: fixup_cil_field_token
; CHECK-NEXT: stind.i // encoding: [0xdf]
; CHECK-NEXT: ldsfld native int '<msil>data' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>data', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 16 // encoding: [0x1f,0x10]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int '<msil>fn_init' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>fn_init', kind: fixup_cil_field_token
; CHECK-NEXT: stind.i // encoding: [0xdf]
; CHECK-NEXT: call void 'init'() // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'init'(), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method '.cctor'

; CHECK-LABEL: .method {{.*}}'<msil>Main'(
; CHECK:      .maxstack 3
; CHECK-NEXT: .locals (string[], int32, native int, int32)
; CHECK-NEXT: call string[] [mscorlib]System.Environment::GetCommandLineArgs() // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: string[] [mscorlib]System.Environment::GetCommandLineArgs(), kind: fixup_cil_method_token
; CHECK-NEXT: stloc.0 // encoding: [0x0a]
; CHECK-NEXT: ldloc.0 // encoding: [0x06]
; CHECK-NEXT: ldlen // encoding: [0x8e]
; CHECK-NEXT: conv.i4 // encoding: [0x69]
; CHECK-NEXT: stloc.1 // encoding: [0x0b]
; CHECK-NEXT: ldloc.1 // encoding: [0x07]
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: mul // encoding: [0x5a]
; CHECK-NEXT: call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int), kind: fixup_cil_method_token
; CHECK-NEXT: stloc.2 // encoding: [0x0c]
; CHECK-NEXT: ldc.i4.0 // encoding: [0x16]
; CHECK-NEXT: stloc.3 // encoding: [0x0d]
; CHECK-NEXT: br.s @argv_cond // encoding: [0x2b,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @argv_cond-1, kind: FK_PCRel_1
; CHECK-NEXT: @argv_loop:
; CHECK-NEXT: ldloc.2 // encoding: [0x08]
; CHECK-NEXT: ldloc.3 // encoding: [0x09]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: mul // encoding: [0x5a]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldloc.0 // encoding: [0x06]
; CHECK-NEXT: ldloc.3 // encoding: [0x09]
; CHECK-NEXT: ldelem.ref // encoding: [0x9a]
; CHECK-NEXT: call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string), kind: fixup_cil_method_token
; CHECK-NEXT: stind.i // encoding: [0xdf]
; CHECK-NEXT: ldloc.3 // encoding: [0x09]
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: stloc.3 // encoding: [0x0d]
; CHECK-NEXT: @argv_cond:
; CHECK-NEXT: ldloc.3 // encoding: [0x09]
; CHECK-NEXT: ldloc.1 // encoding: [0x07]
; CHECK-NEXT: blt.s @argv_loop // encoding: [0x32,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @argv_loop-1, kind: FK_PCRel_1
; CHECK-NEXT: ldloc.2 // encoding: [0x08]
; CHECK-NEXT: ldloc.1 // encoding: [0x07]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: mul // encoding: [0x5a]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldc.i4.0 // encoding: [0x16]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: stind.i // encoding: [0xdf]
; CHECK-NEXT: ldloc.1 // encoding: [0x07]
; CHECK-NEXT: ldloc.2 // encoding: [0x08]
; CHECK-NEXT: call int32 'main'(int32, native int) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: int32 'main'(int32, native int), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method '<msil>Main'
