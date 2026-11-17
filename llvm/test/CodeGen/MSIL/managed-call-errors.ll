; The backend's managed-call errors (ImplementationPlan.md 5.20.3 T-M10 and
; T-M17, 5.20.4 MR4, 5.20.5 ML2 (1); milestone MC-1), each pinned whole, on
; both host x86_64 triples and msil64-unknown-dotnet (one rule for every
; triple), at -O0 and -O2:
; - addr.ll, T-M10 'taking the address of the managed-call function '<f>' is
;   not supported for MSIL yet' for every use of a managed-call declaration
;   that is not the callee of a direct call: an address returned by a
;   function (&f, f_addr), a global initializer (f_init) and an address
;   passed as a call argument (f_arg). <f> is the name without the
;   '__msil_mc.' prefix. Each use in an instruction becomes poison and the
;   initializer's use becomes null (as for 5.15.4's errors), so the IR stays
;   valid: 'ret ptr poison', the argument and '@tbl = global ptr null'; the
;   direct call of f_ok in the same module is kept and compiles. llc reports
;   each error once, exits with 1, never crashes, and still writes the IL
;   (taken from stdout, since llc deletes its -o file after an error), which
;   defines the stub of f_ok and calls it.
; - proto.ll: a direct call of a managed-call function whose function type
;   differs from the declaration's is T-M17 whole, never T-M10 (5.20.4 and
;   T-M17 as amended in section 12 on 2026-11-04, the row '5.20.4 (IR name,
;   MR4) and T-M17 amended (...)' item (2)). The module is what the IR linker
;   makes of two units that declare one external managed call 'p' with
;   different C prototypes (int vs long long) and equal 'msil_method'
;   arguments: one declaration, two equal '!msil.managed.calls' entries, and
;   the second unit's call kept as a direct call with its own function type
;   (a C call through a cast pointer is the same IR). Exactly one error, and
;   no 'taking the address' text; objects alike.
; - proto2.ll: a function that gets T-M17 from MR4 (its two entries differ in
;   the member) and also has such a call ('q', as the IR linker leaves two
;   units' external declarations of one managed call that differ in the
;   member and in the C prototype) gets T-M17 and no T-M10. Two units'
;   internal managed calls never meet like this: the '.__uniq.<n>' of an
;   internal declaration's IR name hashes its '!msil.managed.calls' operands
;   and its function type (5.20.4 as amended), so equal IR names mean equal
;   declarations (clang/test/CodeGen/MSIL/msil-method-uniq.c).
; - ext16.ll, ext8.ll and extmore.ll: T-M17's first form for a managed-call
;   function F with a direct call of F's own function type whose own
;   'signext' or 'zeroext', which F's attributes do not have at that
;   position, would give F another stub (5.20.5's P0 rule as amended in
;   section 12, the row 'fix-loop ownership:
;   `llvm/include/llvm/CLI/ManagedCallIR.h` -> MC-1.d' of 2026-11-11). The IR
;   linker leaves such a call when two units declare F with C prototypes that
;   differ only in the signedness of a narrow integer that the triple's ABI
;   marks 'signext' or 'zeroext': it keeps the first unit's declaration with
;   its attributes, and each call keeps its own (a call through a cast
;   pointer that changes only that is the same IR:
;   clang/test/CodeGen/MSIL/msil-method-cast.c). ext16.ll merges 'unsigned
;   short to16(int)' and 'short to16(int)' for System.Convert::ToInt16: one
;   'declare zeroext i16', whose stub ends in conv.u2, with a 'call zeroext
;   i16' and the second unit's 'call signext i16', which would read 65535 for
;   -1; a third function's 'call signext i16' adds no line, so exactly one
;   error line. ext8.ll merges 'unsigned char f(unsigned char)' and 'signed
;   char f(unsigned char)' for Contoso.Widget::Octet (o8.ll and o8rs.ll of
;   lld/test/MSIL/managed-calls-units.test, in that order): a 'call signext
;   i8' of 'declare zeroext i8', whose stub ends in conv.u1.
;   extmore.ll has the other positions whose stub the extension changes, one
;   error line each (count 8): an 'enum' i8 result declared signext and
;   called zeroext (x_r8: conv.i1 against conv.u1, and the exact result type
;   int8 against unsigned int8); a 'bool' i8 parameter declared without an
;   extension, which the stub narrows, and called zeroext (x_bool); 'enum' i8
;   parameters declared signext (x_es) or without an extension (x_en) and
;   called zeroext, and an 'enum' i16 parameter declared zeroext and called
;   signext (x_eu16), which change the stub's exact parameter type
;   (managed-call-narrow.ll); a 'val' (x_vb) and an 'enum' (x_eb) i1
;   parameter declared zeroext and called signext, which makes the stub
;   narrow it (managed-call-narrow-args.ll); and x_addr, whose address is
;   also taken, gets T-M17 and no T-M10. Text and objects alike, and no
;   'taking the address' line.
; - extok.ll: calls whose own extension gives F no other stub compile with no
;   diagnostic, in text and objects, and the text assembles: two zeroext
;   calls of a zeroext i16 'val' result (k_both); a 'val' i8 (k_p8) and i16
;   (k_p16) parameter called with the other extension, as the IR linker
;   leaves units that msil-ld links (managed-calls-units.test), since the
;   member's call truncates the int32 to the parameter's width; calls without
;   the declaration's extension, which read the declaration's (k_none, a
;   result, and k_nonep, an 'enum' parameter); a 'bool' i8 result called
;   signext (k_bres) and an i1 'enum' result called signext (k_i1r), which
;   the stub makes 0 or 1; a 'bool' parameter declared signext and called
;   zeroext (k_bpar; only a parameter without an extension is narrowed); an
;   'enum' parameter declared without an extension and called signext
;   (k_enum, int8 either way); a 'val' (k_vb) and an 'enum' (k_eb) i1
;   parameter declared without an extension and called signext, narrowed
;   either way; and an i32 result and parameter called signext and zeroext
;   (k_i32).
; - tm17.ll, T-M17 'managed call '<f>' is declared with different
;   'msil_method' or 'msil_catch' arguments in different translation units',
;   once per IR name whose '!msil.managed.calls' entries differ: in the
;   member (d) and only in the catch list (e). Equal entries of one name (k,
;   twice, as an LTO merge of two units gives) and an entry without a
;   declaration in the module (gone) are not errors.
; - tm17b.ll, T-M17's second form 'managed call '<f>' is declared with
;   different 'dllexport', 'constructor' or 'destructor' attributes in
;   different translation units', for the entries of one IR name that differ
;   only in MR4's attributes operand (the 8th): an entry without it and one
;   with "dllexport" (fx_exp), and two constructor priorities (fx_prio).
; - width.ll: the invalid-attributes error 'managed call '<f>' has invalid
;   "msil-managed-*" attributes: <reason>' for hand-written declarations
;   whose IR integer types are not the widths that clang's Sema gives the C
;   types of the conversion (5.20.5 ML2 (1)), the reason being 'parameter
;   <i>: ' or 'the result: ' and then 'conversion '<c>' does not fit the .NET
;   type '<T>' and the IR type '<ty>'': an i5 and an i24 'enum' parameter
;   (System.GCCollectionMode), an i40 'enum' result, an i32 result for an
;   int64 member (Max; llc used to write an int32 stub for it, which PEVerify
;   refuses), an i64 parameter for an int32 member (Abs), and an 'enum'
;   result on a value type's '.ctor' (w_ctor, System.DateTime's 'instance
;   void(int64)' with an i64 result; a '.ctor' result's .NET type is TYPE,
;   MS4). A value type's constructor with an 'enum' result is refused:
;   clang's Sema never writes it, because MS4 and MS5 make a value-type
;   '.ctor' result 'boxed' (llc used to write 'newobj' and then 'conv.i' of
;   the DateTime). Each declaration gets one error, objects alike.
;   widthok.ll: an i1 (a C _Bool) for a uint8 member, as parameters and
;   result, stays accepted.
; - inv.ll and inv2.ll: the same error for a SIGNATURE that does not parse
;   ('int32('), on a declaration (m_bad) and on a hand-written internal
;   "msil-managed-stub" definition (m_bad2), in text and objects.
; - decl.ll: the same error for "msil-managed-decl" (ML1's
;   cli::ManagedDeclAttr, the declaring type of the bound member, which a
;   static method and a 'this-val' or 'this-gch' instance method take;
;   MC-1's review-r5 item 1; managed-call-access.ll pins the accesses it
;   selects), in text and objects, with exactly five lines of output, one
;   error each (count 5). On a static field (dl_sfield, DateTime::MaxValue),
;   on a 'this-val' instance field (dl_ifield, Ns.Stamp::Ticks) and on a
;   'this-obj' method (dl_obj, Array::get_Length), each naming TYPE's own
;   type, the reason is
;   '"msil-managed-decl" belongs only to a static method or to an instance
;   method whose 'this' conversion is 'this-val' or 'this-gch''. On a static
;   method, a type that is neither TYPE's name (without 'valuetype') nor
;   System.ValueType, System.Object or System.Enum is '"msil-managed-decl" is
;   '<D>', which is neither '<TYPE's name>' nor System.ValueType,
;   System.Object or System.Enum': System.Text.Encoding for 'valuetype
;   System.DayOfWeek' (dl_names) and for 'System.Text.UTF8Encoding'
;   (dl_utf8, whose static get_UTF8 Encoding really declares). System.Object
;   on a static method of 'valuetype System.Guid' (dl_ok, ReferenceEquals)
;   is no error.
; - dlg.ll: the same error for the two shapes of a delegate over a C
;   function, which MC-1 does not have (5.20.14 'Delegates', brief Q27 (a);
;   MS3's shape check, cli::checkManagedCallShape, which Sema also applies),
;   for hand-written declarations with the "msil-managed-*" attributes that
;   clang writes, in text and objects, with exactly three lines of output
;   (count 3): the '.ctor' of a class whose signature is 'instance
;   void(object, native int)', also written 'class System.Object' (dg_new
;   and dg_new_obj, System.Action), on the signature, and
;   System.Runtime.InteropServices.Marshal::GetDelegateForFunctionPointer
;   (dg_fp), on the member. The same '.ctor' of a value type (dg_href,
;   HandleRef), Marshal::PtrToStructure, whose parameters are
;   GetDelegateForFunctionPointer's (dg_pts), and the delegate's Invoke
;   (dg_invoke) are no errors.
; tm17b.ll, width.ll, widthok.ll, inv.ll, inv2.ll, decl.ll and dlg.ll run on
; the same three triples at -O0 and -O2 (%{more}), and so do ext16.ll,
; ext8.ll, extmore.ll and extok.ll, in text and as objects (%{ext}).
; - same.ll: only equal duplicate entries; the module compiles without any
;   diagnostic.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{one} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump" --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{addr} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/addr.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=ADDR %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -stop-after=msil-prepare-ir < %t/addr.ll -o - 2>/dev/null \
; DEFINE:   | FileCheck %s --check-prefix=ADDR-IR && \
; DEFINE:   not llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/addr.ll \
; DEFINE:     > %t/addr.%{t}%{o}.il 2>/dev/null && \
; DEFINE:   FileCheck %s --check-prefix=ADDR-IL --input-file=%t/addr.%{t}%{o}.il && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/tm17.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=TM17 %{one} && \
; DEFINE:   llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/same.ll -o /dev/null 2>&1 | count 0 && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/proto.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=PROTO %{one} --implicit-check-not="taking the address" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/proto2.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=PROTO2 --implicit-check-not="taking the address" \
; DEFINE:     --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:     --implicit-check-not="Stack dump" --implicit-check-not=warning:
; DEFINE: %{more} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/tm17b.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=TM17B %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/width.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=WIDTH %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/width.ll -o %t/width.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=WIDTH %{one} && \
; DEFINE:   llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/widthok.ll \
; DEFINE:     -o %t/widthok.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=WIDTHOK --input-file=%t/widthok.%{t}%{o}.il && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/inv.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=INV %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/inv.ll -o %t/inv.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=INV %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/inv2.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=INV2 %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/inv2.ll -o %t/inv2.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=INV2 %{one} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/decl.ll -o /dev/null \
; DEFINE:     2> %t/decl.%{t}%{o}.txt && \
; DEFINE:   FileCheck %s --check-prefix=DECL %{one} --input-file=%t/decl.%{t}%{o}.txt && \
; DEFINE:   count 5 < %t/decl.%{t}%{o}.txt && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/decl.ll -o %t/decl.o \
; DEFINE:     2> %t/decl.%{t}%{o}.obj.txt && \
; DEFINE:   FileCheck %s --check-prefix=DECL %{one} --input-file=%t/decl.%{t}%{o}.obj.txt && \
; DEFINE:   count 5 < %t/decl.%{t}%{o}.obj.txt && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/dlg.ll -o /dev/null \
; DEFINE:     2> %t/dlg.%{t}%{o}.txt && \
; DEFINE:   FileCheck %s --check-prefix=DLG %{one} --input-file=%t/dlg.%{t}%{o}.txt && \
; DEFINE:   count 3 < %t/dlg.%{t}%{o}.txt && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj < %t/dlg.ll -o %t/dlg.o \
; DEFINE:     2> %t/dlg.%{t}%{o}.obj.txt && \
; DEFINE:   FileCheck %s --check-prefix=DLG %{one} --input-file=%t/dlg.%{t}%{o}.obj.txt && \
; DEFINE:   count 3 < %t/dlg.%{t}%{o}.obj.txt
; DEFINE: %{ext} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/ext16.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=EXT16 %{one} --implicit-check-not="taking the address" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs -filetype=obj < %t/ext16.ll -o %t/ext16.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=EXT16 %{one} --implicit-check-not="taking the address" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/ext8.ll -o /dev/null 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=EXT8 %{one} --implicit-check-not="taking the address" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs -filetype=obj < %t/ext8.ll -o %t/ext8.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=EXT8 %{one} --implicit-check-not="taking the address" && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/extmore.ll -o /dev/null \
; DEFINE:     2> %t/extmore.%{t}%{o}.txt && \
; DEFINE:   FileCheck %s --check-prefix=EXTMORE %{one} --implicit-check-not="taking the address" \
; DEFINE:     --input-file=%t/extmore.%{t}%{o}.txt && \
; DEFINE:   count 8 < %t/extmore.%{t}%{o}.txt && \
; DEFINE:   not llc -mtriple=%{t} %{o} -verify-machineinstrs -filetype=obj < %t/extmore.ll -o %t/extmore.o \
; DEFINE:     2> %t/extmore.%{t}%{o}.obj.txt && \
; DEFINE:   FileCheck %s --check-prefix=EXTMORE %{one} --implicit-check-not="taking the address" \
; DEFINE:     --input-file=%t/extmore.%{t}%{o}.obj.txt && \
; DEFINE:   count 8 < %t/extmore.%{t}%{o}.obj.txt && \
; DEFINE:   llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/extok.ll \
; DEFINE:     -o %t/extok.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   llc -mtriple=%{t} %{o} -verify-machineinstrs -filetype=obj < %t/extok.ll \
; DEFINE:     -o %t/extok.%{t}%{o}.o 2>&1 | count 0

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}
; REDEFINE: %{o} = -O2
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}
; REDEFINE: %{o} = -O0
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}
; REDEFINE: %{o} = -O2
; RUN: %{addr}
; RUN: %{more}
; RUN: %{ext}

