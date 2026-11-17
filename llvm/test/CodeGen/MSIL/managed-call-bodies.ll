; The bodies of managed-call stubs (ImplementationPlan.md 5.20.3 MS5, 5.20.5
; ML2 (2) and (3), ML3, 5.20.6 MW1 and MW2, 5.20.9 and 5.20.10; milestone
; MC-1). The input is the IR that clang writes for managed-call declarations
; (5.20.4: '__msil_mc.<symbol>' with the msil-managed-* attributes and the
; '!msil.managed.calls' operands), one declaration per conversion row of MS5
; and per member kind, each called once from @use_all. The AsmPrinter writes
; each stub's body (none of its machine instructions is emitted): the
; argument conversions in C parameter order, exactly one member access, the
; result conversion and 'ret'.
; - 'val' and 'enum' are plain loads, 'bool' in is 'ldc.i4.0; cgt.un', a
;   'bool' result is normalized to 0 or 1 by 'ldc.i4.0; cgt.un' before 'ret'
;   (MS5 as amended in section 12 on 2026-11-04), and so is a C '_Bool'
;   result (IR 'zeroext i1') of the 'val' row on an 8-bit integer CIL type
;   (m_tobyte: uint8 Convert::ToByte; m_tosbyte: int8 Convert::ToSByte, whose
;   -1 must become 1) and of the 'enum' row (m_latency: the int32 enum
;   GCLatencyMode; its stub returns 'unsigned int8', the narrow 'enum' result
;   rule of managed-call-narrow.ll) (the row '5.20.3 MS5 amended (...
;   _Bool/bool ... out column is ldc.i4.0; cgt.un)' of 2026-11-04: C11
;   6.3.1.2); a 'void' stub has no result conversion. An 8- or 16-bit 'val'
;   result that the IR marks 'signext' or 'zeroext' ends with 'conv.i1' or
;   'conv.i2' ('signext'), or 'conv.u1' or 'conv.u2' ('zeroext'), directly
;   after the access and directly before 'ret' (MS5's 'val' row as built;
;   LLVM's meaning of 'signext' and 'zeroext' on a return value): the stub
;   returns int32 (MSIL::Signature has no narrow integers), and its caller
;   uses that int32 as the extended C value. clang marks narrow results so on
;   msil64-unknown-dotnet, not on the host triples; this file gives every
;   triple the same IR. The attribute decides, not the signedness of the CIL
;   type: 'signed char' and 'unsigned char' results for int8 (m_tosbyte_sc,
;   m_tosbyte_uc) and for uint8 (m_tobyte_sc, m_tobyte_uc), and 'short' and
;   'unsigned short' results for int16 (m_toint16_s, m_toint16_us) and for
;   uint16 (m_touint16_s, m_touint16_us). A stub made only of those rows is
;   exactly '.maxstack', '[ldarg]*', '<access>', ['ldc.i4.0; cgt.un' or
;   'conv.<i1|u1|i2|u2>',] 'ret' (m_abs, m_cursor, m_isdigit, m_sqrt,
;   m_alloc, m_enum, m_little, m_collect, m_tobyte, m_tosbyte, m_latency and
;   the eight narrow results), as are the GCHandle rows (m_free 'this-gch':
;   'FromIntPtr; box; unbox' and the access; m_toptr 'gch': 'FromIntPtr' and
;   the 'ToIntPtr' member).
; - Handles (5.20.9 MH1, MH2, MH4, ML2 (3)): an object argument is
;   dereferenced by 'GCHandle::FromIntPtr; box GCHandle; unbox GCHandle; call
;   instance object GCHandle::get_Target()', then 'castclass' to the CIL type
;   ('obj', 'this-obj': a TypeRef such as System.String, System.IO.Stream or
;   the nested Ns.Outer/Inner, or the TypeSpec uint8[]), 'unbox.any V'
;   ('boxed') or 'unbox V' ('this-val', also before 'ldfld'). A method or
;   property of a value type called on a 'this-val' or 'this-gch' handle,
;   without "msil-managed-decl" (none of this file's declarations has it),
;   is in object-form text and in objects the placeholder 'constrained. V'
;   then 'callvirt V::M' that msil-ld rewrites after binding (5.20.3 MS5 as
;   amended again in section 12 on 2026-11-04), for a member that V declares
;   (m_ticks, m_free) and for one that V only inherits from System.ValueType
;   (m_fthash, GetHashCode of ComTypes.FILETIME). Whole-program text, which
;   no msil-ld rewrites, has the final forms instead (the row '5.20.5 ML2
;   (2) and 5.20.6 MW2 amended; refines (d) ...' of 2026-11-04): 'call
;   instance <sig> V::M' (m_ticks, m_free) and, for System.Object's
;   GetHashCode, 'constrained. V' then 'callvirt instance int32
;   [mscorlib]System.Object::GetHashCode()' (m_fthash); managed-call-access.ll
;   has the other cases. The stub-internal GCHandle rows that the conversions
;   use (FromIntPtr, get_Target, Alloc, ToIntPtr) keep 'call'; the
;   'constrained.' token is a TypeRef, as 'unbox V's is. The final forms that
;   "msil-managed-decl" selects are managed-call-access.ll's. A reference
;   result becomes 'GCHandle::Alloc(object); GCHandle::ToIntPtr' ('obj'), a
;   value-type result is boxed first ('boxed', and a value-type .ctor:
;   'newobj', then 'box'), and a GCHandle result is 'GCHandle::ToIntPtr'
;   ('gch', never boxed).
;   MH1's handle 0 is null (5.20.9 MH1 as built in section 12 on 2026-11-04):
;   an 'obj' argument (m_exists_h, m_copy, the second argument of m_write), a
;   'this-obj' handle (m_write, m_dispose, m_length, m_count, m_inner), a
;   'this-val' handle (m_ticks, m_stamp, m_fthash) and a 'boxed' argument
;   (both of m_cmpdate) are tested first ('ldarg; brtrue <deref>; ldnull; br
;   <join>'), and the dereference ends at the join label: after 'castclass'
;   for 'obj' and 'this-obj', before 'unbox V' or 'unbox.any V' (so the null
;   reaches them and they throw NullReferenceException; a null and a managed
;   pointer or a value could not meet at the join). A 'gch' or 'this-gch'
;   value goes to GCHandle::FromIntPtr directly, with no test (m_toptr,
;   m_free): 0 throws InvalidOperationException there, as
;   GCHandle.FromIntPtr(IntPtr.Zero) does.
; - Strings (5.20.10): 'utf8' counts bytes with an IL loop ('ldind.u1' or
;   'ldind.i1'), then 'newobj UTF8Encoding::.ctor(bool, bool)' with (false,
;   true) and 'callvirt Encoding::GetString(uint8*, int32)'; 'utf16' is
;   'newobj String::.ctor(char*)'; 'utf32' counts 4-byte units, then 'newobj
;   UTF32Encoding::.ctor(bool, bool, bool)' with (false, false, true) and
;   GetString. The constructor arguments are pinned on the two or three lines
;   before each 'newobj' (ENC8, ENC32). Never Encoding::get_UTF8/get_UTF32,
;   Marshal::PtrToStringUTF8 or PtrToStringAnsi, and no clause without
;   'msil-managed-catch' (MX1).
; - Accesses: 'call' (static), 'callvirt' (an instance method of a class and
;   of an interface, a property getter, and after 'constrained.' a method of
;   a value-type 'this'), 'newobj' (a
;   class and a value type), 'newarr' (uint8[] and System.IO.Stream[]),
;   'ldsfld' and 'ldfld'; nested types (Ns.Outer/Inner and
;   System.Environment/SpecialFolder in a signature).
; - Scopes: '[mscorlib]' in whole-program text (MW2), '[System]' for the
;   declarations with ASSEMBLY "System" (m_freq, m_ts), and '[msil.bcl]' for
;   everything in object-form text and in objects (MW1).
; - Parents (contract MK3): in object-form text and in objects, a static
;   member of a TYPE written 'valuetype T' has the TypeSpec parent
;   'valuetype [msil.bcl]T' (m_cmpdate's DateTime::Compare, m_now's
;   DateTime::get_Now); GCHandle's static members (m_toptr, m_galloc),
;   instance members and constructors keep the TypeRef, and whole-program
;   text keeps '[mscorlib]T::M' for all of them (managed-call-typespec.ll has
;   the other cases).
; - 64-bit integers: a C 64-bit integer is native int in the stub's
;   signature (D2), so an int64 or uint64 CIL type takes a conversion, as the
;   user's mc1-int64 pick says ("Fix it inside MC-1: conv.i8 on int64
;   arguments, conv.i on int64 results"): 'conv.i8' after the 'ldarg' of an
;   int64 argument and 'conv.u8' after that of a uint64 one, 'conv.i' after
;   an int64 result and 'conv.u' after a uint64 one, pinned with CHECK-NEXT
;   (m_max64, m_memcpy, m_mkdate, m_ticks, m_freq, m_stamp, m_ts, and
;   m_enum64, an enum argument and result of IR type i64).
; - Run-time checks in whole-program text, which no link checks (the WP
;   lines; object-form text and objects have none of them, the OF lines):
;   the 'this' handle of a 'this-obj' member is cast to the TypeSpec 'class
;   T' (m_write, m_dispose, m_length, m_count, m_inner) and that of a
;   'this-val' member unboxed to the TypeSpec 'valuetype V' (m_ticks,
;   m_stamp, m_fthash, whose 'constrained. V' keeps the TypeRef), so a TYPE
;   of the other kind fails to load (T-M16's check); 'this-gch' (m_free) and
;   the argument conversions keep their TypeRefs. A constructor names its
;   TYPE the same way: 'castclass class T' right after a class's 'newobj'
;   (m_mkex), and 'box valuetype V' for a value type's 'boxed' result
;   (m_mkdate). Every 'enum' parameter in C order, and then an 'enum'
;   result, is checked before anything else in the stub (m_enum, m_folder,
;   m_latency, m_enum64, whose parameter and result are both checked),
;   without a local: 'ldtoken E', System.Type::GetTypeFromHandle and
;   get_IsEnum(), then 'sizeof E' against the C value's width in bytes
;   (an i1 counts 8 bits), throwing T-M7's text as InvalidProgramException;
;   the check's labels are the stub's first ones (m_folder's null test moves
;   up), so m_latency, whose C '_Bool' result reads the int32 enum
;   GCLatencyMode, throws there (msil-ld refuses it in a link, T-M7).
; Whole-program text (CORE=mscorlib) and object-form text (CORE=msil.bcl) on
; both host x86_64 triples and msil64-unknown-dotnet at -O0 and -O2 (the
; bodies do not depend on the level); the whole-program text assembles. The
; objects (msil64-unknown-dotnet at -O0 and -O2, each host triple at one
; level), through llvm-cil-dump: the token kinds of the managed members and
; types (class and valuetype in member signatures, TypeRef and TypeSpec
; operands, nested TypeRefs), the link-info symbol table (each emitted stub
; is a LOCAL FUNCTION symbol, as every internal function's method is, and
; never a GLOBAL or WEAK one: MW1 as amended in section 12 on 2026-11-04, and
; 5.8.5 C3; NSTUB counts the table's '__msil_mc.' lines, so no other symbol
; has the prefix), and the object oracle
; (5.12.6): the object written by llc and the object ilasm makes from the
; object-form text have the same canonical listing and link info, which
; proves the written labels and the branch offsets of the length loops (both
; canonical listings drop their 'pe' line: on msil64-unknown-dotnet llc writes
; a PE32/I386 envelope, P-1a, while ilasm here always writes PE32+ x64).
; ilasm writes any TypeRef to System.String into mscorlib, whatever scope
; the text names (an assembly reference it autodetects), so the oracle
; maps the ilasm side's [mscorlib]System.String back to [msil.bcl] and drops
; that reference; llc's object must name [msil.bcl] (DUMP).
; Not pinned: .maxstack values, the stubs' own locals and label names (the
; enum check's local follows them and its labels are pinned by number), the
; null tests of S1-S3 and MH2, MH1's null test of an 'object' argument
; (m_keepalive), and whether an 'object' argument gets a 'castclass'.

; DEFINE: %{nots} = --implicit-check-not=get_UTF8 --implicit-check-not=get_UTF32 \
; DEFINE:   --implicit-check-not=PtrToStringUTF8 --implicit-check-not=PtrToStringAnsi \
; DEFINE:   --implicit-check-not=.try --implicit-check-not="LLVM ERROR"
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib -DSYS=System --input-file=%t.wp.%{t}%{o}.il %{nots} && \
; DEFINE:   grep -B2 "UTF8Encoding::.ctor" %t.wp.%{t}%{o}.il | FileCheck %s --check-prefix=ENC8 && \
; DEFINE:   grep -B3 "UTF32Encoding::.ctor" %t.wp.%{t}%{o}.il | FileCheck %s --check-prefix=ENC32 && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl -DSYS=msil.bcl --input-file=%t.of.%{t}%{o}.il %{nots}

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP %{nots} && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.symtab && \
; DEFINE:   FileCheck %s --check-prefix=LINK --input-file=%t.%{t}%{o}.symtab && \
; DEFINE:   grep -c __msil_mc %t.%{t}%{o}.symtab | FileCheck %s --check-prefix=NSTUB
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of.%{t}%{o}.ilasm.o %t.of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t.%{t}%{o}.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" -e "/^assemblyref 'mscorlib' /d" \
; DEFINE:     -e "s/\[mscorlib\]System\.String::/[msil.bcl]System.String::/g" \
; DEFINE:     -e "s/\[mscorlib\]System\.String$/[msil.bcl]System.String/" > %t.%{t}%{o}.canon2 && \
; DEFINE:   diff %t.%{t}%{o}.canon1 %t.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of.%{t}%{o}.ilasm.o > %t.%{t}%{o}.link2 && \
; DEFINE:   diff %t.%{t}%{o}.link1 %t.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}

source_filename = "managed-call-bodies.c"

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Math::Abs(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_abs'{{$}}
declare i32 @__msil_mc.m_abs(i32 noundef) #0

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_max64'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u8{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.u8{{$}}
; CHECK-NEXT:  {{^}}  call uint64 {{\[}}[[CORE]]]System.Math::Max(uint64, uint64){{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_max64'{{$}}
declare i64 @__msil_mc.m_max64(i64 noundef, i64 noundef) #1

; CHECK-LABEL: .method assembly static void '__msil_mc.m_cursor'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.Console::set_CursorVisible(bool){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_cursor'{{$}}
declare void @__msil_mc.m_cursor(i32 noundef) #2

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_isdigit'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call bool {{\[}}[[CORE]]]System.Char::IsDigit(char){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_isdigit'{{$}}
declare zeroext i1 @__msil_mc.m_isdigit(i16 noundef zeroext) #3

; CHECK-LABEL: .method assembly static float64 '__msil_mc.m_sqrt'(float64) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 {{\[}}[[CORE]]]System.Math::Sqrt(float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_sqrt'{{$}}
declare double @__msil_mc.m_sqrt(double noundef) #4

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_alloc'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_alloc'{{$}}
declare ptr @__msil_mc.m_alloc(i64 noundef) #5

; CHECK-LABEL: .method assembly static void '__msil_mc.m_memcpy'(native int, native int, native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_memcpy'{{$}}
declare void @__msil_mc.m_memcpy(ptr noundef, ptr noundef, i64 noundef, i64 noundef) #6

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_enum'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.IO.FileMode{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_enum': parameter 1 passes 32 bits for 'System.IO.FileMode', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]Ns.Enums::Take(valuetype {{\[}}[[CORE]]]System.IO.FileMode){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_enum'{{$}}
declare i32 @__msil_mc.m_enum(i32 noundef) #7

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_exists'(native int) cil managed
; CHECK:       {{^}}  ldind.{{[iu]}}1{{$}}
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.UTF8Encoding::.ctor(bool, bool){{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  call bool {{\[}}[[CORE]]]System.IO.File::Exists(string){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_exists'{{$}}
declare zeroext i1 @__msil_mc.m_exists(ptr noundef) #8

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_exists16'(native int) cil managed
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.String::.ctor(char*){{$}}
; CHECK:       {{^}}  call bool {{\[}}[[CORE]]]System.IO.File::Exists(string){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_exists16'{{$}}
declare zeroext i1 @__msil_mc.m_exists16(ptr noundef) #9

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_exists32'(native int) cil managed
; CHECK:       {{^}}  ldind.{{[iu]}}4{{$}}
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.UTF32Encoding::.ctor(bool, bool, bool){{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  call bool {{\[}}[[CORE]]]System.IO.File::Exists(string){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_exists32'{{$}}
declare zeroext i1 @__msil_mc.m_exists32(ptr noundef) #10

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_exists_h'(native int) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass {{\[}}[[CORE]]]System.String{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  call bool {{\[}}[[CORE]]]System.IO.File::Exists(string){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_exists_h'{{$}}
declare zeroext i1 @__msil_mc.m_exists_h(ptr noundef) #11

; CHECK-LABEL: .method assembly static void '__msil_mc.m_keepalive'(native int) cil managed
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK:       {{^}}  call void {{\[}}[[CORE]]]System.GC::KeepAlive(object){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_keepalive'{{$}}
declare void @__msil_mc.m_keepalive(ptr noundef) #12

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_stdout'() cil managed
; CHECK:       {{^}}  call class {{\[}}[[CORE]]]System.IO.Stream {{\[}}[[CORE]]]System.Console::OpenStandardOutput(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_stdout'{{$}}
declare ptr @__msil_mc.m_stdout() #13

; CHECK-LABEL: .method assembly static void '__msil_mc.m_copy'(native int, int32, native int, int32) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass uint8[]{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.Runtime.InteropServices.Marshal::Copy(uint8[], int32, native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_copy'{{$}}
declare void @__msil_mc.m_copy(ptr noundef, i32 noundef, ptr noundef, i32 noundef) #14

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_cmpdate'(native int, native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  unbox.any {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  unbox.any {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call int32 {{\[}}[[CORE]]]System.DateTime::Compare(valuetype {{\[}}[[CORE]]]System.DateTime, valuetype {{\[}}[[CORE]]]System.DateTime){{$}}
; OF-NEXT:     {{^}}  call int32 valuetype {{\[}}[[CORE]]]System.DateTime::Compare(valuetype {{\[}}[[CORE]]]System.DateTime, valuetype {{\[}}[[CORE]]]System.DateTime){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_cmpdate'{{$}}
declare i32 @__msil_mc.m_cmpdate(ptr noundef, ptr noundef) #15

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_now'() cil managed
; WP:          {{^}}  call valuetype {{\[}}[[CORE]]]System.DateTime {{\[}}[[CORE]]]System.DateTime::get_Now(){{$}}
; OF:          {{^}}  call valuetype {{\[}}[[CORE]]]System.DateTime valuetype {{\[}}[[CORE]]]System.DateTime::get_Now(){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_now'{{$}}
declare ptr @__msil_mc.m_now() #16

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_mkdate'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.DateTime::.ctor(int64){{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.DateTime{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]System.DateTime{{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_mkdate'{{$}}
declare ptr @__msil_mc.m_mkdate(i64 noundef) #17

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_ticks'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call instance int64 {{\[}}[[CORE]]]System.DateTime::get_Ticks(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}[[CORE]]]System.DateTime{{$}}
; OF-NEXT:     {{^}}  callvirt instance int64 {{\[}}[[CORE]]]System.DateTime::get_Ticks(){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ticks'{{$}}
declare i64 @__msil_mc.m_ticks(ptr noundef) #18

; CHECK-LABEL: .method assembly static void '__msil_mc.m_write'(native int, native int, int32, int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]System.IO.Stream{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]System.IO.Stream{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}  castclass uint8[]{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance void {{\[}}[[CORE]]]System.IO.Stream::Write(uint8[], int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_write'{{$}}
declare void @__msil_mc.m_write(ptr noundef, ptr noundef, i32 noundef, i32 noundef) #19

; CHECK-LABEL: .method assembly static void '__msil_mc.m_dispose'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]System.IDisposable{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]System.IDisposable{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance void {{\[}}[[CORE]]]System.IDisposable::Dispose(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_dispose'{{$}}
declare void @__msil_mc.m_dispose(ptr noundef) #20

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_length'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]System.Array{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]System.Array{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Array::get_Length(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_length'{{$}}
declare i32 @__msil_mc.m_length(ptr noundef) #21

; CHECK-LABEL: .method assembly static void '__msil_mc.m_free'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; WP-NEXT:     {{^}}  call instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; OF-NEXT:     {{^}}  callvirt instance void {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Free(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_free'{{$}}
declare void @__msil_mc.m_free(ptr noundef) #22

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_toptr'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_toptr'{{$}}
declare i64 @__msil_mc.m_toptr(i64 noundef) #23

; The 'obj' argument and the 'gch' result (never boxed, so no null test).
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_galloc'(native int) cil managed
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_galloc'{{$}}
declare i64 @__msil_mc.m_galloc(ptr noundef) #24

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_newbytes'(int32) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  newarr {{(uint8|\[[a-z.]+\]System\.Byte)$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_newbytes'{{$}}
declare ptr @__msil_mc.m_newbytes(i32 noundef) #25

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_newstreams'(int32) cil managed
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  newarr {{\[}}[[CORE]]]System.IO.Stream{{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_newstreams'{{$}}
declare ptr @__msil_mc.m_newstreams(i32 noundef) #26

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_mkex'(native int) cil managed
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.UTF8Encoding::.ctor(bool, bool){{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.EntryPointNotFoundException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}mscorlib]System.EntryPointNotFoundException{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_mkex'{{$}}
declare ptr @__msil_mc.m_mkex(ptr noundef) #27

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_freq'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldsfld int64 {{\[}}[[SYS]]]System.Diagnostics.Stopwatch::Frequency{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_freq'{{$}}
declare i64 @__msil_mc.m_freq() #28

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_little'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldsfld bool {{\[}}[[CORE]]]System.BitConverter::IsLittleEndian{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_little'{{$}}
declare zeroext i1 @__msil_mc.m_little() #29

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_count'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]Ns.Holder{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]Ns.Holder{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  ldfld int32 {{\[}}[[CORE]]]Ns.Holder::Count{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_count'{{$}}
declare i32 @__msil_mc.m_count(ptr noundef) #30

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_stamp'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]Ns.Stamp{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]Ns.Stamp{{$}}
; CHECK-NEXT:  {{^}}  ldfld int64 {{\[}}[[CORE]]]Ns.Stamp::Ticks{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_stamp'{{$}}
declare i64 @__msil_mc.m_stamp(ptr noundef) #31

; CHECK-LABEL: .method assembly static void '__msil_mc.m_collect'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  call void {{\[}}[[CORE]]]System.GC::Collect(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_collect'{{$}}
declare void @__msil_mc.m_collect() #32

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_folder'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.Environment/SpecialFolder{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.Environment/SpecialFolder{{$}}
; WP-NEXT:     {{^}}  ldc.i4.4{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_folder': parameter 1 passes 32 bits for 'System.Environment/SpecialFolder', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call string {{\[}}[[CORE]]]System.Environment::GetFolderPath(valuetype {{\[}}[[CORE]]]System.Environment/SpecialFolder){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_folder'{{$}}
declare ptr @__msil_mc.m_folder(i32 noundef) #33

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_ts'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  call int64 {{\[}}[[SYS]]]System.Diagnostics.Stopwatch::GetTimestamp(){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ts'{{$}}
declare i64 @__msil_mc.m_ts() #34

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_inner'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; WP-NEXT:     {{^}}  castclass class {{\[}}[[CORE]]]Ns.Outer/Inner{{$}}
; OF-NEXT:     {{^}}  castclass {{\[}}[[CORE]]]Ns.Outer/Inner{{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; CHECK-NEXT:  {{^}}  callvirt instance int32 {{\[}}[[CORE]]]Ns.Outer/Inner::get_Value(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_inner'{{$}}
declare i32 @__msil_mc.m_inner(ptr noundef) #35

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_args'() cil managed
; CHECK:       {{^}}  call string[] {{\[}}[[CORE]]]System.Environment::GetCommandLineArgs(){{$}}
; CHECK:       {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::Alloc(object){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_args'{{$}}
declare ptr @__msil_mc.m_args() #36

; A 'this-val' call of a method that V only inherits (System.ValueType's
; GetHashCode on ComTypes.FILETIME): without "msil-managed-decl", object-form
; text and objects get the same placeholder 'constrained. V' + 'callvirt
; V::M' as m_ticks, which msil-ld rewrites to 'callvirt
; System.ValueType::GetHashCode' after binding, and whole-program text calls
; System.Object's GetHashCode after the same 'constrained. V' ('call' with a
; managed pointer as 'this' would be invalid here, ECMA-335 III.2.1).
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_fthash'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue{{(\.s)?}} [[NOTNULL:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  br{{(\.s)?}} [[JOIN:@MC[0-9]+_[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}[[NOTNULL]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle{{$}}
; CHECK-NEXT:  {{^}}  call instance object {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::get_Target(){{$}}
; CHECK-NEXT:  {{^}}[[JOIN]]:{{$}}
; WP-NEXT:     {{^}}  unbox valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; OF-NEXT:     {{^}}  unbox {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; CHECK-NEXT:  {{^}}  constrained. {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; WP-NEXT:     {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Object::GetHashCode(){{$}}
; OF-NEXT:     {{^}}  callvirt instance int32 {{\[}}[[CORE]]]System.Runtime.InteropServices.ComTypes.FILETIME::GetHashCode(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_fthash'{{$}}
declare i32 @__msil_mc.m_fthash(ptr noundef) #37

; A C '_Bool' result (IR 'zeroext i1') of the 'val' row on an 8-bit integer
; CIL type and of the 'enum' row is C's conversion to _Bool: 'ldc.i4.0;
; cgt.un' before 'ret', as for the 'bool' row (m_isdigit), so 2 and -1
; become 1.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tobyte'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call uint8 {{\[}}[[CORE]]]System.Convert::ToByte(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tobyte'{{$}}
declare zeroext i1 @__msil_mc.m_tobyte(i32 noundef) #38

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tosbyte'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int8 {{\[}}[[CORE]]]System.Convert::ToSByte(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tosbyte'{{$}}
declare zeroext i1 @__msil_mc.m_tosbyte(i32 noundef) #39

; CHECK-LABEL: .method assembly static unsigned int8 '__msil_mc.m_latency'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]System.Runtime.GCLatencyMode{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]System.Runtime.GCLatencyMode{{$}}
; WP-NEXT:     {{^}}  ldc.i4.1{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_latency': the result has 8 bits for 'System.Runtime.GCLatencyMode', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.GCLatencyMode {{\[}}[[CORE]]]System.Runtime.GCSettings::get_LatencyMode(){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_latency'{{$}}
declare zeroext i1 @__msil_mc.m_latency() #40

; An enum argument and result of IR type i64 (a C 64-bit integer, native int
; in the stub's signature): 'conv.i8' after the 'ldarg', 'conv.i' after the
; call.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_enum64'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Big{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F:[0-9]+]]_0{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Big{{$}}
; WP-NEXT:     {{^}}  ldc.i4.8{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_1{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_0:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_enum64': parameter 1 passes 64 bits for 'Ns.Big', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_1:{{$}}
; WP-NEXT:     {{^}}  ldtoken {{\[}}[[CORE]]]Ns.Big{{$}}
; WP-NEXT:     {{^}}  call class {{\[}}mscorlib]System.Type {{\[}}mscorlib]System.Type::GetTypeFromHandle(valuetype {{\[}}mscorlib]System.RuntimeTypeHandle){{$}}
; WP-NEXT:     {{^}}  callvirt instance bool {{\[}}mscorlib]System.Type::get_IsEnum(){{$}}
; WP-NEXT:     {{^}}  brfalse.s @MC[[F]]_2{{$}}
; WP-NEXT:     {{^}}  sizeof {{\[}}[[CORE]]]Ns.Big{{$}}
; WP-NEXT:     {{^}}  ldc.i4.8{{$}}
; WP-NEXT:     {{^}}  beq.s @MC[[F]]_3{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_2:{{$}}
; WP-NEXT:     {{^}}  ldstr "managed call 'm_enum64': the result has 64 bits for 'Ns.Big', which is not an enum of that width"{{$}}
; WP-NEXT:     {{^}}  newobj instance void {{\[}}mscorlib]System.InvalidProgramException::.ctor(string){{$}}
; WP-NEXT:     {{^}}  throw{{$}}
; WP-NEXT:     {{^}}@MC[[F]]_3:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]Ns.Big {{\[}}[[CORE]]]Ns.Enums::Flip(valuetype {{\[}}[[CORE]]]Ns.Big){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_enum64'{{$}}
declare i64 @__msil_mc.m_enum64(i64 noundef) #41

; Narrow 'val' results marked 'signext' or 'zeroext': the stub returns
; int32, and the conversion that the attribute names comes directly after
; the call and directly before 'ret', whatever the CIL type's signedness
; (_sc 'signed char', _uc 'unsigned char', _s 'short', _us 'unsigned short').
; int8 Convert::ToSByte(int32):
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tosbyte_sc'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int8 {{\[}}[[CORE]]]System.Convert::ToSByte(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tosbyte_sc'{{$}}
declare signext i8 @__msil_mc.m_tosbyte_sc(i32 noundef) #42

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tosbyte_uc'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int8 {{\[}}[[CORE]]]System.Convert::ToSByte(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tosbyte_uc'{{$}}
declare zeroext i8 @__msil_mc.m_tosbyte_uc(i32 noundef) #43

; uint8 Convert::ToByte(int32):
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tobyte_uc'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call uint8 {{\[}}[[CORE]]]System.Convert::ToByte(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tobyte_uc'{{$}}
declare zeroext i8 @__msil_mc.m_tobyte_uc(i32 noundef) #44

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_tobyte_sc'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call uint8 {{\[}}[[CORE]]]System.Convert::ToByte(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_tobyte_sc'{{$}}
declare signext i8 @__msil_mc.m_tobyte_sc(i32 noundef) #45

; int16 Convert::ToInt16(int32):
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_toint16_s'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int16 {{\[}}[[CORE]]]System.Convert::ToInt16(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_toint16_s'{{$}}
declare signext i16 @__msil_mc.m_toint16_s(i32 noundef) #46

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_toint16_us'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int16 {{\[}}[[CORE]]]System.Convert::ToInt16(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_toint16_us'{{$}}
declare zeroext i16 @__msil_mc.m_toint16_us(i32 noundef) #47

; uint16 Convert::ToUInt16(int32):
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_touint16_us'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call uint16 {{\[}}[[CORE]]]System.Convert::ToUInt16(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_touint16_us'{{$}}
declare zeroext i16 @__msil_mc.m_touint16_us(i32 noundef) #48

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_touint16_s'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call uint16 {{\[}}[[CORE]]]System.Convert::ToUInt16(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_touint16_s'{{$}}
declare signext i16 @__msil_mc.m_touint16_s(i32 noundef) #49

; The callers call the stubs as ordinary methods of the module.
; CHECK-LABEL: .method public static int32 'use_all'(native int, native int, native int) cil managed
; CHECK:       call int32 '__msil_mc.m_abs'(int32)
; CHECK:       call native int '__msil_mc.m_max64'(native int, native int)
; CHECK:       call void '__msil_mc.m_cursor'(int32)
; CHECK:       call native int '__msil_mc.m_mkex'(native int)
; CHECK:       call native int '__msil_mc.m_args'()
; CHECK:       call int32 '__msil_mc.m_fthash'(native int)
define i32 @use_all(ptr %p, ptr %h, ptr %w) {
  %a = call i32 @__msil_mc.m_abs(i32 noundef -5)
  %b = call i64 @__msil_mc.m_max64(i64 noundef 1, i64 noundef 2)
  call void @__msil_mc.m_cursor(i32 noundef 1)
  %c = call zeroext i1 @__msil_mc.m_isdigit(i16 noundef zeroext 49)
  %d = call double @__msil_mc.m_sqrt(double noundef 4.0)
  %e = call ptr @__msil_mc.m_alloc(i64 noundef 16)
  call void @__msil_mc.m_memcpy(ptr noundef %e, ptr noundef %p, i64 noundef 16, i64 noundef 8)
  %f = call i32 @__msil_mc.m_enum(i32 noundef 3)
  %g = call zeroext i1 @__msil_mc.m_exists(ptr noundef %p)
  %i = call zeroext i1 @__msil_mc.m_exists16(ptr noundef %w)
  %j = call zeroext i1 @__msil_mc.m_exists32(ptr noundef %w)
  %k = call zeroext i1 @__msil_mc.m_exists_h(ptr noundef %h)
  call void @__msil_mc.m_keepalive(ptr noundef %h)
  %l = call ptr @__msil_mc.m_stdout()
  call void @__msil_mc.m_copy(ptr noundef %h, i32 noundef 0, ptr noundef %e, i32 noundef 4)
  %m = call i32 @__msil_mc.m_cmpdate(ptr noundef %h, ptr noundef %h)
  %n = call ptr @__msil_mc.m_now()
  %o = call ptr @__msil_mc.m_mkdate(i64 noundef 7)
  %q = call i64 @__msil_mc.m_ticks(ptr noundef %o)
  call void @__msil_mc.m_write(ptr noundef %l, ptr noundef %h, i32 noundef 0, i32 noundef 4)
  call void @__msil_mc.m_dispose(ptr noundef %l)
  %r = call i32 @__msil_mc.m_length(ptr noundef %h)
  call void @__msil_mc.m_free(ptr noundef %l)
  %s = call i64 @__msil_mc.m_toptr(i64 noundef 9)
  %t = call i64 @__msil_mc.m_galloc(ptr noundef %h)
  %u = call ptr @__msil_mc.m_newbytes(i32 noundef 4)
  %v = call ptr @__msil_mc.m_newstreams(i32 noundef 2)
  %x = call ptr @__msil_mc.m_mkex(ptr noundef %p)
  %y = call i64 @__msil_mc.m_freq()
  %z = call zeroext i1 @__msil_mc.m_little()
  %aa = call i32 @__msil_mc.m_count(ptr noundef %h)
  %ab = call i64 @__msil_mc.m_stamp(ptr noundef %h)
  call void @__msil_mc.m_collect()
  %ac = call ptr @__msil_mc.m_folder(i32 noundef 5)
  %ad = call i64 @__msil_mc.m_ts()
  %ae = call i32 @__msil_mc.m_inner(ptr noundef %h)
  %af = call ptr @__msil_mc.m_args()
  %ag = call i32 @__msil_mc.m_fthash(ptr noundef %h)
  %ah = call zeroext i1 @__msil_mc.m_tobyte(i32 noundef 2)
  %ai = call zeroext i1 @__msil_mc.m_tosbyte(i32 noundef -1)
  %aj = call zeroext i1 @__msil_mc.m_latency()
  %ak = call i64 @__msil_mc.m_enum64(i64 noundef 3)
  %n1 = call signext i8 @__msil_mc.m_tosbyte_sc(i32 noundef -1)
  %n2 = call zeroext i8 @__msil_mc.m_tosbyte_uc(i32 noundef -1)
  %n3 = call zeroext i8 @__msil_mc.m_tobyte_uc(i32 noundef 255)
  %n4 = call signext i8 @__msil_mc.m_tobyte_sc(i32 noundef 255)
  %n5 = call signext i16 @__msil_mc.m_toint16_s(i32 noundef -1)
  %n6 = call zeroext i16 @__msil_mc.m_toint16_us(i32 noundef -1)
  %n7 = call zeroext i16 @__msil_mc.m_touint16_us(i32 noundef 65535)
  %n8 = call signext i16 @__msil_mc.m_touint16_s(i32 noundef 65535)
  %r1 = add i32 %a, %f
  %r2 = add i32 %r1, %m
  %r3 = add i32 %r2, %r
  %r4 = add i32 %r3, %aa
  %r5 = add i32 %r4, %ae
  %r6 = add i32 %r5, %ag
  ret i32 %r6
}

; The encoder constructors' arguments, on the lines before each 'newobj'
; (m_exists and m_mkex; m_exists32).
; ENC8:      {{^}}  ldc.i4.0{{$}}
; ENC8-NEXT: {{^}}  ldc.i4.1{{$}}
; ENC8-NEXT: {{^}}  newobj instance void [{{mscorlib|msil.bcl}}]System.Text.UTF8Encoding::.ctor(bool, bool){{$}}
; ENC8-NEXT: {{^--$}}
; ENC8-NEXT: {{^}}  ldc.i4.0{{$}}
; ENC8-NEXT: {{^}}  ldc.i4.1{{$}}
; ENC8-NEXT: {{^}}  newobj instance void [{{mscorlib|msil.bcl}}]System.Text.UTF8Encoding::.ctor(bool, bool){{$}}
; ENC8-NOT:  {{.}}
; ENC32:      {{^}}  ldc.i4.0{{$}}
; ENC32-NEXT: {{^}}  ldc.i4.0{{$}}
; ENC32-NEXT: {{^}}  ldc.i4.1{{$}}
; ENC32-NEXT: {{^}}  newobj instance void [{{mscorlib|msil.bcl}}]System.Text.UTF32Encoding::.ctor(bool, bool, bool){{$}}
; ENC32-NOT:  {{.}}

; Objects: the token kinds (class and valuetype in member signatures,
; TypeRef and TypeSpec operands, nested TypeRefs) and the [msil.bcl] scope.
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_abs' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int32(int32) [msil.bcl]System.Math::'Abs'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_enum' int32(int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(valuetype [msil.bcl]System.IO.FileMode) [msil.bcl]Ns.Enums::'Take'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_exists' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ldind.{{[iu]}}1{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: newobj instance void(bool, bool) [msil.bcl]System.Text.UTF8Encoding::'.ctor'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance string(uint8*, int32) [msil.bcl]System.Text.Encoding::'GetString'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call bool(string) [msil.bcl]System.IO.File::'Exists'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_exists16' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: newobj instance void(char*) [msil.bcl]System.String::'.ctor'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_exists32' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: newobj instance void(bool, bool, bool) [msil.bcl]System.Text.UTF32Encoding::'.ctor'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance string(uint8*, int32) [msil.bcl]System.Text.Encoding::'GetString'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_exists_h' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]System.Runtime.InteropServices.GCHandle(native int) [msil.bcl]System.Runtime.InteropServices.GCHandle::'FromIntPtr'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: box [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call instance object() [msil.bcl]System.Runtime.InteropServices.GCHandle::'get_Target'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.String{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_stdout' native int() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call class [msil.bcl]System.IO.Stream() [msil.bcl]System.Console::'OpenStandardOutput'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]System.Runtime.InteropServices.GCHandle(object) [msil.bcl]System.Runtime.InteropServices.GCHandle::'Alloc'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call native int(valuetype [msil.bcl]System.Runtime.InteropServices.GCHandle) [msil.bcl]System.Runtime.InteropServices.GCHandle::'ToIntPtr'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_copy' void(native int, int32, native int, int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass uint8[]{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call void(uint8[], int32, native int, int32) [msil.bcl]System.Runtime.InteropServices.Marshal::'Copy'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_cmpdate' int32(native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox.any [msil.bcl]System.DateTime{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: unbox.any [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call int32(valuetype [msil.bcl]System.DateTime, valuetype [msil.bcl]System.DateTime) valuetype [msil.bcl]System.DateTime::'Compare'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_now' native int() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]System.DateTime() valuetype [msil.bcl]System.DateTime::'get_Now'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_mkdate' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: newobj instance void(int64) [msil.bcl]System.DateTime::'.ctor'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: box [msil.bcl]System.DateTime{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_ticks' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call instance object() [msil.bcl]System.Runtime.InteropServices.GCHandle::'get_Target'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int64() [msil.bcl]System.DateTime::'get_Ticks'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_write' void(native int, native int, int32, int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]System.IO.Stream{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: castclass uint8[]{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance void(uint8[], int32, int32) [msil.bcl]System.IO.Stream::'Write'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_free' void(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.GCHandle{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance void() [msil.bcl]System.Runtime.InteropServices.GCHandle::'Free'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_newstreams' native int(int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: newarr [msil.bcl]System.IO.Stream{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_freq' native int() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ldsfld int64 [msil.bcl]System.Diagnostics.Stopwatch::'Frequency'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_count' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]Ns.Holder{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldfld int32 [msil.bcl]Ns.Holder::'Count'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_stamp' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: unbox [msil.bcl]Ns.Stamp{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: ldfld int64 [msil.bcl]Ns.Stamp::'Ticks'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_folder' native int(int32) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call string(valuetype [msil.bcl]System.Environment/SpecialFolder) [msil.bcl]System.Environment::'GetFolderPath'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_inner' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: castclass [msil.bcl]Ns.Outer/Inner{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]Ns.Outer/Inner::'get_Value'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_args' native int() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call string[]() [msil.bcl]System.Environment::'GetCommandLineArgs'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_fthash' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call instance object() [msil.bcl]System.Runtime.InteropServices.GCHandle::'get_Target'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: callvirt instance int32() [msil.bcl]System.Runtime.InteropServices.ComTypes.FILETIME::'GetHashCode'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_enum64' native int(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: conv.i8{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]Ns.Big(valuetype [msil.bcl]Ns.Big) [msil.bcl]Ns.Enums::'Flip'{{$}}
; DUMP-NEXT:  {{^}}    L{{[0-9]+}}: conv.i{{$}}
; The narrow 'val' results' conversions, between the call and 'ret'.
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_tosbyte_sc' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int8(int32) [msil.bcl]System.Convert::'ToSByte'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.i1{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_tosbyte_uc' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int8(int32) [msil.bcl]System.Convert::'ToSByte'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.u1{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_tobyte_uc' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call uint8(int32) [msil.bcl]System.Convert::'ToByte'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.u1{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_tobyte_sc' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call uint8(int32) [msil.bcl]System.Convert::'ToByte'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.i1{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_toint16_s' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int16(int32) [msil.bcl]System.Convert::'ToInt16'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.i2{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_toint16_us' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int16(int32) [msil.bcl]System.Convert::'ToInt16'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.u2{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_touint16_us' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call uint16(int32) [msil.bcl]System.Convert::'ToUInt16'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.u2{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_touint16_s' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call uint16(int32) [msil.bcl]System.Convert::'ToUInt16'{{$}}
; DUMP-NEXT:  {{^}}    L2: conv.i2{{$}}
; DUMP-NEXT:  {{^}}    L3: ret{{$}}
; DUMP-LABEL: {{^}}  .method 'use_all' int32(native int, native int, native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.m_abs'{{$}}

; The stubs are internal MethodDefs, each a LOCAL FUNCTION symbol (MW1 as
; amended, C3); use_all is a GLOBAL one.
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_abs' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_max64' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_cursor' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_isdigit' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_sqrt' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_alloc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_memcpy' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_enum' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_exists' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_exists16' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_exists32' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_exists_h' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_keepalive' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_stdout' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_copy' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_cmpdate' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_now' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_mkdate' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_ticks' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_write' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_dispose' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_length' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_free' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_toptr' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_galloc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_newbytes' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_newstreams' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_mkex' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_freq' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_little' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_count' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_stamp' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_collect' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_folder' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_ts' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_inner' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_args' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_fthash' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tobyte' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tosbyte' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_latency' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_enum64' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tosbyte_sc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tosbyte_uc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tobyte_uc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_tobyte_sc' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_toint16_s' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_toint16_us' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_touint16_us' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION LOCAL DEFAULT '__msil_mc.m_touint16_s' method=0x06{{[0-9A-F]+$}}
; LINK-DAG: {{^}}Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'use_all' method=0x06{{[0-9A-F]+$}}
; NSTUB: {{^}}50{{$}}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-conv"="val,val;val" "msil-managed-member"="Max" "msil-managed-sig"="uint64(uint64, uint64)" "msil-managed-type"="System.Math" }
attributes #2 = { "msil-managed-conv"="bool;void" "msil-managed-member"="set_CursorVisible" "msil-managed-sig"="void(bool)" "msil-managed-type"="System.Console" }
attributes #3 = { "msil-managed-conv"="val;bool" "msil-managed-member"="IsDigit" "msil-managed-sig"="bool(char)" "msil-managed-type"="System.Char" }
attributes #4 = { "msil-managed-conv"="val;val" "msil-managed-member"="Sqrt" "msil-managed-sig"="float64(float64)" "msil-managed-type"="System.Math" }
attributes #5 = { "msil-managed-conv"="val;val" "msil-managed-member"="AllocHGlobal" "msil-managed-sig"="native int(native int)" "msil-managed-type"="System.Runtime.InteropServices.Marshal" }
attributes #6 = { "msil-managed-conv"="val,val,val,val;void" "msil-managed-member"="MemoryCopy" "msil-managed-sig"="void(void*, void*, int64, int64)" "msil-managed-type"="System.Buffer" }
attributes #7 = { "msil-managed-conv"="enum;val" "msil-managed-member"="Take" "msil-managed-sig"="int32(valuetype System.IO.FileMode)" "msil-managed-type"="Ns.Enums" }
attributes #8 = { "msil-managed-conv"="utf8;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" }
attributes #9 = { "msil-managed-conv"="utf16;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" }
attributes #10 = { "msil-managed-conv"="utf32;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" }
attributes #11 = { "msil-managed-conv"="obj;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" }
attributes #12 = { "msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" }
attributes #13 = { "msil-managed-conv"=";obj" "msil-managed-member"="OpenStandardOutput" "msil-managed-sig"="class System.IO.Stream()" "msil-managed-type"="System.Console" }
attributes #14 = { "msil-managed-conv"="obj,val,val,val;void" "msil-managed-member"="Copy" "msil-managed-sig"="void(uint8[], int32, native int, int32)" "msil-managed-type"="System.Runtime.InteropServices.Marshal" }
attributes #15 = { "msil-managed-conv"="boxed,boxed;val" "msil-managed-member"="Compare" "msil-managed-sig"="int32(valuetype System.DateTime, valuetype System.DateTime)" "msil-managed-type"="valuetype System.DateTime" }
attributes #16 = { "msil-managed-conv"=";boxed" "msil-managed-member"="get_Now" "msil-managed-sig"="valuetype System.DateTime()" "msil-managed-type"="valuetype System.DateTime" }
attributes #17 = { "msil-managed-conv"="val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" }
attributes #18 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" }
attributes #19 = { "msil-managed-conv"="this-obj,obj,val,val;void" "msil-managed-member"="Write" "msil-managed-sig"="instance void(uint8[], int32, int32)" "msil-managed-type"="System.IO.Stream" }
attributes #20 = { "msil-managed-conv"="this-obj;void" "msil-managed-member"="Dispose" "msil-managed-sig"="instance void()" "msil-managed-type"="System.IDisposable" }
attributes #21 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.Array" }
attributes #22 = { "msil-managed-conv"="this-gch;void" "msil-managed-member"="Free" "msil-managed-sig"="instance void()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #23 = { "msil-managed-conv"="gch;val" "msil-managed-member"="ToIntPtr" "msil-managed-sig"="native int(valuetype System.Runtime.InteropServices.GCHandle)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #24 = { "msil-managed-conv"="obj;gch" "msil-managed-member"="Alloc" "msil-managed-sig"="valuetype System.Runtime.InteropServices.GCHandle(object)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #25 = { "msil-managed-conv"="val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="uint8[]" }
attributes #26 = { "msil-managed-conv"="val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="class System.IO.Stream[]" }
attributes #27 = { "msil-managed-conv"="utf8;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string)" "msil-managed-type"="System.EntryPointNotFoundException" }
attributes #28 = { "msil-managed-asm"="System" "msil-managed-conv"=";val" "msil-managed-member"="Frequency" "msil-managed-sig"="field int64" "msil-managed-type"="System.Diagnostics.Stopwatch" }
attributes #29 = { "msil-managed-conv"=";bool" "msil-managed-member"="IsLittleEndian" "msil-managed-sig"="field bool" "msil-managed-type"="System.BitConverter" }
attributes #30 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="Count" "msil-managed-sig"="instance field int32" "msil-managed-type"="Ns.Holder" }
attributes #31 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="Ticks" "msil-managed-sig"="instance field int64" "msil-managed-type"="valuetype Ns.Stamp" }
attributes #32 = { "msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC" }
attributes #33 = { "msil-managed-conv"="enum;obj" "msil-managed-member"="GetFolderPath" "msil-managed-sig"="string(valuetype System.Environment/SpecialFolder)" "msil-managed-type"="System.Environment" }
attributes #34 = { "msil-managed-asm"="System" "msil-managed-conv"=";val" "msil-managed-member"="GetTimestamp" "msil-managed-sig"="int64()" "msil-managed-type"="System.Diagnostics.Stopwatch" }
attributes #35 = { "msil-managed-conv"="this-obj;val" "msil-managed-member"="get_Value" "msil-managed-sig"="instance int32()" "msil-managed-type"="Ns.Outer/Inner" }
attributes #36 = { "msil-managed-conv"=";obj" "msil-managed-member"="GetCommandLineArgs" "msil-managed-sig"="string[]()" "msil-managed-type"="System.Environment" }
attributes #37 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.Runtime.InteropServices.ComTypes.FILETIME" }
attributes #38 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToByte" "msil-managed-sig"="uint8(int32)" "msil-managed-type"="System.Convert" }
attributes #39 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToSByte" "msil-managed-sig"="int8(int32)" "msil-managed-type"="System.Convert" }
attributes #40 = { "msil-managed-conv"=";enum" "msil-managed-member"="get_LatencyMode" "msil-managed-sig"="valuetype System.Runtime.GCLatencyMode()" "msil-managed-type"="System.Runtime.GCSettings" }
attributes #41 = { "msil-managed-conv"="enum;enum" "msil-managed-member"="Flip" "msil-managed-sig"="valuetype Ns.Big(valuetype Ns.Big)" "msil-managed-type"="Ns.Enums" }
attributes #42 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToSByte" "msil-managed-sig"="int8(int32)" "msil-managed-type"="System.Convert" }
attributes #43 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToSByte" "msil-managed-sig"="int8(int32)" "msil-managed-type"="System.Convert" }
attributes #44 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToByte" "msil-managed-sig"="uint8(int32)" "msil-managed-type"="System.Convert" }
attributes #45 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToByte" "msil-managed-sig"="uint8(int32)" "msil-managed-type"="System.Convert" }
attributes #46 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(int32)" "msil-managed-type"="System.Convert" }
attributes #47 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(int32)" "msil-managed-type"="System.Convert" }
attributes #48 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToUInt16" "msil-managed-sig"="uint16(int32)" "msil-managed-type"="System.Convert" }
attributes #49 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToUInt16" "msil-managed-sig"="uint16(int32)" "msil-managed-type"="System.Convert" }
!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15, !16, !17, !18, !19, !20, !21, !22, !23, !24, !25, !26, !27, !28, !29, !30, !31, !32, !33, !34, !35, !36, !37, !38, !39, !40, !41, !42, !43, !44, !45, !46, !47, !48, !49}
!0 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.m_max64", !"System.Math", !"Max", !"uint64(uint64, uint64)", !"", !"val,val;val", !""}
!2 = !{!"__msil_mc.m_cursor", !"System.Console", !"set_CursorVisible", !"void(bool)", !"", !"bool;void", !""}
!3 = !{!"__msil_mc.m_isdigit", !"System.Char", !"IsDigit", !"bool(char)", !"", !"val;bool", !""}
!4 = !{!"__msil_mc.m_sqrt", !"System.Math", !"Sqrt", !"float64(float64)", !"", !"val;val", !""}
!5 = !{!"__msil_mc.m_alloc", !"System.Runtime.InteropServices.Marshal", !"AllocHGlobal", !"native int(native int)", !"", !"val;val", !""}
!6 = !{!"__msil_mc.m_memcpy", !"System.Buffer", !"MemoryCopy", !"void(void*, void*, int64, int64)", !"", !"val,val,val,val;void", !""}
!7 = !{!"__msil_mc.m_enum", !"Ns.Enums", !"Take", !"int32(valuetype System.IO.FileMode)", !"", !"enum;val", !""}
!8 = !{!"__msil_mc.m_exists", !"System.IO.File", !"Exists", !"bool(string)", !"", !"utf8;bool", !""}
!9 = !{!"__msil_mc.m_exists16", !"System.IO.File", !"Exists", !"bool(string)", !"", !"utf16;bool", !""}
!10 = !{!"__msil_mc.m_exists32", !"System.IO.File", !"Exists", !"bool(string)", !"", !"utf32;bool", !""}
!11 = !{!"__msil_mc.m_exists_h", !"System.IO.File", !"Exists", !"bool(string)", !"", !"obj;bool", !""}
!12 = !{!"__msil_mc.m_keepalive", !"System.GC", !"KeepAlive", !"void(object)", !"", !"obj;void", !""}
!13 = !{!"__msil_mc.m_stdout", !"System.Console", !"OpenStandardOutput", !"class System.IO.Stream()", !"", !";obj", !""}
!14 = !{!"__msil_mc.m_copy", !"System.Runtime.InteropServices.Marshal", !"Copy", !"void(uint8[], int32, native int, int32)", !"", !"obj,val,val,val;void", !""}
!15 = !{!"__msil_mc.m_cmpdate", !"valuetype System.DateTime", !"Compare", !"int32(valuetype System.DateTime, valuetype System.DateTime)", !"", !"boxed,boxed;val", !""}
!16 = !{!"__msil_mc.m_now", !"valuetype System.DateTime", !"get_Now", !"valuetype System.DateTime()", !"", !";boxed", !""}
!17 = !{!"__msil_mc.m_mkdate", !"valuetype System.DateTime", !".ctor", !"instance void(int64)", !"", !"val;boxed", !""}
!18 = !{!"__msil_mc.m_ticks", !"valuetype System.DateTime", !"get_Ticks", !"instance int64()", !"", !"this-val;val", !""}
!19 = !{!"__msil_mc.m_write", !"System.IO.Stream", !"Write", !"instance void(uint8[], int32, int32)", !"", !"this-obj,obj,val,val;void", !""}
!20 = !{!"__msil_mc.m_dispose", !"System.IDisposable", !"Dispose", !"instance void()", !"", !"this-obj;void", !""}
!21 = !{!"__msil_mc.m_length", !"System.Array", !"get_Length", !"instance int32()", !"", !"this-obj;val", !""}
!22 = !{!"__msil_mc.m_free", !"valuetype System.Runtime.InteropServices.GCHandle", !"Free", !"instance void()", !"", !"this-gch;void", !""}
!23 = !{!"__msil_mc.m_toptr", !"valuetype System.Runtime.InteropServices.GCHandle", !"ToIntPtr", !"native int(valuetype System.Runtime.InteropServices.GCHandle)", !"", !"gch;val", !""}
!24 = !{!"__msil_mc.m_galloc", !"valuetype System.Runtime.InteropServices.GCHandle", !"Alloc", !"valuetype System.Runtime.InteropServices.GCHandle(object)", !"", !"obj;gch", !""}
!25 = !{!"__msil_mc.m_newbytes", !"uint8[]", !".ctor", !"instance void(int32)", !"", !"val;obj", !""}
!26 = !{!"__msil_mc.m_newstreams", !"class System.IO.Stream[]", !".ctor", !"instance void(int32)", !"", !"val;obj", !""}
!27 = !{!"__msil_mc.m_mkex", !"System.EntryPointNotFoundException", !".ctor", !"instance void(string)", !"", !"utf8;obj", !""}
!28 = !{!"__msil_mc.m_freq", !"System.Diagnostics.Stopwatch", !"Frequency", !"field int64", !"System", !";val", !""}
!29 = !{!"__msil_mc.m_little", !"System.BitConverter", !"IsLittleEndian", !"field bool", !"", !";bool", !""}
!30 = !{!"__msil_mc.m_count", !"Ns.Holder", !"Count", !"instance field int32", !"", !"this-obj;val", !""}
!31 = !{!"__msil_mc.m_stamp", !"valuetype Ns.Stamp", !"Ticks", !"instance field int64", !"", !"this-val;val", !""}
!32 = !{!"__msil_mc.m_collect", !"System.GC", !"Collect", !"void()", !"", !";void", !""}
!33 = !{!"__msil_mc.m_folder", !"System.Environment", !"GetFolderPath", !"string(valuetype System.Environment/SpecialFolder)", !"", !"enum;obj", !""}
!34 = !{!"__msil_mc.m_ts", !"System.Diagnostics.Stopwatch", !"GetTimestamp", !"int64()", !"System", !";val", !""}
!35 = !{!"__msil_mc.m_inner", !"Ns.Outer/Inner", !"get_Value", !"instance int32()", !"", !"this-obj;val", !""}
!36 = !{!"__msil_mc.m_args", !"System.Environment", !"GetCommandLineArgs", !"string[]()", !"", !";obj", !""}
!37 = !{!"__msil_mc.m_fthash", !"valuetype System.Runtime.InteropServices.ComTypes.FILETIME", !"GetHashCode", !"instance int32()", !"", !"this-val;val", !""}
!38 = !{!"__msil_mc.m_tobyte", !"System.Convert", !"ToByte", !"uint8(int32)", !"", !"val;val", !""}
!39 = !{!"__msil_mc.m_tosbyte", !"System.Convert", !"ToSByte", !"int8(int32)", !"", !"val;val", !""}
!40 = !{!"__msil_mc.m_latency", !"System.Runtime.GCSettings", !"get_LatencyMode", !"valuetype System.Runtime.GCLatencyMode()", !"", !";enum", !""}
!41 = !{!"__msil_mc.m_enum64", !"Ns.Enums", !"Flip", !"valuetype Ns.Big(valuetype Ns.Big)", !"", !"enum;enum", !""}
!42 = !{!"__msil_mc.m_tosbyte_sc", !"System.Convert", !"ToSByte", !"int8(int32)", !"", !"val;val", !""}
!43 = !{!"__msil_mc.m_tosbyte_uc", !"System.Convert", !"ToSByte", !"int8(int32)", !"", !"val;val", !""}
!44 = !{!"__msil_mc.m_tobyte_uc", !"System.Convert", !"ToByte", !"uint8(int32)", !"", !"val;val", !""}
!45 = !{!"__msil_mc.m_tobyte_sc", !"System.Convert", !"ToByte", !"uint8(int32)", !"", !"val;val", !""}
!46 = !{!"__msil_mc.m_toint16_s", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!47 = !{!"__msil_mc.m_toint16_us", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!48 = !{!"__msil_mc.m_touint16_us", !"System.Convert", !"ToUInt16", !"uint16(int32)", !"", !"val;val", !""}
!49 = !{!"__msil_mc.m_touint16_s", !"System.Convert", !"ToUInt16", !"uint16(int32)", !"", !"val;val", !""}
