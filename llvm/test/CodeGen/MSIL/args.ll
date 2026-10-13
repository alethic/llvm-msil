; Formal arguments (ImplementationPlan.md 5.1, M3.c/M3.d; recon passes-frame.md
; 7): parameters are unnamed in the method header, in the CIL type of their
; legalized value type: int32 for i32 and for every narrower integer (i1, i8,
; i16, with or without an extension attribute), native int for i64 and
; pointers (decision D2), float32, float64. Internal methods never declare a
; narrow CIL type, so a write to a parameter can never truncate (the
; narrow-parameter hazard of passes-frame.md 7). An argument is read where it
; is used, with ldarg.0-3, ldarg.s N for N < 256 and ldarg N above, and is never
; copied into a local (no .locals, stloc or starg here). A narrow argument that
; carries a zeroext/signext attribute is already extended, so extending it again
; costs nothing. The bodies (with %{body}) and the headers (SIG) are checked on
; the output of llc's default -O2 and of -O0, for both triples: the -O0
; pipeline is a different one (no OptimizeLiveIntervals or RegColoring, and the
; non-optimizing paths of RegStackify), so 'no copies into locals' is checked
; at both levels. The ilasm round trips assemble the -O2 output for
; windows-msvc and the -O0 output for linux-gnu.

; DEFINE: %{body} = FileCheck %s --implicit-check-not=.locals --implicit-check-not=stloc \
; DEFINE:   --implicit-check-not=starg --implicit-check-not=pop --implicit-check-not=conv.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il
; RUN: %{body} --input-file=%t.lnx.il
; RUN: FileCheck %s --input-file=%t.lnx.il --check-prefix=SIG
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il
; RUN: %{body} --input-file=%t.win.il
; RUN: FileCheck %s --input-file=%t.win.il --check-prefix=SIG
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il
; RUN: %{body} --input-file=%t.lnx.o0.il
; RUN: FileCheck %s --input-file=%t.lnx.o0.il --check-prefix=SIG
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il
; RUN: %{body} --input-file=%t.win.o0.il
; RUN: FileCheck %s --input-file=%t.win.o0.il --check-prefix=SIG
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

; SIG:      {{^}}.method public static int32 'arg0'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg1'(int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg2'(int32, int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg3'(int32, int32, int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg4'(int32, int32, int32, int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg5'(int32, int32, int32, int32, int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'arg_i64'(native int) cil managed{{$}}
; SIG:      {{^}}.method public static native int 'arg_ptr'(int32, native int) cil managed{{$}}
; SIG:      {{^}}.method public static float32 'arg_f32'(float64, float32) cil managed{{$}}
; SIG:      {{^}}.method public static float64 'arg_f64'(float32, float64) cil managed{{$}}
; SIG:      {{^}}.method public static float64 'mixed'(int32, native int, float32, float64, native int, int32, native int, float32, float64) cil managed{{$}}
; SIG:      {{^}}.method public static void 'narrow_params'(int32, int32, int32, int32, int32, int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'zext_u8_param'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'sext_s8_param'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'zext_u16_param'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'sext_s16_param'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'zext_i1_param'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static void 'all_unused'(int32, native int, float32, float64) cil managed{{$}}
; SIG:      {{^}}.method public static float32 'fence_f32'(float32) cil managed{{$}}
; SIG:      {{^}}.method public static float64 'fence_f64'(float64) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg255'({{(int32, ){255}int32}}) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'arg256'({{(int32, ){255}int32, int32}}) cil managed{{$}}
; SIG-NOT:  .method

source_filename = "args.c"

; CHECK-LABEL: .method {{.*}}'arg0'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'arg0'{{$}}
define i32 @arg0(i32 %a) {
  ret i32 %a
}

; CHECK-LABEL: .method {{.*}}'arg1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg1(i32 %a, i32 %b) {
  ret i32 %b
}

; CHECK-LABEL: .method {{.*}}'arg2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg2(i32 %a, i32 %b, i32 %c) {
  ret i32 %c
}

; CHECK-LABEL: .method {{.*}}'arg3'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg3(i32 %a, i32 %b, i32 %c, i32 %d) {
  ret i32 %d
}

; The fifth parameter is the first without a one-byte form.
; CHECK-LABEL: .method {{.*}}'arg4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg4(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
  ret i32 %e
}

