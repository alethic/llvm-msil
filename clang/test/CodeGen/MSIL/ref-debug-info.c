// Debug info for .NET references (ImplementationPlan.md 5.21.5 RG7 and
// 5.21.14 RV13, milestone R-a): -g works, and a reference is described as a
// forward-declared structure named by its class's display name (RC4), as
// clang describes WebAssembly's reference types: one node per class, shared
// by every spelling ('String ^' and 'Text ^'), by parameters, locals and the
// subroutine type (the ONE runs check that each name appears once), for a
// class, a nested type and arrays ('string[]' for both spellings of an array
// of strings, 'class System.IO.Stream[]'). DWARF on the eight host triples
// and the portable triple, CodeView too on the x86_64 Windows MSVC triple;
// limited and standalone debug info, at -O0 and -O2.
//
// -g also works past clang's IR (5.21.6 RI4): with the MSIL target the unit
// compiles with debug info to a CIL object at -O0 and -O2 on the Windows and
// portable triples (DWARF 4 and DWARF 5, the versions the driver's -g picks
// there); the compile succeeding is the check. P15 removes the debug records
// and lifetime markers of reference slots. Above -O0 clang leaves reference
// slots only in an optnone function: 'keep' keeps its slots at -O2, with
// llvm.lifetime.start/end and #dbg_declare (LIFE), so they are the slots P15
// receives there.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -debug-info-kind=limited -dwarf-version=5 -emit-llvm -o %t.lnx.ll %s
// RUN: FileCheck %s --input-file=%t.lnx.ll
// RUN: FileCheck %s --input-file=%t.lnx.ll --check-prefix=ONE
// RUN: FileCheck %s --input-file=%t.lnx.ll --check-prefix=ONEARR
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -debug-info-kind=limited -dwarf-version=4 -emit-llvm -o %t.win.ll %s
// RUN: FileCheck %s --input-file=%t.win.ll
// RUN: FileCheck %s --input-file=%t.win.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -debug-info-kind=limited -gcodeview -emit-llvm -o %t.cv.ll %s
// RUN: FileCheck %s --input-file=%t.cv.ll
// RUN: FileCheck %s --input-file=%t.cv.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -debug-info-kind=standalone -dwarf-version=5 -emit-llvm -o %t.p64.ll %s
// RUN: FileCheck %s --input-file=%t.p64.ll
// RUN: FileCheck %s --input-file=%t.p64.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -debug-info-kind=limited -dwarf-version=5 -emit-llvm -o %t.mus.ll %s
// RUN: FileCheck %s --input-file=%t.mus.ll
// RUN: FileCheck %s --input-file=%t.mus.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -debug-info-kind=limited -dwarf-version=5 -emit-llvm -o %t.a64lnx.ll %s
// RUN: FileCheck %s --input-file=%t.a64lnx.ll
// RUN: FileCheck %s --input-file=%t.a64lnx.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -debug-info-kind=limited -dwarf-version=5 -emit-llvm -o %t.a64mus.ll %s
// RUN: FileCheck %s --input-file=%t.a64mus.ll
// RUN: FileCheck %s --input-file=%t.a64mus.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -debug-info-kind=limited -dwarf-version=4 -emit-llvm -o %t.gnu.ll %s
// RUN: FileCheck %s --input-file=%t.gnu.ll
// RUN: FileCheck %s --input-file=%t.gnu.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -debug-info-kind=limited -dwarf-version=4 -emit-llvm -o %t.a64win.ll %s
// RUN: FileCheck %s --input-file=%t.a64win.ll
// RUN: FileCheck %s --input-file=%t.a64win.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -debug-info-kind=limited -dwarf-version=4 -emit-llvm -o %t.a64gnu.ll %s
// RUN: FileCheck %s --input-file=%t.a64gnu.ll
// RUN: FileCheck %s --input-file=%t.a64gnu.ll --check-prefix=ONE
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -debug-info-kind=limited -dwarf-version=5 -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=TYPES
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -debug-info-kind=limited -dwarf-version=4 -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=LIFE
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -debug-info-kind=limited -dwarf-version=4 -emit-obj -o %t.win.O0.o %s %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -debug-info-kind=limited -dwarf-version=4 -O2 -emit-obj -o %t.win.O2.o %s %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple msil64-unknown-dotnet -debug-info-kind=standalone -dwarf-version=5 -emit-obj -o %t.p64.O0.o %s %}
// RUN: %if msil-registered-target %{ %clang_cc1 -triple msil64-unknown-dotnet -debug-info-kind=standalone -dwarf-version=5 -O2 -emit-obj -o %t.p64.O2.o %s %}

typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type(" System.String "))) Text Text;
typedef struct __attribute__((cli_type("Ns.Outer/Inner"))) Inner Inner;
typedef struct __attribute__((cli_type("string[]"))) Strings Strings;
typedef struct __attribute__((cli_type("class System.String[]"))) Strings2 Strings2;
typedef struct __attribute__((cli_type("class System.IO.Stream[]"))) Streams Streams;

String ^machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));

__attribute__((noinline)) static int f(String ^a, Text ^b, Inner ^c, Strings ^d, Strings2 ^e, Streams ^g) {
  String ^l = a;
  Text ^m = b;
  return (l == m) + (c == 0) + (d == e) + (g == 0);
}

__attribute__((optnone, noinline)) static String ^keep(String ^p, int n) {
  String ^r;
  {
    String ^t = p;
    if (n)
      r = t;
  }
  return r;
}

int test(void) {
  String ^s = machine();
  return f(s, s, 0, 0, 0, 0) + (keep(s, 1) == s);
}

// CHECK-DAG: [[STR:![0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "System.String", {{.*}}flags: DIFlagFwdDecl{{.*}})
// CHECK-DAG: [[INNER:![0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "Ns.Outer/Inner", {{.*}}flags: DIFlagFwdDecl{{.*}})
// CHECK-DAG: [[ARR:![0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "string[]", {{.*}}flags: DIFlagFwdDecl{{.*}})
// CHECK-DAG: [[STREAMS:![0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "class System.IO.Stream[]", {{.*}}flags: DIFlagFwdDecl{{.*}})
// CHECK-DAG: !DILocalVariable(name: "a", arg: 1, {{.*}}type: [[STR]])
// CHECK-DAG: !DILocalVariable(name: "b", arg: 2, {{.*}}type: [[STR]])
// CHECK-DAG: !DILocalVariable(name: "c", arg: 3, {{.*}}type: [[INNER]])
// CHECK-DAG: !DILocalVariable(name: "d", arg: 4, {{.*}}type: [[ARR]])
// CHECK-DAG: !DILocalVariable(name: "e", arg: 5, {{.*}}type: [[ARR]])
// CHECK-DAG: !DILocalVariable(name: "g", arg: 6, {{.*}}type: [[STREAMS]])
// CHECK-DAG: !DILocalVariable(name: "l", {{.*}}type: [[STR]])
// CHECK-DAG: !DILocalVariable(name: "m", {{.*}}type: [[STR]])
// CHECK-DAG: !DILocalVariable(name: "s", {{.*}}type: [[STR]])
// f's subroutine type: the result, then one node per parameter.
// CHECK-DAG: !{!{{[0-9]+}}, [[STR]], [[STR]], [[INNER]], [[ARR]], [[ARR]], [[STREAMS]]}

// One node per class.
// ONE:     name: "System.String"
// ONE-NOT: name: "System.String"
// ONEARR:     name: "string[]"
// ONEARR-NOT: name: "string[]"

// TYPES-DAG: [[STR:![0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "System.String", {{.*}}flags: DIFlagFwdDecl{{.*}})
// TYPES-DAG: !DILocalVariable(name: "s", {{.*}}type: [[STR]])

// LIFE-LABEL: define internal target("msil.ref") @keep(
// LIFE-DAG:   %r = alloca target("msil.ref")
// LIFE-DAG:   %t = alloca target("msil.ref")
// LIFE-DAG:   call void @llvm.lifetime.start.p0(ptr %r)
// LIFE-DAG:   call void @llvm.lifetime.start.p0(ptr %t)
// LIFE-DAG:   #dbg_declare(ptr %r,
// LIFE-DAG:   #dbg_declare(ptr %t,
// LIFE-DAG:   call void @llvm.lifetime.end.p0(ptr %t)
// LIFE-DAG:   call void @llvm.lifetime.end.p0(ptr %r)
// LIFE:       ret target("msil.ref")