; Objects take the same path (P0 runs for every output form).
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj < %t/addr.ll -o %t/addr.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ADDR %{one}
; RUN: not llc -mtriple=msil64-unknown-dotnet -filetype=obj < %t/tm17.ll -o %t/tm17.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=TM17 %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj < %t/proto.ll -o %t/proto.o 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PROTO %{one} --implicit-check-not="taking the address"

; The IL taken from stdout assembles, and so does extok.ll's.
; RUN: %if ilasm %{ %ilasm-verify -output=%t/addr.lnx.dll %t/addr.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/addr.win.dll %t/addr.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/extok.win.dll %t/extok.x86_64-pc-windows-msvc-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/extok.p64.dll %t/extok.msil64-unknown-dotnet-O0.il %}

; ADDR-DAG: error: {{.*}}taking the address of the managed-call function 'f_addr' is not supported for MSIL yet{{$}}
; ADDR-DAG: error: {{.*}}taking the address of the managed-call function 'f_init' is not supported for MSIL yet{{$}}
; ADDR-DAG: error: {{.*}}taking the address of the managed-call function 'f_arg' is not supported for MSIL yet{{$}}

; ADDR-IR:      @tbl = global ptr null
; ADDR-IR-LABEL: define ptr @addr()
; ADDR-IR-NEXT:   ret ptr poison
; ADDR-IR-LABEL: define void @arg()
; ADDR-IR-NEXT:   call void @take(ptr poison)
; ADDR-IR-LABEL: define i32 @ok(i32 %x)
; ADDR-IR-NEXT:   %r = call i32 @__msil_mc.f_ok(i32 %x)