; CHECK-LABEL: .method {{.*}}'arg5'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 5{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg5(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
  ret i32 %f
}

; CHECK-LABEL: .method {{.*}}'arg_i64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @arg_i64(i64 %x) {
  ret i64 %x
}

; CHECK-LABEL: .method {{.*}}'arg_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @arg_ptr(i32 %n, ptr %p) {
  ret ptr %p
}

; CHECK-LABEL: .method {{.*}}'arg_f32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @arg_f32(double %d, float %f) {
  ret float %f
}

; CHECK-LABEL: .method {{.*}}'arg_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @arg_f64(float %f, double %d) {
  ret double %d
}

; CHECK-LABEL: .method {{.*}}'mixed'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @mixed(i32 %a, i64 %b, float %c, double %d, ptr %e, i32 %f, i64 %g, float %h, double %i) {
  ret double %i
}

; Narrow parameters, with and without extension attributes, are int32.
; CHECK-LABEL: .method {{.*}}'narrow_params'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @narrow_params(i8 %a, i16 %b, i1 %c, i8 zeroext %d, i16 signext %e, i1 zeroext %f) {
  ret void
}

; Extending an argument that its attribute already extended is free.
; CHECK-LABEL: .method {{.*}}'zext_u8_param'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_u8_param(i8 zeroext %a) {
  %z = zext i8 %a to i32
  ret i32 %z
}

; CHECK-LABEL: .method {{.*}}'sext_s8_param'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_s8_param(i8 signext %a) {
  %s = sext i8 %a to i32
  ret i32 %s
}

; CHECK-LABEL: .method {{.*}}'zext_u16_param'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_u16_param(i16 zeroext %a) {
  %z = zext i16 %a to i32
  ret i32 %z
}

; CHECK-LABEL: .method {{.*}}'sext_s16_param'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_s16_param(i16 signext %a) {
  %s = sext i16 %a to i32
  ret i32 %s
}

; CHECK-LABEL: .method {{.*}}'zext_i1_param'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_i1_param(i1 zeroext %a) {
  %z = zext i1 %a to i32
  ret i32 %z
}

; Unused arguments of every type leave nothing in the body.
; CHECK-LABEL: .method {{.*}}'all_unused'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'all_unused'{{$}}
define void @all_unused(i32 %a, ptr %b, float %c, double %d) {
  ret void
}

