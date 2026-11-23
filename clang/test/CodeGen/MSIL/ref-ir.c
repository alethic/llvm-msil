// The IR of .NET references (ImplementationPlan.md 5.21.5 RG1-RG5, RG8, RG12,
// 5.21.6 RI1-RI6 and 5.21.14 RV10, milestone R-a), at -O0 on the eight host
// triples and msil64-unknown-dotnet, and at -O2 and -Og on the x86_64 Windows
// and Linux triples:
// - RG1: a reference is target("msil.ref"); -O0 locals and parameters are
//   allocas of it.
// - RS2, RG2, RG3: NULL is 'zeroinitializer', stored at every declaration
//   and for every null form in an initializer, an assignment, an argument and
//   a return. A declaration with an initializer stores NULL before its
//   initializer runs (RS2 and RG3 as amended by section 12's row of
//   2026-11-21), so null_forms stores NULL twice per variable; a declaration
//   without one keeps its single store (null_local, pick). At -O2 the stored
//   NULL survives as zeroinitializer (never undef or poison): null_local's
//   comparisons and pick's select (the path verifier's example, which folded
//   to 'a' when the store was missing). The same holds where a jump leaves an
//   initializer for a label in the variable's scope (a 'goto' and a computed
//   'goto' in a statement expression, a 'goto' in one '?:' arm) and for the
//   self-initialization 's = s': -O0 stores NULL before the initializer, and
//   at -O2 the bypass path joins with zeroinitializer.
// - RG4, RI6: '==' calls @__msil_ref.eq and zero-extends its i1; '!=' and
//   every truth test ('if', '!', '&&', '||', a '?:' condition) negate it, a
//   truth test against zeroinitializer; the helper is declared once with
//   exactly 'memory(none) nounwind willreturn speculatable'.
// - RG5: a cast to Object, to the same class or to another spelling of it
//   emits nothing; a cast to another class calls its cast stub
//   ('__msil_mc.<castclass>.__uniq.<n>', one per class;
//   ref-managed-calls.c checks the declarations), and an unused cast is still
//   called at -O2 (it can throw).
// - RG8 and TreeTransform: a VLA bound holding 'c ? a : b' over two
//   spellings of one class (whose type is the class's canonical node), in
//   sizeof's operand (rebuilt by TreeTransform), also through a statement
//   expression with __auto_type.
// - RI4: llvm.fake.use of references at -Og (the driver's
//   -fextend-variable-liveness=all).
// - RG12: falling off the end of a function that returns a reference gets
//   -Wreturn-type and llvm.trap followed by unreachable at every level (the
//   TRAP prefix, -O0 to -O3, -Os, -Oz and -Og); a reference function that
//   always returns has no trap, and neither has an int function that falls
//   off its end (C's own rule). A valueless 'return;' in a reference
//   function gets the trap and unreachable at that statement, at every level
//   (RG12 as amended by section 12's row of 2026-11-21). clang refuses such
//   a 'return;' by default (-Wreturn-mismatch is an error; the -fsyntax-only
//   line pins it), so every RUN line that compiles the unit passes
//   -Wno-return-mismatch, which leaves -Wreturn-type's warnings on.

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -emit-llvm -o %t.win.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.win.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.win.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Wno-return-mismatch -emit-llvm -o %t.lnx.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.lnx.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.lnx.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -Wno-return-mismatch -emit-llvm -o %t.mus.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.mus.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.mus.ll
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -Wno-return-mismatch -emit-llvm -o %t.gnu.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.gnu.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.gnu.ll
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -Wno-return-mismatch -emit-llvm -o %t.a64win.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.a64win.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.a64win.ll
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -Wno-return-mismatch -emit-llvm -o %t.a64lnx.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.a64lnx.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.a64lnx.ll
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -Wno-return-mismatch -emit-llvm -o %t.a64mus.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.a64mus.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.a64mus.ll
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -Wno-return-mismatch -emit-llvm -o %t.a64gnu.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.a64gnu.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.a64gnu.ll
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -Wno-return-mismatch -emit-llvm -o %t.p64.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O0,TRAP --input-file=%t.p64.ll
// RUN: FileCheck %s --check-prefix=HELPER0 --input-file=%t.p64.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -O2 -emit-llvm -o %t.win2.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O2,TRAP --input-file=%t.win2.ll \
// RUN:   --implicit-check-not='target("msil.ref") undef' --implicit-check-not='target("msil.ref") poison'
// RUN: FileCheck %s --check-prefix=HELPER2 --input-file=%t.win2.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Wno-return-mismatch -O2 -emit-llvm -o %t.lnx2.ll %s -verify
// RUN: FileCheck %s --check-prefixes=CHECK,O2,TRAP --input-file=%t.lnx2.ll \
// RUN:   --implicit-check-not='target("msil.ref") undef' --implicit-check-not='target("msil.ref") poison'
// RUN: FileCheck %s --check-prefix=HELPER2 --input-file=%t.lnx2.ll
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -O1 -emit-llvm -o - %s -verify | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -O3 -emit-llvm -o - %s -verify | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -Os -emit-llvm -o - %s -verify | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -Wno-return-mismatch -Oz -emit-llvm -o - %s -verify | FileCheck %s --check-prefix=TRAP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -Wno-return-mismatch -O1 -emit-llvm -o - %s -verify | FileCheck %s --check-prefix=TRAP
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -Wno-return-mismatch -Og -S -emit-llvm -o - %s 2>/dev/null \
// RUN:   | FileCheck %s --check-prefixes=OG,TRAP
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -Wno-return-mismatch -Og -S -emit-llvm -o - %s 2>/dev/null \
// RUN:   | FileCheck %s --check-prefixes=OG,TRAP
// Without -Wno-return-mismatch clang refuses the valueless 'return;': an
// error, which -w (no warnings) leaves on.
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -fsyntax-only -w %s -verify=mismatch

