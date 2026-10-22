; The bitcode symbol table (irsymtab) of an MSIL module carries its
; '!llvm.linker.options' (ImplementationPlan.md 5.10.6, M13): IRSymtab.cpp
; collects them for 'isOSBinFormatCOFF() || isOSBinFormatMSIL()', so
; lto::InputFile::getCOFFLinkerOpts() gives msil-ld the library requests of
; '#pragma comment(lib)' for MSIL bitcode and archive members (clang emits
; them in the native spelling: '-l<name>' on Linux triples,
; '/DEFAULTLIB:<name>.lib' on Windows triples; pragma-comment-lib.c in
; clang/test/CodeGen/MSIL). The collected string lives in the STRTAB block that
; the module and its symbol table share (the module's own metadata strings
; are in the METADATA block), so each option is checked there, in the dump of
; llvm-bcanalyzer -show-binary-blobs. llvm-lto2 dump-symtab prints the linker
; options only for COFF triples, so it cannot show them here.
;
; Only that collection is shared with MSIL: the per-symbol flags of
; emitLinkerFlagsForGlobalCOFF stay with COFF triples (5.10.6). For a dllexport
; definition it adds ' /EXPORT:<name>' on windows-msvc and ' -export:<name>'
; on the other COFF triples, and for a hidden definition on windows-gnu
; ' -exclude-symbols:<name>'. Every MSIL module has a dllexport definition
; (msil_exported) and the windows-gnu one also a hidden definition
; (msil_hidden); their runs reject all three spellings anywhere in the dump
; (%{no_coff_flags}). Their checks match the blob piece by piece, without a
; '{{.*}}' that could swallow a stray flag, because FileCheck applies
; --implicit-check-not only outside the text that the checks match. The
; native windows-msvc and windows-gnu modules, with the same definitions, are
; the positive controls: they must carry the flags.
;
; The native x86_64 Linux module is the control of the collection: ELF
; modules keep their '!llvm.linker.options' out of the symbol table, and its
; STRTAB block names neither option. Native windows-msvc is also the positive
; control of the upstream COFF collection. Each module has a defined and an
; undefined function, so the STRTAB blob is not empty in any run. Unlike the
; llc tests (section 2 of the plan), every module has a 'target datalayout',
; the native triple's (which is the MSIL triple's, byte for byte): without one
; the bitcode writer writes no symbol table at all ('input module has no
; datalayout'), and every run would fail at its SYMTAB_BLOCK line.

; DEFINE: %{no_coff_flags} = --implicit-check-not=/EXPORT: \
; DEFINE:   --implicit-check-not=-export: --implicit-check-not=-exclude-symbols:

; RUN: split-file %s %t
; RUN: llvm-as %t/lnx.ll -o %t/lnx.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/lnx.bc \
; RUN:   | FileCheck %s --check-prefix=LNX %{no_coff_flags}
; RUN: llvm-as %t/win.ll -o %t/win.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/win.bc \
; RUN:   | FileCheck %s --check-prefix=WIN %{no_coff_flags}
; RUN: llvm-as %t/mingw.ll -o %t/mingw.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/mingw.bc \
; RUN:   | FileCheck %s --check-prefix=WIN %{no_coff_flags}
; RUN: llvm-as %t/native-elf.ll -o %t/native-elf.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/native-elf.bc | FileCheck %s --check-prefix=ELF
; RUN: llvm-as %t/native-coff.ll -o %t/native-coff.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/native-coff.bc | FileCheck %s --check-prefix=COFF
; RUN: llvm-as %t/native-mingw.ll -o %t/native-mingw.bc
; RUN: llvm-bcanalyzer -dump -show-binary-blobs %t/native-mingw.bc | FileCheck %s --check-prefix=MINGW

; LNX:      <SYMTAB_BLOCK
; LNX:      <STRTAB_BLOCK
; LNX-NEXT:   <BLOB abbrevid={{[0-9]+}}/> blob data = '
; LNX-SAME:   -lws2_32
; LNX-SAME:   -lmsil_extra
; LNX-NEXT: </STRTAB_BLOCK>

; WIN:      <SYMTAB_BLOCK
; WIN:      <STRTAB_BLOCK
; WIN-NEXT:   <BLOB abbrevid={{[0-9]+}}/> blob data = '
; WIN-SAME:   /DEFAULTLIB:ws2_32.lib
; WIN-SAME:   /include:msil_sym
; WIN-NEXT: </STRTAB_BLOCK>

; ELF:      <SYMTAB_BLOCK
; ELF:      <STRTAB_BLOCK
; ELF-NEXT:   <BLOB abbrevid={{[0-9]+}}/> blob data = '
; ELF-NOT:    lws2_32
; ELF-NOT:    lmsil_extra
; ELF:      </STRTAB_BLOCK>

; COFF:      <SYMTAB_BLOCK
; COFF:      <STRTAB_BLOCK
; COFF-NEXT:   <BLOB abbrevid={{[0-9]+}}/> blob data = '
; COFF-SAME:   /DEFAULTLIB:ws2_32.lib
; COFF-SAME:   /include:msil_sym
; COFF-SAME:   /EXPORT:msil_exported
; COFF-NEXT: </STRTAB_BLOCK>

; MINGW:      <SYMTAB_BLOCK
; MINGW:      <STRTAB_BLOCK
; MINGW-NEXT:   <BLOB abbrevid={{[0-9]+}}/> blob data = '
; MINGW-SAME:   /DEFAULTLIB:ws2_32.lib
; MINGW-SAME:   /include:msil_sym
; MINGW-SAME:   -export:msil_exported
; MINGW-SAME:   -exclude-symbols:msil_hidden
; MINGW-NEXT: </STRTAB_BLOCK>

;--- lnx.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu-msil"
source_filename = "lnx.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

define dllexport void @msil_exported() {
  ret void
}

!llvm.linker.options = !{!0, !1}
!0 = !{!"-lws2_32"}
!1 = !{!"-lmsil_extra"}

;--- win.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc-msil"
source_filename = "win.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

define dllexport void @msil_exported() {
  ret void
}

!llvm.linker.options = !{!0, !1}
!0 = !{!"/DEFAULTLIB:ws2_32.lib"}
!1 = !{!"/include:msil_sym"}

;--- mingw.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-w64-windows-gnu-msil"
source_filename = "mingw.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

define dllexport void @msil_exported() {
  ret void
}

define hidden void @msil_hidden() {
  ret void
}

!llvm.linker.options = !{!0, !1}
!0 = !{!"/DEFAULTLIB:ws2_32.lib"}
!1 = !{!"/include:msil_sym"}

;--- native-elf.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
source_filename = "native-elf.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

!llvm.linker.options = !{!0, !1}
!0 = !{!"-lws2_32"}
!1 = !{!"-lmsil_extra"}

;--- native-coff.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"
source_filename = "native-coff.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

define dllexport void @msil_exported() {
  ret void
}

!llvm.linker.options = !{!0, !1}
!0 = !{!"/DEFAULTLIB:ws2_32.lib"}
!1 = !{!"/include:msil_sym"}

;--- native-mingw.ll
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-w64-windows-gnu"
source_filename = "native-mingw.c"

define void @msil_defined() {
  call void @msil_undefined()
  ret void
}

declare void @msil_undefined()

define dllexport void @msil_exported() {
  ret void
}

define hidden void @msil_hidden() {
  ret void
}

!llvm.linker.options = !{!0, !1}
!0 = !{!"/DEFAULTLIB:ws2_32.lib"}
!1 = !{!"/include:msil_sym"}