; llvm.arithmetic.fence (clang's __arithmetic_fence, -fprotect-parens) only
; stops the optimizer from reassociating across it and emits no code: the
; argument is returned as it is, with no copy through a local, at -O2 and at
; -O0 (the body checks below run on the output of both levels).
; CHECK-LABEL: .method {{.*}}'fence_f32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fence_f32'{{$}}
define float @fence_f32(float %x) {
  %r = call float @llvm.arithmetic.fence.f32(float %x)
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'fence_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fence_f64'{{$}}
define double @fence_f64(double %x) {
  %r = call double @llvm.arithmetic.fence.f64(double %x)
  ret double %r
}

; Parameter 255 is the last one with ldarg.s, parameter 256 the first that
; needs the two-byte index of ldarg.
; CHECK-LABEL: .method {{.*}}'arg255'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 255{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @arg255(i32 %p0, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, i32 %p6, i32 %p7, i32 %p8, i32 %p9, i32 %p10, i32 %p11, i32 %p12, i32 %p13, i32 %p14, i32 %p15, i32 %p16, i32 %p17, i32 %p18, i32 %p19, i32 %p20, i32 %p21, i32 %p22, i32 %p23, i32 %p24, i32 %p25, i32 %p26, i32 %p27, i32 %p28, i32 %p29, i32 %p30, i32 %p31, i32 %p32, i32 %p33, i32 %p34, i32 %p35, i32 %p36, i32 %p37, i32 %p38, i32 %p39, i32 %p40, i32 %p41, i32 %p42, i32 %p43, i32 %p44, i32 %p45, i32 %p46, i32 %p47, i32 %p48, i32 %p49, i32 %p50, i32 %p51, i32 %p52, i32 %p53, i32 %p54, i32 %p55, i32 %p56, i32 %p57, i32 %p58, i32 %p59, i32 %p60, i32 %p61, i32 %p62, i32 %p63, i32 %p64, i32 %p65, i32 %p66, i32 %p67, i32 %p68, i32 %p69, i32 %p70, i32 %p71, i32 %p72, i32 %p73, i32 %p74, i32 %p75, i32 %p76, i32 %p77, i32 %p78, i32 %p79, i32 %p80, i32 %p81, i32 %p82, i32 %p83, i32 %p84, i32 %p85, i32 %p86, i32 %p87, i32 %p88, i32 %p89, i32 %p90, i32 %p91, i32 %p92, i32 %p93, i32 %p94, i32 %p95, i32 %p96, i32 %p97, i32 %p98, i32 %p99, i32 %p100, i32 %p101, i32 %p102, i32 %p103, i32 %p104, i32 %p105, i32 %p106, i32 %p107, i32 %p108, i32 %p109, i32 %p110, i32 %p111, i32 %p112, i32 %p113, i32 %p114, i32 %p115, i32 %p116, i32 %p117, i32 %p118, i32 %p119, i32 %p120, i32 %p121, i32 %p122, i32 %p123, i32 %p124, i32 %p125, i32 %p126, i32 %p127, i32 %p128, i32 %p129, i32 %p130, i32 %p131, i32 %p132, i32 %p133, i32 %p134, i32 %p135, i32 %p136, i32 %p137, i32 %p138, i32 %p139, i32 %p140, i32 %p141, i32 %p142, i32 %p143, i32 %p144, i32 %p145, i32 %p146, i32 %p147, i32 %p148, i32 %p149, i32 %p150, i32 %p151, i32 %p152, i32 %p153, i32 %p154, i32 %p155, i32 %p156, i32 %p157, i32 %p158, i32 %p159, i32 %p160, i32 %p161, i32 %p162, i32 %p163, i32 %p164, i32 %p165, i32 %p166, i32 %p167, i32 %p168, i32 %p169, i32 %p170, i32 %p171, i32 %p172, i32 %p173, i32 %p174, i32 %p175, i32 %p176, i32 %p177, i32 %p178, i32 %p179, i32 %p180, i32 %p181, i32 %p182, i32 %p183, i32 %p184, i32 %p185, i32 %p186, i32 %p187, i32 %p188, i32 %p189, i32 %p190, i32 %p191, i32 %p192, i32 %p193, i32 %p194, i32 %p195, i32 %p196, i32 %p197, i32 %p198, i32 %p199, i32 %p200, i32 %p201, i32 %p202, i32 %p203, i32 %p204, i32 %p205, i32 %p206, i32 %p207, i32 %p208, i32 %p209, i32 %p210, i32 %p211, i32 %p212, i32 %p213, i32 %p214, i32 %p215, i32 %p216, i32 %p217, i32 %p218, i32 %p219, i32 %p220, i32 %p221, i32 %p222, i32 %p223, i32 %p224, i32 %p225, i32 %p226, i32 %p227, i32 %p228, i32 %p229, i32 %p230, i32 %p231, i32 %p232, i32 %p233, i32 %p234, i32 %p235, i32 %p236, i32 %p237, i32 %p238, i32 %p239, i32 %p240, i32 %p241, i32 %p242, i32 %p243, i32 %p244, i32 %p245, i32 %p246, i32 %p247, i32 %p248, i32 %p249, i32 %p250, i32 %p251, i32 %p252, i32 %p253, i32 %p254, i32 %p255) {
  ret i32 %p255
}

; CHECK-LABEL: .method {{.*}}'arg256'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg 256{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'arg256'{{$}}
define i32 @arg256(i32 %p0, i32 %p1, i32 %p2, i32 %p3, i32 %p4, i32 %p5, i32 %p6, i32 %p7, i32 %p8, i32 %p9, i32 %p10, i32 %p11, i32 %p12, i32 %p13, i32 %p14, i32 %p15, i32 %p16, i32 %p17, i32 %p18, i32 %p19, i32 %p20, i32 %p21, i32 %p22, i32 %p23, i32 %p24, i32 %p25, i32 %p26, i32 %p27, i32 %p28, i32 %p29, i32 %p30, i32 %p31, i32 %p32, i32 %p33, i32 %p34, i32 %p35, i32 %p36, i32 %p37, i32 %p38, i32 %p39, i32 %p40, i32 %p41, i32 %p42, i32 %p43, i32 %p44, i32 %p45, i32 %p46, i32 %p47, i32 %p48, i32 %p49, i32 %p50, i32 %p51, i32 %p52, i32 %p53, i32 %p54, i32 %p55, i32 %p56, i32 %p57, i32 %p58, i32 %p59, i32 %p60, i32 %p61, i32 %p62, i32 %p63, i32 %p64, i32 %p65, i32 %p66, i32 %p67, i32 %p68, i32 %p69, i32 %p70, i32 %p71, i32 %p72, i32 %p73, i32 %p74, i32 %p75, i32 %p76, i32 %p77, i32 %p78, i32 %p79, i32 %p80, i32 %p81, i32 %p82, i32 %p83, i32 %p84, i32 %p85, i32 %p86, i32 %p87, i32 %p88, i32 %p89, i32 %p90, i32 %p91, i32 %p92, i32 %p93, i32 %p94, i32 %p95, i32 %p96, i32 %p97, i32 %p98, i32 %p99, i32 %p100, i32 %p101, i32 %p102, i32 %p103, i32 %p104, i32 %p105, i32 %p106, i32 %p107, i32 %p108, i32 %p109, i32 %p110, i32 %p111, i32 %p112, i32 %p113, i32 %p114, i32 %p115, i32 %p116, i32 %p117, i32 %p118, i32 %p119, i32 %p120, i32 %p121, i32 %p122, i32 %p123, i32 %p124, i32 %p125, i32 %p126, i32 %p127, i32 %p128, i32 %p129, i32 %p130, i32 %p131, i32 %p132, i32 %p133, i32 %p134, i32 %p135, i32 %p136, i32 %p137, i32 %p138, i32 %p139, i32 %p140, i32 %p141, i32 %p142, i32 %p143, i32 %p144, i32 %p145, i32 %p146, i32 %p147, i32 %p148, i32 %p149, i32 %p150, i32 %p151, i32 %p152, i32 %p153, i32 %p154, i32 %p155, i32 %p156, i32 %p157, i32 %p158, i32 %p159, i32 %p160, i32 %p161, i32 %p162, i32 %p163, i32 %p164, i32 %p165, i32 %p166, i32 %p167, i32 %p168, i32 %p169, i32 %p170, i32 %p171, i32 %p172, i32 %p173, i32 %p174, i32 %p175, i32 %p176, i32 %p177, i32 %p178, i32 %p179, i32 %p180, i32 %p181, i32 %p182, i32 %p183, i32 %p184, i32 %p185, i32 %p186, i32 %p187, i32 %p188, i32 %p189, i32 %p190, i32 %p191, i32 %p192, i32 %p193, i32 %p194, i32 %p195, i32 %p196, i32 %p197, i32 %p198, i32 %p199, i32 %p200, i32 %p201, i32 %p202, i32 %p203, i32 %p204, i32 %p205, i32 %p206, i32 %p207, i32 %p208, i32 %p209, i32 %p210, i32 %p211, i32 %p212, i32 %p213, i32 %p214, i32 %p215, i32 %p216, i32 %p217, i32 %p218, i32 %p219, i32 %p220, i32 %p221, i32 %p222, i32 %p223, i32 %p224, i32 %p225, i32 %p226, i32 %p227, i32 %p228, i32 %p229, i32 %p230, i32 %p231, i32 %p232, i32 %p233, i32 %p234, i32 %p235, i32 %p236, i32 %p237, i32 %p238, i32 %p239, i32 %p240, i32 %p241, i32 %p242, i32 %p243, i32 %p244, i32 %p245, i32 %p246, i32 %p247, i32 %p248, i32 %p249, i32 %p250, i32 %p251, i32 %p252, i32 %p253, i32 %p254, i32 %p255, i32 %p256) {
  ret i32 %p256
}

declare float @llvm.arithmetic.fence.f32(float)
declare double @llvm.arithmetic.fence.f64(double)