#include <cli/cli.h>

#define NULL ((void *)0)
#define NOINLINE __attribute__((noinline))

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE(" System.String ") Text Text; // another spelling of String
typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.IO.Stream") Stream Stream;

// Opaque inputs: the optimizer cannot know what these return.
String ^machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));
Object ^boxed(void) __attribute__((msil_method("Ns.Objects", "Get", "object()")));
int count(void) __attribute__((msil_method("System.Environment", "get_ProcessorCount", "int32()")));

// test() is emitted first; the static functions follow in the order of their
// first calls in it.
// CHECK-LABEL: define {{.*}}i32 @test()

// RS2, RG3: NULL at the declaration, at every level.
NOINLINE static int null_local(void) {
  String ^r;
  Object ^o;
  return (r == NULL) + (o == NULL);
}
// O0-LABEL: define internal i32 @null_local()
// O0:       %r = alloca target("msil.ref")
// O0:       %o = alloca target("msil.ref")
// O0:       store target("msil.ref") zeroinitializer, ptr %r
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %o
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O2-LABEL: define internal {{.*}}i32 @null_local()
// O2-NOT:   {{undef|poison}}
// O2:       call i1 @__msil_ref.eq(target("msil.ref") zeroinitializer, target("msil.ref") zeroinitializer)
// O2-NOT:   {{undef|poison}}
// O2:       ret i32

// Each null form in an initializer and an assignment. Each declaration
// stores NULL, then its initializer's NULL.
NOINLINE static int null_forms(void) {
  String ^a = NULL, ^b = 0, ^c = (void *)0, ^d = '\0';
  a = NULL;
  b = 0;
  return (a == b) + (c == d);
}
// O0-LABEL: define internal i32 @null_forms()
// O0:       store target("msil.ref") zeroinitializer, ptr %a
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %a
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %b
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %b
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %c
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %c
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %d
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %d
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %a
// O0-NEXT:  store target("msil.ref") zeroinitializer, ptr %b

// The path verifier's example: without the store, -O1 and above would fold
// the phi of 'a' and the unset value to 'a'.
NOINLINE static String ^pick(String ^a, int c) {
  String ^s;
  if (c)
    s = a;
  return s;
}
// O0-LABEL: define internal target("msil.ref") @pick(target("msil.ref") %a, i32 noundef %c)
// O0:       store target("msil.ref") zeroinitializer, ptr %s
// O0-NOT:   store target("msil.ref") zeroinitializer, ptr %s
// O2-LABEL: define internal {{.*}}target("msil.ref") @pick(
// O2:       {{select i1 %[^,]+, target\("msil\.ref"\) zeroinitializer, target\("msil\.ref"\) %a|select i1 %[^,]+, target\("msil\.ref"\) %a, target\("msil\.ref"\) zeroinitializer|phi target\("msil\.ref"\) .*zeroinitializer}}
// O2:       ret target("msil.ref")
// OG-LABEL: define internal {{.*}}target("msil.ref") @pick(
// OG:       call void (...) @llvm.fake.use(target("msil.ref") %{{.*}})

