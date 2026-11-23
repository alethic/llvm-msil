// Managed calls and cast stubs with .NET references in the IR
// (ImplementationPlan.md 5.21.9 RM1, RM2, RM8, RM9, 5.21.5 RG5, RG9, 5.20.4
// MR1-MR4 as amended by section 12's row 'MC-1's contract' (2026-11-21), and
// 5.21.14 RV11, milestone R-a):
// - RM1, RM2: "msil-managed-conv" holds 'ref' for a reference parameter or
//   result and 'this-ref' for a reference 'this', beside MC-1's codes: 'val',
//   'gch' and 'this-gch' for tickets (RM9), 'err' with msil_catch, 'void';
//   references are target("msil.ref") in the declarations (MR2) and the
//   '!msil.managed.calls' operands repeat the attributes (MR4). A static
//   managed call gets its '.__uniq.<n>' postfix.
// - RM8, RG5: a C cast to another class calls a declaration
//   '__msil_mc.<castclass>.__uniq.<n>' (type 'target("msil.ref")
//   (target("msil.ref"))') with exactly MR1's four attributes: the class's
//   display name, '.castclass', 'object(object)' and 'ref;ref', and no memory
//   attribute, plus its MR4 entry: one declaration per class in a module,
//   shared by every cast to that class in any spelling ('class
//   System.String[]' and 'string[]' are one array class), named by the class
//   (System.String, System.IO.Stream, string[], class System.IO.Stream[],
//   Ns.Outer/Inner).
// - RG9: the names do not depend on the unit, the spelling or the triple: the
//   TU2 run compiles another unit on the Linux triple that casts to the same
//   classes through other typedef names and declares the same static managed
//   call through another spelling; checked together with the Windows unit,
//   every name is equal (equal casts merge under LTO).
// - With the back end, the -S text defines each stub: a cast stub is
//   'ldarg.0; castclass <class>; ret', a 'this-ref' stub calls with
//   'callvirt' (the IL runs; llvm/test/CodeGen/MSIL/managed-call-*.ll check
//   the bodies in detail).

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o %t.win.ll %s
// RUN: FileCheck %s --input-file=%t.win.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o %t.lnx.ll %s
// RUN: FileCheck %s --input-file=%t.lnx.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o %t.mus.ll %s
// RUN: FileCheck %s --input-file=%t.mus.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o %t.gnu.ll %s
// RUN: FileCheck %s --input-file=%t.gnu.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o %t.a64win.ll %s
// RUN: FileCheck %s --input-file=%t.a64win.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o %t.a64lnx.ll %s
// RUN: FileCheck %s --input-file=%t.a64lnx.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o %t.a64mus.ll %s
// RUN: FileCheck %s --input-file=%t.a64mus.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o %t.a64gnu.ll %s
// RUN: FileCheck %s --input-file=%t.a64gnu.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o %t.p64.ll %s
// RUN: FileCheck %s --input-file=%t.p64.ll --check-prefixes=CHECK,ATTR,MD
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -emit-llvm -o %t.win2.ll %s
// RUN: FileCheck %s --input-file=%t.win2.ll --check-prefixes=ATTR,MD

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -DTU2 -emit-llvm -o %t.tu2.ll %s
// RUN: cat %t.win.ll %t.tu2.ll > %t.both.ll
// RUN: FileCheck %s --input-file=%t.both.ll --check-prefix=SAME

// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -S -o %t.il %s %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL --input-file=%t.il %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t.dll %t.il %}

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)
typedef __INTPTR_TYPE__ intptr_t;

#if defined(TU2)