; ADDR-IL-LABEL: .method assembly static int32 '__msil_mc.f_ok'(int32) cil managed
; ADDR-IL:       call int32 [mscorlib]System.Math::Abs(int32)
; ADDR-IL-LABEL: .method public static int32 'ok'(int32) cil managed
; ADDR-IL:       call int32 '__msil_mc.f_ok'(int32)

; TM17-DAG: error: {{.*}}managed call 'd' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; TM17-DAG: error: {{.*}}managed call 'e' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; PROTO: error: {{.*}}managed call 'p' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; PROTO2: error: {{.*}}managed call 'q' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; EXT16: {{^}}error: managed call 'to16' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; EXT8: {{^}}error: managed call 'f' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; EXTMORE-DAG: {{^}}error: managed call 'x_r8' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_bool' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_es' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_en' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_eu16' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_vb' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_eb' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}
; EXTMORE-DAG: {{^}}error: managed call 'x_addr' is declared with different 'msil_method' or 'msil_catch' arguments in different translation units{{$}}

; Exactly eight lines (count 8), one per name, as for decl.ll below.

; TM17B-DAG: {{^}}error: managed call 'fx_exp' is declared with different 'dllexport', 'constructor' or 'destructor' attributes in different translation units{{$}}
; TM17B-DAG: {{^}}error: managed call 'fx_prio' is declared with different 'dllexport', 'constructor' or 'destructor' attributes in different translation units{{$}}