// A jump out of a reference's initializer to a label after the declaration,
// in its scope, skips no NULL store (msil-ref-jumps.c accepts it): the
// declaration stores NULL before the initializer runs, so the bypass path
// reads NULL, as does the self-initialization 's = s'.
NOINLINE static String ^init_goto(String ^a, int c) {
  String ^s = ({
    if (c)
      goto out;
    a;
  });
out:
  return s;
}
NOINLINE static String ^init_computed_goto(String ^a, int c) {
  void *t = &&out;
  String ^s = ({
    if (c)
      goto *t;
    a;
  });
out:
  return s;
}
NOINLINE static String ^init_arm_goto(String ^a, int c) {
  String ^s = c ? ({
    goto out;
    a;
  })
                : a;
out:
  return s;
}
NOINLINE static String ^self_init(String ^a, int c) {
  String ^s = s;
  if (c)
    s = a;
  return s;
}
// O0-LABEL: define internal target("msil.ref") @init_goto(
// O0-NOT:   load i32, ptr %c.addr
// O0:       store target("msil.ref") zeroinitializer, ptr %s
// O0:       load i32, ptr %c.addr
// O0:       store target("msil.ref") %{{.+}}, ptr %s
// O0:       load target("msil.ref"), ptr %s
// O0:       ret target("msil.ref")
// O2-LABEL: define internal {{.*}}target("msil.ref") @init_goto(
// O2:       {{select i1 %[^,]+, target\("msil\.ref"\) zeroinitializer, target\("msil\.ref"\) %a|select i1 %[^,]+, target\("msil\.ref"\) %a, target\("msil\.ref"\) zeroinitializer|phi target\("msil\.ref"\) .*zeroinitializer}}
// O2:       ret target("msil.ref")
// O0-LABEL: define internal target("msil.ref") @init_computed_goto(
// O0-NOT:   load i32, ptr %c.addr
// O0:       store target("msil.ref") zeroinitializer, ptr %s
// O0:       load i32, ptr %c.addr
// O0:       store target("msil.ref") %{{.+}}, ptr %s
// O0:       load target("msil.ref"), ptr %s
// O0:       indirectbr
// O2-LABEL: define internal {{.*}}target("msil.ref") @init_computed_goto(
// O2:       {{select i1 %[^,]+, target\("msil\.ref"\) zeroinitializer, target\("msil\.ref"\) %a|select i1 %[^,]+, target\("msil\.ref"\) %a, target\("msil\.ref"\) zeroinitializer|phi target\("msil\.ref"\) .*zeroinitializer}}
// O2:       ret target("msil.ref")
// O0-LABEL: define internal target("msil.ref") @init_arm_goto(
// O0-NOT:   load i32, ptr %c.addr
// O0:       store target("msil.ref") zeroinitializer, ptr %s
// O0:       load i32, ptr %c.addr
// O0:       store target("msil.ref") %{{.+}}, ptr %s
// O0:       load target("msil.ref"), ptr %s
// O0:       ret target("msil.ref")
// O2-LABEL: define internal {{.*}}target("msil.ref") @init_arm_goto(
// O2:       {{select i1 %[^,]+, target\("msil\.ref"\) zeroinitializer, target\("msil\.ref"\) %a|select i1 %[^,]+, target\("msil\.ref"\) %a, target\("msil\.ref"\) zeroinitializer|phi target\("msil\.ref"\) .*zeroinitializer}}
// O2:       ret target("msil.ref")
// O0-LABEL: define internal target("msil.ref") @self_init(
// O0-NOT:   load target("msil.ref"), ptr %s
// O0:       store target("msil.ref") zeroinitializer, ptr %s
// O0-NEXT:  %[[SELF:[0-9]+]] = load target("msil.ref"), ptr %s
// O0-NEXT:  store target("msil.ref") %[[SELF]], ptr %s
// O2-LABEL: define internal {{.*}}target("msil.ref") @self_init(
// O2:       {{select i1 %[^,]+, target\("msil\.ref"\) zeroinitializer, target\("msil\.ref"\) %a|select i1 %[^,]+, target\("msil\.ref"\) %a, target\("msil\.ref"\) zeroinitializer|phi target\("msil\.ref"\) .*zeroinitializer}}
// O2:       ret target("msil.ref")