// Another unit: other typedef names and spellings of the same classes.
typedef struct CLI_TYPE(" System.String ") Str Str;
typedef struct CLI_TYPE("System.Object") Obj Obj;
typedef struct CLI_TYPE("System.IO.Stream") Strm Strm;
typedef struct CLI_TYPE("class System.String[]") StrArr StrArr;
static Str ^u_copy(Str ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
Obj ^u_boxed(void) __attribute__((msil_method("Ns.Objects", "Get", "object()")));
int tu2(void) {
  Obj ^o = u_boxed();
  return ((Str ^)o == u_copy(NULL)) + ((Strm ^)o == NULL) + ((StrArr ^)o == NULL);
}
#else

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.String") Text Text; // another typedef name of String
typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.IO.Stream") Stream Stream;
typedef struct CLI_TYPE("System.Text.StringBuilder") StringBuilder StringBuilder;
typedef struct CLI_TYPE("System.IDisposable") IDisposable IDisposable;
typedef struct CLI_TYPE("Ns.Outer/Inner") Inner Inner;
typedef struct CLI_TYPE("string[]") Strings Strings;
typedef struct CLI_TYPE("class System.String[]") Strings2 Strings2;
typedef struct CLI_TYPE("class System.IO.Stream[]") Streams Streams;
typedef struct CLI_TYPE("uint8[]") Bytes Bytes;

String ^r_empty(void) __attribute__((msil_method("System.String", "Empty", "field string")));
String ^r_concat(String ^a, Text ^b) __attribute__((msil_method("System.String", "Concat", "string(string, string)")));
void r_keep(Object ^o) __attribute__((msil_method("System.GC", "KeepAlive", "void(class System.Object)")));
StringBuilder ^r_new_builder(void) __attribute__((msil_method("System.Text.StringBuilder", ".ctor", "instance void()")));
StringBuilder ^r_append(StringBuilder ^sb, String ^s) __attribute__((msil_method("System.Text.StringBuilder", "Append", "instance class System.Text.StringBuilder(string)")));
Bytes ^r_new_bytes(int n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int32)")));
void r_dispose(IDisposable ^d) __attribute__((msil_method("System.IDisposable", "Dispose", "instance void()")));
int r_length(String ^s) __attribute__((msil_method("System.String", "get_Length", "instance int32()")));
String ^r_read_all(String ^path, int *err) __attribute__((msil_method("System.IO.File", "ReadAllText", "string(string)"), msil_catch("System.IO.IOException", 5)));
intptr_t t_alloc(Object ^o) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Alloc", "valuetype System.Runtime.InteropServices.GCHandle(object)")));
Object ^t_target(intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "get_Target", "instance object()")));
static String ^r_static(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));