; WIDTH-DAG: {{^}}error: managed call 'w_gc5' has invalid "msil-managed-*" attributes: parameter 2: conversion 'enum' does not fit the .NET type 'valuetype System.GCCollectionMode' and the IR type 'i5'{{$}}
; WIDTH-DAG: {{^}}error: managed call 'w_dow40' has invalid "msil-managed-*" attributes: the result: conversion 'enum' does not fit the .NET type 'valuetype System.DayOfWeek' and the IR type 'i40'{{$}}
; WIDTH-DAG: {{^}}error: managed call 'w_max' has invalid "msil-managed-*" attributes: the result: conversion 'val' does not fit the .NET type 'int64' and the IR type 'i32'{{$}}
; WIDTH-DAG: {{^}}error: managed call 'w_abs64' has invalid "msil-managed-*" attributes: parameter 1: conversion 'val' does not fit the .NET type 'int32' and the IR type 'i64'{{$}}
; WIDTH-DAG: {{^}}error: managed call 'w_gc24' has invalid "msil-managed-*" attributes: parameter 2: conversion 'enum' does not fit the .NET type 'valuetype System.GCCollectionMode' and the IR type 'i24'{{$}}
; WIDTH-DAG: {{^}}error: managed call 'w_ctor' has invalid "msil-managed-*" attributes: the result: conversion 'enum' does not fit the .NET type 'valuetype System.DateTime' and the IR type 'i64'{{$}}

; WIDTHOK-LABEL: {{^}}.method assembly static {{.+}} '__msil_mc.w_max8'({{.+}}) cil managed{{$}}
; WIDTHOK:       {{^}}  ldarg.0{{$}}
; WIDTHOK-NEXT:  {{^}}  ldarg.1{{$}}
; WIDTHOK-NEXT:  {{^}}  call uint8 {{\[}}mscorlib]System.Math::Max(uint8, uint8){{$}}
; WIDTHOK-NEXT:  {{^}}  ldc.i4.0{{$}}
; WIDTHOK-NEXT:  {{^}}  cgt.un{{$}}
; WIDTHOK-NEXT:  {{^}}  ret{{$}}
; WIDTHOK-LABEL: {{^}}.method public static {{.+}} 'use'({{.+}}) cil managed{{$}}
; WIDTHOK:       {{^}}  call {{.+}} '__msil_mc.w_max8'({{.+}}){{$}}

; INV: {{^}}error: managed call 'm_bad' has invalid "msil-managed-*" attributes: signature: expected a type at the end of the text{{$}}

; INV2: {{^}}error: managed call 'm_bad2' has invalid "msil-managed-*" attributes: signature: expected a type at the end of the text{{$}}

; DECL-DAG: {{^}}error: managed call 'dl_sfield' has invalid "msil-managed-*" attributes: "msil-managed-decl" belongs only to a static method or to an instance method whose 'this' conversion is 'this-val' or 'this-gch'{{$}}
; DECL-DAG: {{^}}error: managed call 'dl_ifield' has invalid "msil-managed-*" attributes: "msil-managed-decl" belongs only to a static method or to an instance method whose 'this' conversion is 'this-val' or 'this-gch'{{$}}
; DECL-DAG: {{^}}error: managed call 'dl_obj' has invalid "msil-managed-*" attributes: "msil-managed-decl" belongs only to a static method or to an instance method whose 'this' conversion is 'this-val' or 'this-gch'{{$}}
; DECL-DAG: {{^}}error: managed call 'dl_names' has invalid "msil-managed-*" attributes: "msil-managed-decl" is 'System.Text.Encoding', which is neither 'System.DayOfWeek' nor System.ValueType, System.Object or System.Enum{{$}}
; DECL-DAG: {{^}}error: managed call 'dl_utf8' has invalid "msil-managed-*" attributes: "msil-managed-decl" is 'System.Text.Encoding', which is neither 'System.Text.UTF8Encoding' nor System.ValueType, System.Object or System.Enum{{$}}

; Exactly five lines (count 5): these five errors, none for dl_ok and
; nothing else, since the CHECK-DAG lines above leave the lines between and
; after their matches unchecked by --implicit-check-not.

; DLG-DAG: {{^}}error: managed call 'dg_new' has invalid "msil-managed-*" attributes: signature: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}
; DLG-DAG: {{^}}error: managed call 'dg_new_obj' has invalid "msil-managed-*" attributes: signature: a class constructor with the signature '(object, native int)', the shape of a delegate's constructor, is not supported for MSIL yet{{$}}
; DLG-DAG: {{^}}error: managed call 'dg_fp' has invalid "msil-managed-*" attributes: member: delegates over function pointers are not supported for MSIL yet{{$}}

; Exactly three lines (count 3): none for dg_href, dg_pts and dg_invoke.

;--- addr.ll
source_filename = "addr.c"

@tbl = global ptr @__msil_mc.f_init

declare i32 @__msil_mc.f_addr(i32) #0
declare i32 @__msil_mc.f_init(i32) #0
declare i32 @__msil_mc.f_arg(i32) #0
declare i32 @__msil_mc.f_ok(i32) #0

define internal void @take(ptr %p) {
  ret void
}

define ptr @addr() {
  ret ptr @__msil_mc.f_addr
}

define void @arg() {
  call void @take(ptr @__msil_mc.f_arg)
  ret void
}

define i32 @ok(i32 %x) {
  %r = call i32 @__msil_mc.f_ok(i32 %x)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1, !2, !3}