// NULL as a return value and as an argument.
NOINLINE static String ^null_return(void) { return NULL; }
// O0-LABEL: define internal target("msil.ref") @null_return()
// O0:       ret target("msil.ref") zeroinitializer
NOINLINE static int null_argument(int c) { return pick(NULL, c) == NULL; }
// O0-LABEL: define internal i32 @null_argument(
// O0:       call target("msil.ref") @pick(target("msil.ref") zeroinitializer, i32 noundef %{{[0-9]+}})
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %call, target("msil.ref") zeroinitializer)

// RG4: '==' and '!=' (two spellings of one class, Object against a class).
NOINLINE static int equal(String ^a, Text ^b) { return a == b; }
// O0-LABEL: define internal i32 @equal(
// O0:       %[[EQ1:.+]] = call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}})
// O0-NEXT:  zext i1 %[[EQ1]] to i32
NOINLINE static int not_equal(Object ^o, String ^a) { return o != a; }
// O0-LABEL: define internal i32 @not_equal(
// O0:       %[[EQ2:.+]] = call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}})
// O0-NEXT:  %[[NE2:.+]] = xor i1 %[[EQ2]], true
// O0-NEXT:  zext i1 %[[NE2]] to i32

// RS6: truth tests compare with NULL.
NOINLINE static int truth(String ^a, Object ^o) {
  int r = 0;
  if (a)
    r += 1;
  r += !o;
  r += a && o;
  r += a || o;
  r += o ? 8 : 16;
  return r;
}
// O0-LABEL: define internal i32 @truth(
// O0:       %[[T1:.+]] = call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O0-NEXT:  %[[T2:.+]] = xor i1 %[[T1]], true
// O0-NEXT:  br i1 %[[T2]],
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O0:       ret i32

// RG5: casts that emit nothing.
NOINLINE static Object ^up(String ^s) { return s; }
NOINLINE static Object ^up_cast(String ^s) { return (Object ^)s; }
NOINLINE static String ^same_cast(Text ^t) { return (String ^)t; }
// O0-LABEL: define internal target("msil.ref") @up(
// O0-NOT:   call
// O0:       ret target("msil.ref") %{{[0-9]+}}
// O0-LABEL: define internal target("msil.ref") @up_cast(
// O0-NOT:   call
// O0:       ret target("msil.ref") %{{[0-9]+}}
// O0-LABEL: define internal target("msil.ref") @same_cast(
// O0-NOT:   call
// O0:       ret target("msil.ref") %{{[0-9]+}}

// RG5: a cast to another class calls its stub, also when the result is
// unused, at every level.
NOINLINE static String ^down(Object ^o) { return (String ^)o; }
NOINLINE static void unused_casts(Object ^o) {
  (void)(String ^)o;
  (void)(Text ^)o;
  (void)(Stream ^)o;
}
// CHECK-LABEL: define internal {{.*}}target("msil.ref") @down(
// CHECK:       call {{.*}}target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STRCAST:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK-LABEL: define internal {{.*}}void @unused_casts(
// CHECK:       call {{.*}}target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STRCAST]]"(target("msil.ref") %{{.+}})
// CHECK:       call {{.*}}target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STRCAST]]"(target("msil.ref") %{{.+}})
// CHECK:       call {{.*}}target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STREAMCAST:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       ret void

