; Every P/Invoke declaration prints 'nomangle cdecl' (ImplementationPlan.md
; M18.a (7); plan 5.10.11 and the section 12 row of 2026-10-21 on D14's text):
; pinvokeimpl("<module>" [as "<entry>"] nomangle cdecl), which ilasm writes
; as ImplMap MappingFlags 0x0201 (NoMangle 0x0001 | CallConvCdecl 0x0200), so
; the runtime binds the export name exactly and never probes for A/W
; variants. The Phase-1 import table itself is unchanged (D14, frozen by plan
; 5.10.13): the C library of each triple's runtime (linux-gnu and
; linux-musl "libc", windows-msvc "ucrtbase", windows-gnu "msvcrt"), the math
; library ("libm.so.6" on linux-gnu, the C library elsewhere), and a
; function with msil-import-module ('imp2'; with msil-import-name the 'as
; "<entry>"' form, 'imp'). The stubs keep Phase 1's 'public hidebysig static
; ... cil managed preservesig' (MethodDef flags 0x2096, ImplFlags 0x0080).
; Each output assembles with ilasm, and llvm-cil-dump --tables shows the
; ImplMap rows, each bound to its stub and its ModuleRef.

; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{check} = FileCheck %s --implicit-check-not=pinvokeimpl --implicit-check-not='" cdecl)'

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.gnu.il
; RUN: %{check} -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.gnu.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-musl-msil < %s -o %t.musl.il
; RUN: %{check} -DCLIB=libc -DMLIB=libc --input-file=%t.musl.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.msvc.il
; RUN: %{check} -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.msvc.il
; RUN: %{llc} -mtriple=x86_64-w64-windows-gnu-msil < %s -o %t.mingw.il
; RUN: %{check} -DCLIB=msvcrt -DMLIB=msvcrt --input-file=%t.mingw.il

; RUN: %if ilasm && llvm-cil-dump %{ %ilasm-verify -output=%t.gnu.dll %t.gnu.il && llvm-cil-dump --tables %t.gnu.dll \
; RUN:   | FileCheck %s --check-prefixes=TABLES,TWOLIB -DCLIB=libc %}
; RUN: %if ilasm && llvm-cil-dump %{ %ilasm-verify -output=%t.musl.dll %t.musl.il && llvm-cil-dump --tables %t.musl.dll \
; RUN:   | FileCheck %s --check-prefixes=TABLES,ONELIB -DCLIB=libc %}
; RUN: %if ilasm && llvm-cil-dump %{ %ilasm-verify -output=%t.msvc.dll %t.msvc.il && llvm-cil-dump --tables %t.msvc.dll \
; RUN:   | FileCheck %s --check-prefixes=TABLES,ONELIB -DCLIB=ucrtbase %}
; RUN: %if ilasm && llvm-cil-dump %{ %ilasm-verify -output=%t.mingw.dll %t.mingw.il && llvm-cil-dump --tables %t.mingw.dll \
; RUN:   | FileCheck %s --check-prefixes=TABLES,ONELIB -DCLIB=msvcrt %}

declare i32 @puts(ptr)
declare i32 @putchar(i32)
declare double @sqrt(double)
declare i32 @imp(i32) "msil-import-module"="mylib" "msil-import-name"="entry"
declare i32 @imp2(i32) "msil-import-module"="mylib"

; CHECK-LABEL: .method public static int32 'use'(native int, float64) cil managed
; CHECK:       call int32 'puts'(native int)
; CHECK:       call int32 'putchar'(int32)
; CHECK:       call float64 'sqrt'(float64)
; CHECK:       call int32 'imp'(int32)
; CHECK:       call int32 'imp2'(int32)
; CHECK:       } // end of method 'use'
define i32 @use(ptr %s, double %x) {
  %a = call i32 @puts(ptr %s)
  %b = call i32 @putchar(i32 65)
  %r = call double @sqrt(double %x)
  %c = fptosi double %r to i32
  %d = call i32 @imp(i32 %c)
  %e = call i32 @imp2(i32 %d)
  %s1 = add i32 %a, %b
  %s2 = add i32 %s1, %e
  ret i32 %s2
}

; CHECK-DAG: .method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}
; CHECK-DAG: .method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'putchar'(int32) cil managed preservesig {}
; CHECK-DAG: .method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}
; CHECK-DAG: .method public hidebysig static pinvokeimpl("mylib" as "entry" nomangle cdecl) int32 'imp'(int32) cil managed preservesig {}
; CHECK-DAG: .method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'imp2'(int32) cil managed preservesig {}

; TABLES-LABEL: Table: MethodDef rows=6 rowsize={{[0-9]+}}
; TABLES-DAG:   MethodDef{{\[}}[[PUTS:[0-9]+]]{{\]}}: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='puts' Signature=00 01 08 18 ParamList=
; TABLES-DAG:   MethodDef{{\[}}[[PUTCHAR:[0-9]+]]{{\]}}: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='putchar' Signature=00 01 08 08 ParamList=
; TABLES-DAG:   MethodDef{{\[}}[[SQRT:[0-9]+]]{{\]}}: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='sqrt' Signature=00 01 0D 0D ParamList=
; TABLES-DAG:   MethodDef{{\[}}[[IMP:[0-9]+]]{{\]}}: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='imp' Signature=00 01 08 08 ParamList=
; TABLES-DAG:   MethodDef{{\[}}[[IMP2:[0-9]+]]{{\]}}: RVA=0x00000000 ImplFlags=0x0080 Flags=0x2096 Name='imp2' Signature=00 01 08 08 ParamList=
; TABLES-LABEL: Table: ModuleRef rows=
; ONELIB-SAME:  2 rowsize=
; TWOLIB-SAME:  3 rowsize=
; TABLES-DAG:   ModuleRef{{\[}}[[CROW:[0-9]+]]{{\]}}: Name='[[CLIB]]'
; TABLES-DAG:   ModuleRef{{\[}}[[UROW:[0-9]+]]{{\]}}: Name='mylib'
; TWOLIB-DAG:   ModuleRef{{\[}}[[MROW:[0-9]+]]{{\]}}: Name='libm.so.6'
; TABLES-LABEL: Table: ImplMap rows=5 rowsize={{[0-9]+}}
; TABLES-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[PUTS]] ImportName='puts' ImportScope=0x1A00000[[CROW]]
; TABLES-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[PUTCHAR]] ImportName='putchar' ImportScope=0x1A00000[[CROW]]
; ONELIB-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[SQRT]] ImportName='sqrt' ImportScope=0x1A00000[[CROW]]
; TWOLIB-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[SQRT]] ImportName='sqrt' ImportScope=0x1A00000[[MROW]]
; TABLES-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[IMP]] ImportName='entry' ImportScope=0x1A00000[[UROW]]
; TABLES-DAG:   MappingFlags=0x0201 MemberForwarded=0x0600000[[IMP2]] ImportName='imp2' ImportScope=0x1A00000[[UROW]]