!0 = !{!"__msil_mc.f_addr", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.f_init", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!2 = !{!"__msil_mc.f_arg", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!3 = !{!"__msil_mc.f_ok", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}

;--- tm17.ll
source_filename = "tm17.c"

declare i32 @__msil_mc.d(i32) #0
declare i32 @__msil_mc.e(i32, ptr) #1
declare i32 @__msil_mc.k(i32) #0

define i32 @use(i32 %x, ptr %err) {
  %a = call i32 @__msil_mc.d(i32 %x)
  %b = call i32 @__msil_mc.e(i32 %a, ptr %err)
  %c = call i32 @__msil_mc.k(i32 %b)
  ret i32 %c
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-catch"="System.OverflowException=75" "msil-managed-conv"="val,err;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6}
!0 = !{!"__msil_mc.d", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.e", !"System.Math", !"Abs", !"int32(int32)", !"", !"val,err;val", !"System.OverflowException=75"}
!2 = !{!"__msil_mc.k", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!3 = !{!"__msil_mc.d", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}
!4 = !{!"__msil_mc.e", !"System.Math", !"Abs", !"int32(int32)", !"", !"val,err;val", !"System.OverflowException=34"}
!5 = !{!"__msil_mc.k", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!6 = !{!"__msil_mc.gone", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}

;--- same.ll
source_filename = "same.c"

declare i32 @__msil_mc.k(i32) #0

define i32 @use(i32 %x) {
  %c = call i32 @__msil_mc.k(i32 %x)
  ret i32 %c
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.k", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.k", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}

;--- proto.ll
source_filename = "ld-temp.o"

declare i32 @__msil_mc.p(i32) #0

define i32 @first(i32 %x) {
  %r = call i32 @__msil_mc.p(i32 %x)
  ret i32 %r
}

define i32 @second(i64 %x) {
  %r = call i32 @__msil_mc.p(i64 %x)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.p", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.p", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}

;--- proto2.ll
source_filename = "ld-temp.o"

declare i32 @__msil_mc.q(i32) #0

define i32 @first(i32 %x) {
  %r = call i32 @__msil_mc.q(i32 %x)
  ret i32 %r
}

define i32 @second(i32 %x, i32 %y) {
  %r = call i32 @__msil_mc.q(i32 %x, i32 %y)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.q", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.q", !"System.Math", !"Max", !"int32(int32, int32)", !"", !"val,val;val", !""}

;--- ext16.ll
source_filename = "ld-temp.o"

declare zeroext i16 @__msil_mc.to16(i32 noundef) #0

define i32 @first(i32 %x) {
  %r = call zeroext i16 @__msil_mc.to16(i32 noundef %x)
  %e = zext i16 %r to i32
  ret i32 %e
}

define i32 @second(i32 %x) {
  %r = call signext i16 @__msil_mc.to16(i32 noundef %x)
  %e = sext i16 %r to i32
  ret i32 %e
}

define i32 @third(i32 %x) {
  %r = call signext i16 @__msil_mc.to16(i32 noundef %x)
  %e = sext i16 %r to i32
  ret i32 %e
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(int32)" "msil-managed-type"="System.Convert" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.to16", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.to16", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}

;--- ext8.ll
source_filename = "ld-temp.o"

declare zeroext i8 @__msil_mc.f(i8 noundef zeroext) #0

define i32 @use_b(i32 %x) {
  %b = trunc i32 %x to i8
  %r = call zeroext i8 @__msil_mc.f(i8 noundef zeroext %b)
  %e = zext i8 %r to i32
  ret i32 %e
}

define i32 @use_a(i32 %x) {
  %b = trunc i32 %x to i8
  %r = call signext i8 @__msil_mc.f(i8 noundef zeroext %b)
  %e = sext i8 %r to i32
  ret i32 %e
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Octet" "msil-managed-sig"="uint8(uint8)" "msil-managed-type"="Contoso.Widget" }

!msil.managed.calls = !{!0, !1}
!0 = !{!"__msil_mc.f", !"Contoso.Widget", !"Octet", !"uint8(uint8)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.f", !"Contoso.Widget", !"Octet", !"uint8(uint8)", !"", !"val;val", !""}

;--- extmore.ll
source_filename = "ld-temp.o"

declare signext i8 @__msil_mc.x_r8() #0
declare i32 @__msil_mc.x_bool(i8 noundef) #1
declare void @__msil_mc.x_es(i8 noundef signext) #2
declare void @__msil_mc.x_en(i8 noundef) #2
declare void @__msil_mc.x_eu16(i16 noundef zeroext) #3
declare i32 @__msil_mc.x_vb(i1 noundef zeroext) #4
declare void @__msil_mc.x_eb(i1 noundef zeroext) #6
declare zeroext i16 @__msil_mc.x_addr(i32 noundef) #5

; The first unit's calls, with the declarations' attributes.
define i32 @first(i8 %c, i16 %s, i1 %b, i32 %x) {
  %r8 = call signext i8 @__msil_mc.x_r8()
  %rb = call i32 @__msil_mc.x_bool(i8 noundef %c)
  call void @__msil_mc.x_es(i8 noundef signext %c)
  call void @__msil_mc.x_en(i8 noundef %c)
  call void @__msil_mc.x_eu16(i16 noundef zeroext %s)
  %rv = call i32 @__msil_mc.x_vb(i1 noundef zeroext %b)
  call void @__msil_mc.x_eb(i1 noundef zeroext %b)
  %ra = call zeroext i16 @__msil_mc.x_addr(i32 noundef %x)
  ret i32 %rv
}

; The second unit's calls, each with another extension.
define i32 @second(i8 %c, i16 %s, i1 %b, i32 %x) {
  %r8 = call zeroext i8 @__msil_mc.x_r8()
  %rb = call i32 @__msil_mc.x_bool(i8 noundef zeroext %c)
  call void @__msil_mc.x_es(i8 noundef zeroext %c)
  call void @__msil_mc.x_en(i8 noundef zeroext %c)
  call void @__msil_mc.x_eu16(i16 noundef signext %s)
  %rv = call i32 @__msil_mc.x_vb(i1 noundef signext %b)
  call void @__msil_mc.x_eb(i1 noundef signext %b)
  %ra = call signext i16 @__msil_mc.x_addr(i32 noundef %x)
  ret i32 %rv
}

define ptr @addr() {
  ret ptr @__msil_mc.x_addr
}

attributes #0 = { "msil-managed-conv"=";enum" "msil-managed-member"="Get8" "msil-managed-sig"="valuetype Ns.Mode8()" "msil-managed-type"="Ns.Narrow" }
attributes #1 = { "msil-managed-conv"="bool;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(bool)" "msil-managed-type"="System.Convert" }
attributes #2 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Set8" "msil-managed-sig"="void(valuetype Ns.Mode8)" "msil-managed-type"="Ns.Narrow" }
attributes #3 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU16" "msil-managed-sig"="void(valuetype Ns.ModeU16)" "msil-managed-type"="Ns.Narrow" }
attributes #4 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(uint8)" "msil-managed-type"="System.Convert" }
attributes #5 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(int32)" "msil-managed-type"="System.Convert" }
attributes #6 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU8" "msil-managed-sig"="void(valuetype Ns.ModeU8)" "msil-managed-type"="Ns.Narrow" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7}
!0 = !{!"__msil_mc.x_r8", !"Ns.Narrow", !"Get8", !"valuetype Ns.Mode8()", !"", !";enum", !""}
!1 = !{!"__msil_mc.x_bool", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!2 = !{!"__msil_mc.x_es", !"Ns.Narrow", !"Set8", !"void(valuetype Ns.Mode8)", !"", !"enum;void", !""}
!3 = !{!"__msil_mc.x_en", !"Ns.Narrow", !"Set8", !"void(valuetype Ns.Mode8)", !"", !"enum;void", !""}
!4 = !{!"__msil_mc.x_eu16", !"Ns.Narrow", !"SetU16", !"void(valuetype Ns.ModeU16)", !"", !"enum;void", !""}
!5 = !{!"__msil_mc.x_vb", !"System.Convert", !"ToInt32", !"int32(uint8)", !"", !"val;val", !""}
!6 = !{!"__msil_mc.x_addr", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!7 = !{!"__msil_mc.x_eb", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}

;--- extok.ll
source_filename = "ld-temp.o"

declare zeroext i16 @__msil_mc.k_both(i32 noundef) #0
declare zeroext i8 @__msil_mc.k_p8(i8 noundef zeroext) #1
declare i32 @__msil_mc.k_p16(i16 noundef signext) #2
declare zeroext i16 @__msil_mc.k_none(i32 noundef) #0
declare void @__msil_mc.k_nonep(i8 noundef zeroext) #3
declare zeroext i8 @__msil_mc.k_bres(i16 noundef zeroext) #4
declare i32 @__msil_mc.k_bpar(i8 noundef signext) #5
declare void @__msil_mc.k_enum(i8 noundef) #6
declare zeroext i1 @__msil_mc.k_i1r() #7
declare i32 @__msil_mc.k_vb(i1 noundef) #8
declare void @__msil_mc.k_eb(i1 noundef) #3
declare i32 @__msil_mc.k_i32(i32 noundef) #9

; The first unit's calls, with the declarations' attributes.
define i32 @first(i8 %c, i16 %s, i1 %b, i32 %x) {
  %r1 = call zeroext i16 @__msil_mc.k_both(i32 noundef %x)
  %r2 = call zeroext i8 @__msil_mc.k_p8(i8 noundef zeroext %c)
  %r3 = call i32 @__msil_mc.k_p16(i16 noundef signext %s)
  %r4 = call zeroext i16 @__msil_mc.k_none(i32 noundef %x)
  call void @__msil_mc.k_nonep(i8 noundef zeroext %c)
  %r5 = call zeroext i8 @__msil_mc.k_bres(i16 noundef zeroext %s)
  %r6 = call i32 @__msil_mc.k_bpar(i8 noundef signext %c)
  call void @__msil_mc.k_enum(i8 noundef %c)
  %r7 = call zeroext i1 @__msil_mc.k_i1r()
  %r8 = call i32 @__msil_mc.k_vb(i1 noundef %b)
  call void @__msil_mc.k_eb(i1 noundef %b)
  %r9 = call i32 @__msil_mc.k_i32(i32 noundef %x)
  ret i32 %r9
}

; The second unit's calls: another extension, or none, where it gives the
; declaration no other stub.
define i32 @second(i8 %c, i16 %s, i1 %b, i32 %x) {
  %r1 = call zeroext i16 @__msil_mc.k_both(i32 noundef %x)
  %r2 = call zeroext i8 @__msil_mc.k_p8(i8 noundef signext %c)
  %r3 = call i32 @__msil_mc.k_p16(i16 noundef zeroext %s)
  %r4 = call i16 @__msil_mc.k_none(i32 noundef %x)
  call void @__msil_mc.k_nonep(i8 noundef %c)
  %r5 = call signext i8 @__msil_mc.k_bres(i16 noundef zeroext %s)
  %r6 = call i32 @__msil_mc.k_bpar(i8 noundef zeroext %c)
  call void @__msil_mc.k_enum(i8 noundef signext %c)
  %r7 = call signext i1 @__msil_mc.k_i1r()
  %r8 = call i32 @__msil_mc.k_vb(i1 noundef signext %b)
  call void @__msil_mc.k_eb(i1 noundef signext %b)
  %r9 = call signext i32 @__msil_mc.k_i32(i32 noundef zeroext %x)
  ret i32 %r9
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt16" "msil-managed-sig"="int16(int32)" "msil-managed-type"="System.Convert" }
attributes #1 = { "msil-managed-conv"="val;val" "msil-managed-member"="Octet" "msil-managed-sig"="uint8(uint8)" "msil-managed-type"="Contoso.Widget" }
attributes #2 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(int16)" "msil-managed-type"="System.Convert" }
attributes #3 = { "msil-managed-conv"="enum;void" "msil-managed-member"="SetU8" "msil-managed-sig"="void(valuetype Ns.ModeU8)" "msil-managed-type"="Ns.Narrow" }
attributes #4 = { "msil-managed-conv"="val;bool" "msil-managed-member"="IsDigit" "msil-managed-sig"="bool(char)" "msil-managed-type"="System.Char" }
attributes #5 = { "msil-managed-conv"="bool;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(bool)" "msil-managed-type"="System.Convert" }
attributes #6 = { "msil-managed-conv"="enum;void" "msil-managed-member"="Set8" "msil-managed-sig"="void(valuetype Ns.Mode8)" "msil-managed-type"="Ns.Narrow" }
attributes #7 = { "msil-managed-conv"=";enum" "msil-managed-member"="GetU8" "msil-managed-sig"="valuetype Ns.ModeU8()" "msil-managed-type"="Ns.Narrow" }
attributes #8 = { "msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(uint8)" "msil-managed-type"="System.Convert" }
attributes #9 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11}
!0 = !{!"__msil_mc.k_both", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.k_p8", !"Contoso.Widget", !"Octet", !"uint8(uint8)", !"", !"val;val", !""}
!2 = !{!"__msil_mc.k_p16", !"System.Convert", !"ToInt32", !"int32(int16)", !"", !"val;val", !""}
!3 = !{!"__msil_mc.k_none", !"System.Convert", !"ToInt16", !"int16(int32)", !"", !"val;val", !""}
!4 = !{!"__msil_mc.k_nonep", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}
!5 = !{!"__msil_mc.k_bres", !"System.Char", !"IsDigit", !"bool(char)", !"", !"val;bool", !""}
!6 = !{!"__msil_mc.k_bpar", !"System.Convert", !"ToInt32", !"int32(bool)", !"", !"bool;val", !""}
!7 = !{!"__msil_mc.k_enum", !"Ns.Narrow", !"Set8", !"void(valuetype Ns.Mode8)", !"", !"enum;void", !""}
!8 = !{!"__msil_mc.k_i1r", !"Ns.Narrow", !"GetU8", !"valuetype Ns.ModeU8()", !"", !";enum", !""}
!9 = !{!"__msil_mc.k_vb", !"System.Convert", !"ToInt32", !"int32(uint8)", !"", !"val;val", !""}
!10 = !{!"__msil_mc.k_i32", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!11 = !{!"__msil_mc.k_eb", !"Ns.Narrow", !"SetU8", !"void(valuetype Ns.ModeU8)", !"", !"enum;void", !""}

;--- tm17b.ll
source_filename = "ld-temp.o"

declare i32 @__msil_mc.fx_exp(i32) #0
declare void @__msil_mc.fx_prio() #1

define i32 @use(i32 %x) {
  call void @__msil_mc.fx_prio()
  %r = call i32 @__msil_mc.fx_exp(i32 %x)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-conv"=";void" "msil-managed-member"="Collect" "msil-managed-sig"="void()" "msil-managed-type"="System.GC" }

!msil.managed.calls = !{!0, !1, !2, !3}
!0 = !{!"__msil_mc.fx_exp", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!1 = !{!"__msil_mc.fx_exp", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !"", !"dllexport"}
!2 = !{!"__msil_mc.fx_prio", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(65535)"}
!3 = !{!"__msil_mc.fx_prio", !"System.GC", !"Collect", !"void()", !"", !";void", !"", !"constructor(101)"}

;--- width.ll
source_filename = "width.c"

declare void @__msil_mc.w_gc5(i32, i5) #0
declare i40 @__msil_mc.w_dow40(i32) #1
declare i32 @__msil_mc.w_max(i64 noundef, i64 noundef) #2
declare i64 @__msil_mc.w_abs64(i64) #3
declare void @__msil_mc.w_gc24(i32, i24) #0
declare i64 @__msil_mc.w_ctor(i64) #4

define i32 @use(i32 %x, i64 %y) {
  call void @__msil_mc.w_gc5(i32 %x, i5 3)
  %d = call i40 @__msil_mc.w_dow40(i32 %x)
  %m = call i32 @__msil_mc.w_max(i64 noundef %y, i64 noundef %y)
  %a = call i64 @__msil_mc.w_abs64(i64 %y)
  call void @__msil_mc.w_gc24(i32 %x, i24 2)
  %c = call i64 @__msil_mc.w_ctor(i64 %y)
  ret i32 %x
}

attributes #0 = { "msil-managed-conv"="val,enum;void" "msil-managed-member"="Collect" "msil-managed-sig"="void(int32, valuetype System.GCCollectionMode)" "msil-managed-type"="System.GC" }
attributes #1 = { "msil-managed-conv"="val;enum" "msil-managed-member"="ToDay" "msil-managed-sig"="valuetype System.DayOfWeek(int32)" "msil-managed-type"="Ns.Days" }
attributes #2 = { "msil-managed-conv"="val,val;val" "msil-managed-member"="Max" "msil-managed-sig"="int64(int64, int64)" "msil-managed-type"="System.Math" }
attributes #3 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #4 = { "msil-managed-conv"="val;enum" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5}
!0 = !{!"__msil_mc.w_gc5", !"System.GC", !"Collect", !"void(int32, valuetype System.GCCollectionMode)", !"", !"val,enum;void", !""}
!1 = !{!"__msil_mc.w_dow40", !"Ns.Days", !"ToDay", !"valuetype System.DayOfWeek(int32)", !"", !"val;enum", !""}
!2 = !{!"__msil_mc.w_max", !"System.Math", !"Max", !"int64(int64, int64)", !"", !"val,val;val", !""}
!3 = !{!"__msil_mc.w_abs64", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!4 = !{!"__msil_mc.w_gc24", !"System.GC", !"Collect", !"void(int32, valuetype System.GCCollectionMode)", !"", !"val,enum;void", !""}
!5 = !{!"__msil_mc.w_ctor", !"valuetype System.DateTime", !".ctor", !"instance void(int64)", !"", !"val;enum", !""}

;--- widthok.ll
source_filename = "widthok.c"

declare zeroext i1 @__msil_mc.w_max8(i1 noundef zeroext, i1 noundef zeroext) #0

define zeroext i1 @use(i1 zeroext %a, i1 zeroext %b) {
  %r = call zeroext i1 @__msil_mc.w_max8(i1 noundef zeroext %a, i1 noundef zeroext %b)
  ret i1 %r
}

attributes #0 = { "msil-managed-conv"="val,val;val" "msil-managed-member"="Max" "msil-managed-sig"="uint8(uint8, uint8)" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.w_max8", !"System.Math", !"Max", !"uint8(uint8, uint8)", !"", !"val,val;val", !""}

;--- inv.ll
source_filename = "inv.c"

declare i32 @__msil_mc.m_bad(i32 noundef) #0

define i32 @use(i32 %x) {
  %r = call i32 @__msil_mc.m_bad(i32 noundef %x)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(" "msil-managed-type"="System.Math" }

!msil.managed.calls = !{!0}
!0 = !{!"__msil_mc.m_bad", !"System.Math", !"Abs", !"int32(", !"", !"val;val", !""}

;--- inv2.ll
source_filename = "inv2.c"

define internal i32 @__msil_mc.m_bad2(i32 noundef %x) #0 {
  unreachable
}

define i32 @use(i32 %x) {
  %r = call i32 @__msil_mc.m_bad2(i32 noundef %x)
  ret i32 %r
}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(" "msil-managed-stub" "msil-managed-type"="System.Math" }

;--- decl.ll
source_filename = "decl.c"

declare ptr @__msil_mc.dl_sfield() #0
declare i64 @__msil_mc.dl_ifield(ptr noundef) #1
declare i32 @__msil_mc.dl_obj(ptr noundef) #2
declare ptr @__msil_mc.dl_names(ptr noundef) #3
declare ptr @__msil_mc.dl_utf8() #4
declare zeroext i1 @__msil_mc.dl_ok(ptr noundef, ptr noundef) #5

define i32 @use(ptr %h) {
  %a = call ptr @__msil_mc.dl_sfield()
  %b = call i64 @__msil_mc.dl_ifield(ptr noundef %h)
  %c = call i32 @__msil_mc.dl_obj(ptr noundef %h)
  %d = call ptr @__msil_mc.dl_names(ptr noundef %h)
  %e = call ptr @__msil_mc.dl_utf8()
  %f = call zeroext i1 @__msil_mc.dl_ok(ptr noundef %h, ptr noundef %h)
  ret i32 %c
}

attributes #0 = { "msil-managed-conv"=";boxed" "msil-managed-decl"="System.DateTime" "msil-managed-member"="MaxValue" "msil-managed-sig"="field valuetype System.DateTime" "msil-managed-type"="valuetype System.DateTime" }
attributes #1 = { "msil-managed-conv"="this-val;val" "msil-managed-decl"="Ns.Stamp" "msil-managed-member"="Ticks" "msil-managed-sig"="instance field int64" "msil-managed-type"="valuetype Ns.Stamp" }
attributes #2 = { "msil-managed-conv"="this-obj;val" "msil-managed-decl"="System.Array" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.Array" }
attributes #3 = { "msil-managed-conv"="obj;obj" "msil-managed-decl"="System.Text.Encoding" "msil-managed-member"="GetNames" "msil-managed-sig"="string[](class System.Type)" "msil-managed-type"="valuetype System.DayOfWeek" }
attributes #4 = { "msil-managed-conv"=";obj" "msil-managed-decl"="System.Text.Encoding" "msil-managed-member"="get_UTF8" "msil-managed-sig"="class System.Text.Encoding()" "msil-managed-type"="System.Text.UTF8Encoding" }
attributes #5 = { "msil-managed-conv"="obj,obj;bool" "msil-managed-decl"="System.Object" "msil-managed-member"="ReferenceEquals" "msil-managed-sig"="bool(object, object)" "msil-managed-type"="valuetype System.Guid" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5}
!0 = !{!"__msil_mc.dl_sfield", !"valuetype System.DateTime", !"MaxValue", !"field valuetype System.DateTime", !"", !";boxed", !""}
!1 = !{!"__msil_mc.dl_ifield", !"valuetype Ns.Stamp", !"Ticks", !"instance field int64", !"", !"this-val;val", !""}
!2 = !{!"__msil_mc.dl_obj", !"System.Array", !"get_Length", !"instance int32()", !"", !"this-obj;val", !""}
!3 = !{!"__msil_mc.dl_names", !"valuetype System.DayOfWeek", !"GetNames", !"string[](class System.Type)", !"", !"obj;obj", !""}
!4 = !{!"__msil_mc.dl_utf8", !"System.Text.UTF8Encoding", !"get_UTF8", !"class System.Text.Encoding()", !"", !";obj", !""}
!5 = !{!"__msil_mc.dl_ok", !"valuetype System.Guid", !"ReferenceEquals", !"bool(object, object)", !"", !"obj,obj;bool", !""}

;--- dlg.ll
source_filename = "dlg.c"

declare ptr @__msil_mc.dg_new(ptr noundef, i64 noundef) #0
declare ptr @__msil_mc.dg_new_obj(ptr noundef, i64 noundef) #1
declare ptr @__msil_mc.dg_fp(i64 noundef, ptr noundef) #2
declare ptr @__msil_mc.dg_href(ptr noundef, i64 noundef) #3
declare ptr @__msil_mc.dg_pts(i64 noundef, ptr noundef) #4
declare void @__msil_mc.dg_invoke(ptr noundef) #5

define i32 @use(ptr %h, i64 %f) {
  %a = call ptr @__msil_mc.dg_new(ptr noundef null, i64 noundef %f)
  %b = call ptr @__msil_mc.dg_new_obj(ptr noundef null, i64 noundef %f)
  %c = call ptr @__msil_mc.dg_fp(i64 noundef %f, ptr noundef %h)
  %d = call ptr @__msil_mc.dg_href(ptr noundef %h, i64 noundef %f)
  %e = call ptr @__msil_mc.dg_pts(i64 noundef %f, ptr noundef %h)
  call void @__msil_mc.dg_invoke(ptr noundef %a)
  ret i32 0
}

attributes #0 = { "msil-managed-conv"="obj,val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(object, native int)" "msil-managed-type"="System.Action" }
attributes #1 = { "msil-managed-conv"="obj,val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(class System.Object, native int)" "msil-managed-type"="System.Action" }
attributes #2 = { "msil-managed-conv"="val,obj;obj" "msil-managed-member"="GetDelegateForFunctionPointer" "msil-managed-sig"="class System.Delegate(native int, class System.Type)" "msil-managed-type"="System.Runtime.InteropServices.Marshal" }
attributes #3 = { "msil-managed-conv"="obj,val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(object, native int)" "msil-managed-type"="valuetype System.Runtime.InteropServices.HandleRef" }
attributes #4 = { "msil-managed-conv"="val,obj;obj" "msil-managed-member"="PtrToStructure" "msil-managed-sig"="object(native int, class System.Type)" "msil-managed-type"="System.Runtime.InteropServices.Marshal" }
attributes #5 = { "msil-managed-conv"="this-obj;void" "msil-managed-member"="Invoke" "msil-managed-sig"="instance void()" "msil-managed-type"="System.Action" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5}
!0 = !{!"__msil_mc.dg_new", !"System.Action", !".ctor", !"instance void(object, native int)", !"", !"obj,val;obj", !""}
!1 = !{!"__msil_mc.dg_new_obj", !"System.Action", !".ctor", !"instance void(class System.Object, native int)", !"", !"obj,val;obj", !""}
!2 = !{!"__msil_mc.dg_fp", !"System.Runtime.InteropServices.Marshal", !"GetDelegateForFunctionPointer", !"class System.Delegate(native int, class System.Type)", !"", !"val,obj;obj", !""}
!3 = !{!"__msil_mc.dg_href", !"valuetype System.Runtime.InteropServices.HandleRef", !".ctor", !"instance void(object, native int)", !"", !"obj,val;boxed", !""}
!4 = !{!"__msil_mc.dg_pts", !"System.Runtime.InteropServices.Marshal", !"PtrToStructure", !"object(native int, class System.Type)", !"", !"val,obj;obj", !""}
!5 = !{!"__msil_mc.dg_invoke", !"System.Action", !"Invoke", !"instance void()", !"", !"this-obj;void", !""}