// RG8: a VLA bound with a reference '?:' over two spellings.
NOINLINE static int vla_bound(String ^a, Text ^b, int c) {
  return (int)sizeof(int[(c ? a : b) != NULL ? 3 : 5]);
}
NOINLINE static int vla_auto(String ^a, Text ^b, int c) {
  return (int)sizeof(int[({ __auto_type t = c ? a : b; t ? 3 : 5; })]);
}
// O0-LABEL: define internal i32 @vla_bound(
// O0:       %[[P1:.+]] = phi target("msil.ref") [ %{{[0-9]+}}, %{{.+}} ], [ %{{[0-9]+}}, %{{.+}} ]
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %[[P1]], target("msil.ref") zeroinitializer)
// O0:       select i1 %{{.+}}, i32 3, i32 5
// O0-LABEL: define internal i32 @vla_auto(
// O0:       %t = alloca target("msil.ref")
// O0:       store target("msil.ref") zeroinitializer, ptr %t
// O0:       %[[P2:.+]] = phi target("msil.ref") [ %{{[0-9]+}}, %{{.+}} ], [ %{{[0-9]+}}, %{{.+}} ]
// O0:       store target("msil.ref") %[[P2]], ptr %t
// O0:       call i1 @__msil_ref.eq(target("msil.ref") %{{[0-9]+}}, target("msil.ref") zeroinitializer)
// O0:       select i1 %{{.+}}, i32 3, i32 5

// RG12: falling off the end.
NOINLINE static String ^falls(int c) {
  if (c)
    return machine();
} // expected-warning-re {{{{^}}non-void function does not return a value in all control paths{{$}}}}
// TRAP-LABEL: define internal {{.*}}target("msil.ref") @falls(
// TRAP:       call void @llvm.trap()
// TRAP-NEXT:  unreachable
NOINLINE static String ^always(int c) {
  if (c)
    return machine();
  return NULL;
}
// TRAP-LABEL: define internal {{.*}}target("msil.ref") @always(
// TRAP-NOT:   llvm.trap
// TRAP:       ret target("msil.ref")
// TRAP:       {{^}}}
// A valueless 'return;' (-Wno-return-mismatch): the trap at that statement,
// on the path where 'c' is 0 (at -O0 the block after the 'if'). From -O1 on
// the optimizer may give valueless a 'void' result: when it returns, it
// returns 'a', which its caller already holds.
NOINLINE static String ^valueless(String ^a, int c) {
  if (c)
    return a;
  return; // mismatch-error-re {{{{^}}non-void function 'valueless' should return a value{{$}}}}
}
// TRAP-LABEL: define internal {{.*}}@valueless(
// O0:         br i1 %tobool, label %if.then, label %if.end
// O0:         {{^}}if.end:
// TRAP:       call void @llvm.trap()
// TRAP-NEXT:  unreachable
NOINLINE static int falls_int(int c) {
  if (c)
    return 1;
} // expected-warning-re {{{{^}}non-void function does not return a value in all control paths{{$}}}}
// O0-LABEL: define internal i32 @falls_int(
// O0-NOT:   llvm.trap
// O0:       {{^}}}

// The helper (RI6): one declaration, with exactly these attributes at -O0
// (the optimizer may add more).
// HELPER0:     declare i1 @__msil_ref.eq(target("msil.ref"), target("msil.ref")) [[EQATTR:#[0-9]+]]
// HELPER0-NOT: declare {{.*}}@__msil_ref.eq
// HELPER0:     attributes [[EQATTR]] = { nounwind speculatable willreturn memory(none) }
// HELPER2:     declare i1 @__msil_ref.eq(target("msil.ref"), target("msil.ref"))
// HELPER2-NOT: declare {{.*}}@__msil_ref.eq

int test(void) {
  String ^m = machine();
  Object ^o = boxed();
  int c = count();
  int r = null_local() + null_forms();
  r += pick(m, c) == NULL;
  r += (init_goto(m, c) == NULL) + (init_computed_goto(m, c) == NULL);
  r += (init_arm_goto(m, c) == NULL) + (self_init(m, c) == NULL);
  r += null_return() == NULL;
  r += null_argument(c);
  r += equal(m, m) + not_equal(o, m) + truth(m, o);
  r += (up(m) == o) + (up_cast(m) == o) + (same_cast(m) == m);
  r += down(o) == m;
  unused_casts(o);
  r += vla_bound(m, m, c) + vla_auto(m, NULL, c);
  r += (falls(c) == m) + (always(c) == m) + (valueless(m, c) == m) + falls_int(c);
  return r;
}
