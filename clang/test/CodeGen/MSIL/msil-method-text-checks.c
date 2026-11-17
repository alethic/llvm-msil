// The run-time checks of managed-call stubs in clang -S text
// (ImplementationPlan.md 5.20.6 MW2 and 5.20.7 MK2 and MK3, with T-M7's and
// T-M16's texts; milestone MC-1), as llvm/test/CodeGen/MSIL/
// managed-call-text-checks.ll pins them for llc: -S text reaches ilasm
// without a link, so msil-ld never checks its declarations against the
// referenced TypeDefs, and its stubs make those checks when they run,
// before the member access.
// - Kind (T-M16): the 'this' handle of a 'this-obj' instance method or field
//   is cast to the TypeSpec 'class [mscorlib]T' (sb_len, holder_count), and
//   that of a 'this-val' one is unboxed to the TypeSpec 'valuetype
//   [mscorlib]V' (dt_year, stamp_ticks): the type loader refuses the
//   TypeSpec when the type has the other kind. A constructor's TYPE is
//   named the same way: a class's 'newobj' is followed by 'castclass class
//   [mscorlib]T' (sb_new), and a value type's constructed object is boxed
//   to the TypeSpec, 'box valuetype [mscorlib]V' (dt_new).
// - Enum (T-M7): an 'enum' parameter (set_fg) or result (get_fg) is checked
//   first, without a local (C#'s 'typeof(E).IsEnum': 'ldtoken E',
//   System.Type's GetTypeFromHandle and get_IsEnum()) and with 'sizeof E'
//   against the C value's width in bytes, throwing T-M7's text as
//   InvalidProgramException otherwise. That width is the target's: a C
//   'long' (set_fg_long) is 32 bits on the Windows triple (the LONG32 lines,
//   ldc.i4.4, so an int32 enum such as ConsoleColor passes) and 64 bits on
//   the Linux triple and msil64-unknown-dotnet (the LONG64 lines, ldc.i4.8
//   and the 64-bit text, so the stub throws for it).
// Each stub is pinned whole, from '.method' to '} // end of method' (with
// the comments that -S text writes at its start), on both host x86_64
// triples and msil64-unknown-dotnet at -O0 and -O2; the text assembles. Not
// pinned: the function number in label names and .maxstack, which is at
// least 2 in a stub with an enum check.

// REQUIRES: msil-registered-target
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O0 -S -o %t.win0.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG32 --input-file=%t.win0.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -S -o %t.win2.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG32 --input-file=%t.win2.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O0 -S -o %t.lnx0.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG64 --input-file=%t.lnx0.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -S -o %t.lnx2.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG64 --input-file=%t.lnx2.il
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O0 -S -o %t.p64.0.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG64 --input-file=%t.p64.0.il
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -O2 -S -o %t.p64.2.il %s
// RUN: FileCheck %s --check-prefixes=CHECK,LONG64 --input-file=%t.p64.2.il
// RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win2.il %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx0.il %}

typedef struct __dn_object *dn_handle;

int sb_len(dn_handle sb) __attribute__((msil_method("System.Text.StringBuilder", "get_Length", "instance int32()")));
int holder_count(dn_handle h) __attribute__((msil_method("Ns.Holder", "Count", "instance field int32")));
int dt_year(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "get_Year", "instance int32()")));
long long stamp_ticks(dn_handle s) __attribute__((msil_method("valuetype Ns.Stamp", "Ticks", "instance field int64")));
void set_fg(int c) __attribute__((msil_method("System.Console", "set_ForegroundColor", "void(valuetype System.ConsoleColor)")));
int get_fg(void) __attribute__((msil_method("System.Console", "get_ForegroundColor", "valuetype System.ConsoleColor()")));
void set_fg_long(long c) __attribute__((msil_method("System.Console", "set_ForegroundColor", "void(valuetype System.ConsoleColor)")));
dn_handle sb_new(dn_handle s) __attribute__((msil_method("System.Text.StringBuilder", ".ctor", "instance void(string)")));
dn_handle dt_new(long long ticks) __attribute__((msil_method("valuetype System.DateTime", ".ctor", "instance void(int64)")));

