;; Check that MSIL methods are found by their one-line '.method' header and
;; '} // end of method' footer, and that their labels anchor on '.method'.
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil | FileCheck %s

;; Check -march=msil with a native triple, and output without asm comments.
; RUN: llc < %s -march=msil -mtriple=x86_64-unknown-linux-gnu -asm-verbose=false | FileCheck %s --check-prefix=MARCH

;; Check that the checks of a non-MSIL RUN line keep their plain labels and
;; come first, separated from the MSIL ones by an empty comment line.
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu | FileCheck %s --check-prefix=X64

;; 'add' is called before it is defined, and the call prints "'add'(" too.
define i32 @caller(i32 %x) {
  %r = call i32 @add(i32 %x, i32 7)
  %s = call i64 @add.i64(i64 1, i64 2)
  %t = trunc i64 %s to i32
  %u = add i32 %r, %t
  ret i32 %u
}

define i32 @add(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

define internal i64 @add.i64(i64 %a, i64 %b) {
  %r = add i64 %a, %b
  ret i64 %r
}

define i32 @"sel$ect"(i32 %c, i32 %a, i32 %b) {
  %cc = icmp ne i32 %c, 0
  %r = select i1 %cc, i32 %a, i32 %b
  ret i32 %r
}

define void @nothing() {
  ret void
}

;; The entry point '<msil>Main' that llc adds for main is not an IR function.
define i32 @main() {
  %r = call i32 @caller(i32 35)
  ret i32 %r
}