// CHECK-LABEL: define {{.*}}i32 @use()
// CHECK:       call target("msil.ref") @__msil_mc.r_empty()
// CHECK:       call target("msil.ref") @__msil_mc.r_new_builder()
// CHECK:       call target("msil.ref") @__msil_mc.r_append(target("msil.ref") %{{.+}}, target("msil.ref") %{{.+}})
// CHECK:       call i64 @__msil_mc.t_alloc(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @__msil_mc.t_target(i64 noundef %{{.+}})
// CHECK:       call void @__msil_mc.r_keep(target("msil.ref") %{{.+}})
// Casts to String in two spellings share one declaration; Object needs none.
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STR:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STR]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[IDISP:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       call void @__msil_mc.r_dispose(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STREAM:[0-9]+]]"(target("msil.ref") %{{.+}})
// Both spellings of an array of strings are one class.
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[ARR:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[ARR]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[STREAMS:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[INNER:[0-9]+]]"(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @__msil_mc.r_static.__uniq.[[UNIQ:[0-9]+]](target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @__msil_mc.r_read_all(target("msil.ref") %{{.+}}, ptr noundef %err)
// CHECK:       call target("msil.ref") @__msil_mc.r_concat(target("msil.ref") %{{.+}}, target("msil.ref") %{{.+}})
// CHECK:       call i32 @__msil_mc.r_length(target("msil.ref") %{{.+}})
// CHECK:       call target("msil.ref") @__msil_mc.r_new_bytes(i32 noundef 4)
// CHECK-NOT:   call {{.*}}castclass
// CHECK:       ret i32

// MR1 and MR2: each declaration and its attributes.
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_empty() {{(local_unnamed_addr )?}}[[A_EMPTY:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_concat(target("msil.ref"), target("msil.ref")) {{(local_unnamed_addr )?}}[[A_CONCAT:#[0-9]+]]
// ATTR-DAG: declare void @__msil_mc.r_keep(target("msil.ref")) {{(local_unnamed_addr )?}}[[A_KEEP:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_new_builder() {{(local_unnamed_addr )?}}[[A_NEW:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_append(target("msil.ref"), target("msil.ref")) {{(local_unnamed_addr )?}}[[A_APPEND:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_new_bytes(i32 noundef) {{(local_unnamed_addr )?}}[[A_BYTES:#[0-9]+]]
// ATTR-DAG: declare void @__msil_mc.r_dispose(target("msil.ref")) {{(local_unnamed_addr )?}}[[A_DISPOSE:#[0-9]+]]
// ATTR-DAG: declare i32 @__msil_mc.r_length(target("msil.ref")) {{(local_unnamed_addr )?}}[[A_LENGTH:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_read_all(target("msil.ref"), ptr noundef) {{(local_unnamed_addr )?}}[[A_READ:#[0-9]+]]
// ATTR-DAG: declare i64 @__msil_mc.t_alloc(target("msil.ref")) {{(local_unnamed_addr )?}}[[A_ALLOC:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.t_target(i64 noundef) {{(local_unnamed_addr )?}}[[A_TARGET:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @__msil_mc.r_static.__uniq.{{[0-9]+}}(target("msil.ref")) {{(local_unnamed_addr )?}}[[A_STATIC:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_STR:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_IDISP:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_STREAM:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_ARR:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_STREAMS:#[0-9]+]]
// ATTR-DAG: declare target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref")) {{(local_unnamed_addr )?}}[[C_INNER:#[0-9]+]]
// ATTR-DAG: attributes [[A_EMPTY]] = { {{([a-z_]+ )*}}"msil-managed-conv"=";ref" "msil-managed-member"="Empty" "msil-managed-sig"="field string" "msil-managed-type"="System.String" {{.*}}}
// ATTR-DAG: attributes [[A_CONCAT]] = { {{([a-z_]+ )*}}"msil-managed-conv"="ref,ref;ref" "msil-managed-member"="Concat" "msil-managed-sig"="string(string, string)" "msil-managed-type"="System.String" {{.*}}}
// ATTR-DAG: attributes [[A_KEEP]] = { {{([a-z_]+ )*}}"msil-managed-conv"="ref;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(class System.Object)" "msil-managed-type"="System.GC" {{.*}}}
// ATTR-DAG: attributes [[A_NEW]] = { {{([a-z_]+ )*}}"msil-managed-conv"=";ref" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void()" "msil-managed-type"="System.Text.StringBuilder" {{.*}}}
// ATTR-DAG: attributes [[A_APPEND]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-ref,ref;ref" "msil-managed-member"="Append" "msil-managed-sig"="instance class System.Text.StringBuilder(string)" "msil-managed-type"="System.Text.StringBuilder" {{.*}}}
// ATTR-DAG: attributes [[A_BYTES]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;ref" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="uint8[]" {{.*}}}
// ATTR-DAG: attributes [[A_DISPOSE]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-ref;void" "msil-managed-member"="Dispose" "msil-managed-sig"="instance void()" "msil-managed-type"="System.IDisposable" {{.*}}}
// ATTR-DAG: attributes [[A_LENGTH]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-ref;val" "msil-managed-member"="get_Length" "msil-managed-sig"="instance int32()" "msil-managed-type"="System.String" {{.*}}}
// ATTR-DAG: attributes [[A_READ]] = { {{([a-z_]+ )*}}"msil-managed-catch"="System.IO.IOException=5" "msil-managed-conv"="ref,err;ref" "msil-managed-member"="ReadAllText" "msil-managed-sig"="string(string)" "msil-managed-type"="System.IO.File" {{.*}}}
// ATTR-DAG: attributes [[A_ALLOC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="ref;gch" "msil-managed-member"="Alloc" "msil-managed-sig"="valuetype System.Runtime.InteropServices.GCHandle(object)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" {{.*}}}
// ATTR-DAG: attributes [[A_TARGET]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-gch;ref" "msil-managed-member"="get_Target" "msil-managed-sig"="instance object()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" {{.*}}}
// ATTR-DAG: attributes [[A_STATIC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="ref;ref" "msil-managed-member"="Copy" "msil-managed-sig"="string(string)" "msil-managed-type"="System.String" {{.*}}}
// RM8: a cast declaration has exactly MR1's four attributes and nothing
// else (no memory attribute: an unused cast still runs and throws).
// ATTR-DAG: attributes [[C_STR]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.String" }
// ATTR-DAG: attributes [[C_IDISP]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.IDisposable" }
// ATTR-DAG: attributes [[C_STREAM]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="System.IO.Stream" }
// ATTR-DAG: attributes [[C_ARR]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="string[]" }
// ATTR-DAG: attributes [[C_STREAMS]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="class System.IO.Stream[]" }
// ATTR-DAG: attributes [[C_INNER]] = { "msil-managed-conv"="ref;ref" "msil-managed-member"=".castclass" "msil-managed-sig"="object(object)" "msil-managed-type"="Ns.Outer/Inner" }

// MR4: one operand per declaration, casts included.
// MD-DAG: !{!"__msil_mc.r_empty", !"System.String", !"Empty", !"field string", !"", !";ref", !""}
// MD-DAG: !{!"__msil_mc.r_concat", !"System.String", !"Concat", !"string(string, string)", !"", !"ref,ref;ref", !""}
// MD-DAG: !{!"__msil_mc.r_append", !"System.Text.StringBuilder", !"Append", !"instance class System.Text.StringBuilder(string)", !"", !"this-ref,ref;ref", !""}
// MD-DAG: !{!"__msil_mc.r_dispose", !"System.IDisposable", !"Dispose", !"instance void()", !"", !"this-ref;void", !""}
// MD-DAG: !{!"__msil_mc.r_read_all", !"System.IO.File", !"ReadAllText", !"string(string)", !"", !"ref,err;ref", !"System.IO.IOException=5"}
// MD-DAG: !{!"__msil_mc.t_alloc", !"valuetype System.Runtime.InteropServices.GCHandle", !"Alloc", !"valuetype System.Runtime.InteropServices.GCHandle(object)", !"", !"ref;gch", !""}
// MD-DAG: !{!"__msil_mc.t_target", !"valuetype System.Runtime.InteropServices.GCHandle", !"get_Target", !"instance object()", !"", !"this-gch;ref", !""}
// MD-DAG: !{!"__msil_mc.r_static.__uniq.{{[0-9]+}}", !"System.String", !"Copy", !"string(string)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"System.String", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"System.IDisposable", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"System.IO.Stream", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"string[]", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"class System.IO.Stream[]", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !{!"__msil_mc.<castclass>.__uniq.{{[0-9]+}}", !"Ns.Outer/Inner", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// MD-DAG: !msil.managed.calls = !{

// RG9 across units, spellings and triples (the TU2 run): the names that
// this unit's metadata gives on the Windows triple are those the other unit
// calls on the Linux triple.
// SAME-DAG: !{!"__msil_mc.<castclass>.__uniq.[[S:[0-9]+]]", !"System.String", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// SAME-DAG: !{!"__msil_mc.<castclass>.__uniq.[[ST:[0-9]+]]", !"System.IO.Stream", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// SAME-DAG: !{!"__msil_mc.<castclass>.__uniq.[[SA:[0-9]+]]", !"string[]", !".castclass", !"object(object)", !"", !"ref;ref", !""}
// SAME-DAG: !{!"__msil_mc.r_static.__uniq.[[U:[0-9]+]]", !"System.String", !"Copy", !"string(string)", !"", !"ref;ref", !""}
// SAME:     define {{.*}}i32 @tu2()
// SAME-DAG: call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[S]]"(
// SAME-DAG: call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[ST]]"(
// SAME-DAG: call target("msil.ref") @"__msil_mc.<castclass>.__uniq.[[SA]]"(
// SAME-DAG: call target("msil.ref") @__msil_mc.u_copy.__uniq.[[U]](

// IL-LABEL: .method {{.*}} '__msil_mc.r_append'(object, object) cil managed
// IL:       castclass {{.*}}System.Text.StringBuilder
// IL:       callvirt instance class {{.*}}System.Text.StringBuilder {{.*}}System.Text.StringBuilder::Append(string)
// IL-LABEL: .method {{.*}} '__msil_mc.r_dispose'(object) cil managed
// IL:       castclass {{.*}}System.IDisposable
// IL:       callvirt instance void {{.*}}System.IDisposable::Dispose()
// IL-LABEL: .method assembly static object '__msil_mc.<castclass>.__uniq.{{[0-9]+}}'(object) cil managed
// IL-NEXT:  {
// IL:       ldarg.0
// IL-NEXT:  castclass {{.*}}System.IDisposable
// IL-NEXT:  ret
// IL-NEXT:  } // end of method '__msil_mc.<castclass>.__uniq.{{[0-9]+}}'

int use(void) {
  int err = 0;
  String ^s = r_empty();
  StringBuilder ^sb = r_append(r_new_builder(), s);
  intptr_t ticket = t_alloc(sb);
  Object ^o = t_target(ticket);
  r_keep(o);
  String ^a = (String ^)o;
  Text ^b = (Text ^)o;
  Object ^c = (Object ^)a;
  r_dispose((IDisposable ^)o);
  Stream ^st = (Stream ^)o;
  Strings ^arr1 = (Strings ^)o;
  Strings2 ^arr2 = (Strings2 ^)o;
  Streams ^sts = (Streams ^)o;
  Inner ^in = (Inner ^)o;
  String ^copy = r_static(a);
  String ^text = r_read_all(copy, &err);
  String ^both = r_concat(text, b);
  return r_length(both) + (c == o) + (st == NULL) + (arr1 == arr2) + (sts == NULL) + (in == NULL) +
         (r_new_bytes(4) == NULL) + err;
}

#endif