int use(dn_handle h, int v) {
  set_fg(get_fg());
  set_fg_long(v);
  return sb_len(h) + holder_count(h) + dt_year(h) + (int)stamp_ticks(h) +
         sb_len(sb_new(h)) + dt_year(dt_new(v));
}

// The stubs follow 'use' in the order of their first use.
// CHECK-LABEL: {{^}}.method assembly static void '__msil_mc.set_fg'(int32) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.set_fg$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.set_fg$}}
// CHECK-NEXT:  {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
// CHECK-NEXT:  {{^}}  ldtoken {{\[}}mscorlib]System.ConsoleColor{{$}}
// CHECK-NEXT:  {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
// CHECK-NEXT:  {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
// CHECK-NEXT:  {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  sizeof {{\[}}mscorlib]System.ConsoleColor{{$}}
// CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
// CHECK-NEXT:  {{^}}  beq.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldstr "managed call 'set_fg': parameter 1 passes 32 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
// CHECK-NEXT:  {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
// CHECK-NEXT:  {{^}}  throw{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call void {{\[}}mscorlib]System.Console::set_ForegroundColor(valuetype {{\[}}mscorlib]System.ConsoleColor){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.set_fg'{{$}}

// CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.get_fg'() cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.get_fg$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.get_fg$}}
// CHECK-NEXT:  {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
// CHECK-NEXT:  {{^}}  ldtoken {{\[}}mscorlib]System.ConsoleColor{{$}}
// CHECK-NEXT:  {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
// CHECK-NEXT:  {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
// CHECK-NEXT:  {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  sizeof {{\[}}mscorlib]System.ConsoleColor{{$}}
// CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
// CHECK-NEXT:  {{^}}  beq.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldstr "managed call 'get_fg': the result has 32 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
// CHECK-NEXT:  {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
// CHECK-NEXT:  {{^}}  throw{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.ConsoleColor {{\[}}mscorlib]System.Console::get_ForegroundColor(){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.get_fg'{{$}}

// CHECK-LABEL: {{^}}.method assembly static void '__msil_mc.set_fg_long'(
// LONG32-SAME: int32) cil managed{{$}}
// LONG64-SAME: native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.set_fg_long$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.set_fg_long$}}
// CHECK-NEXT:  {{^  \.maxstack ([2-9]|[1-9][0-9]+)$}}
// CHECK-NEXT:  {{^}}  ldtoken {{\[}}mscorlib]System.ConsoleColor{{$}}
// CHECK-NEXT:  {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
// CHECK-NEXT:  {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
// CHECK-NEXT:  {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  sizeof {{\[}}mscorlib]System.ConsoleColor{{$}}
// LONG32-NEXT: {{^}}  ldc.i4.4{{$}}
// LONG64-NEXT: {{^}}  ldc.i4.8{{$}}
// CHECK-NEXT:  {{^}}  beq.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// LONG32-NEXT: {{^}}  ldstr "managed call 'set_fg_long': parameter 1 passes 32 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
// LONG64-NEXT: {{^}}  ldstr "managed call 'set_fg_long': parameter 1 passes 64 bits for 'System.ConsoleColor', which is not an enum of that width"{{$}}
// CHECK-NEXT:  {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
// CHECK-NEXT:  {{^}}  throw{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// LONG64-NEXT: {{^}}  conv.i8{{$}}
// CHECK-NEXT:  {{^}}  call void {{\[}}mscorlib]System.Console::set_ForegroundColor(valuetype {{\[}}mscorlib]System.ConsoleColor){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.set_fg_long'{{$}}

// CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.sb_len'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.sb_len$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.sb_len$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  ldnull{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
// CHECK-NEXT:  {{^}}  box {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  unbox {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
// CHECK-NEXT:  {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}mscorlib]System.Text.StringBuilder::get_Length(){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.sb_len'{{$}}

// CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.holder_count'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.holder_count$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.holder_count$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  ldnull{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
// CHECK-NEXT:  {{^}}  box {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  unbox {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
// CHECK-NEXT:  {{^}}  castclass class {{\[}}mscorlib]Ns.Holder{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  ldfld int32 {{\[}}mscorlib]Ns.Holder::Count{{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.holder_count'{{$}}

// CHECK-LABEL: {{^}}.method assembly static int32 '__msil_mc.dt_year'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.dt_year$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.dt_year$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  ldnull{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
// CHECK-NEXT:  {{^}}  box {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  unbox {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  unbox valuetype {{\[}}mscorlib]System.DateTime{{$}}
// CHECK-NEXT:  {{^}}  call instance int32 {{\[}}mscorlib]System.DateTime::get_Year(){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.dt_year'{{$}}

// CHECK-LABEL: {{^}}.method assembly static native int '__msil_mc.stamp_ticks'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.stamp_ticks$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.stamp_ticks$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  ldnull{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
// CHECK-NEXT:  {{^}}  box {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  unbox {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  unbox valuetype {{\[}}mscorlib]Ns.Stamp{{$}}
// CHECK-NEXT:  {{^}}  ldfld int64 {{\[}}mscorlib]Ns.Stamp::Ticks{{$}}
// CHECK-NEXT:  {{^}}  conv.i{{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.stamp_ticks'{{$}}

// Constructors (Kind): a class's 'newobj' is followed by 'castclass class
// [mscorlib]T' (sb_new) ...
// CHECK-LABEL: {{^}}.method assembly static native int '__msil_mc.sb_new'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.sb_new$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.sb_new$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F:[0-9]+]]_0{{$}}
// CHECK-NEXT:  {{^}}  ldnull{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_1{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_0:{{$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
// CHECK-NEXT:  {{^}}  box {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  unbox {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle{{$}}
// CHECK-NEXT:  {{^}}  call instance object {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
// CHECK-NEXT:  {{^}}  castclass {{\[}}mscorlib]System.String{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_1:{{$}}
// CHECK-NEXT:  {{^}}  newobj instance void {{\[}}mscorlib]System.Text.StringBuilder::.ctor(string){{$}}
// CHECK-NEXT:  {{^}}  castclass class {{\[}}mscorlib]System.Text.StringBuilder{{$}}
// CHECK-NEXT:  {{^}}  dup{{$}}
// CHECK-NEXT:  {{^}}  brtrue.s @MC[[F]]_2{{$}}
// CHECK-NEXT:  {{^}}  pop{{$}}
// CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
// CHECK-NEXT:  {{^}}  conv.i{{$}}
// CHECK-NEXT:  {{^}}  br.s @MC[[F]]_3{{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_2:{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
// CHECK-NEXT:  {{^}}  call native int {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle){{$}}
// CHECK-NEXT:  {{^}}@MC[[F]]_3:{{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.sb_new'{{$}}

// ... and a value type's constructed object is boxed to the TypeSpec,
// 'box valuetype [mscorlib]V' (dt_new).
// CHECK-LABEL: {{^}}.method assembly static native int '__msil_mc.dt_new'(native int) cil managed{{$}}
// CHECK-NEXT:  {{^\{ +// -- Begin function __msil_mc\.dt_new$}}
// CHECK-NEXT:  {{^ +// @__msil_mc\.dt_new$}}
// CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
// CHECK-NEXT:  {{^}}  ldarg.0{{$}}
// CHECK-NEXT:  {{^}}  conv.i8{{$}}
// CHECK-NEXT:  {{^}}  newobj instance void {{\[}}mscorlib]System.DateTime::.ctor(int64){{$}}
// CHECK-NEXT:  {{^}}  box valuetype {{\[}}mscorlib]System.DateTime{{$}}
// CHECK-NEXT:  {{^}}  call valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
// CHECK-NEXT:  {{^}}  call native int {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}mscorlib]System.Runtime.InteropServices.GCHandle){{$}}
// CHECK-NEXT:  {{^}}  ret{{$}}
// CHECK-NEXT:  {{^}}} // end of method '__msil_mc.dt_new'{{$}}
