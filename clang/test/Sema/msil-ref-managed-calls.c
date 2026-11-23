// Managed calls with .NET references (ImplementationPlan.md 5.21.9 RM1-RM13,
// 5.20.3 as amended by section 12's row 'MC-1's contract' (2026-11-21), and
// 5.21.14 RV8, milestone R-a).
// - RM2: a reference takes 'ref' for a CIL 'string', 'object', 'class T' or
//   'T[]' parameter, result, '.ctor' type (classes and arrays) or field when
//   its key equals the CIL type's folded key ('class System.String' is
//   'string', 'class System.Object' is 'object', both spellings of an array
//   are one key, two C spellings of a class are one key), and 'this-ref' for
//   'this' of a class, interface, nested type, String or Object TYPE with an
//   equal key (instance methods and instance fields). Exact classes only (the
//   user's pick "I think A is correct."): a String reference for 'object', an
//   Object reference for 'string', a Stream for a FileStream and back, a
//   reference for a value type, a GCHandle, a primitive, a pointer or 'void',
//   and an array for System.Array's 'this' are T-M3, pinned whole; for 'this'
//   it names "the 'this' reference" (RM4). A call converts a class to Object
//   at the call site as C does (RS4).
// - RM4: T-M4's breakdown says "a 'this' reference" when the first C
//   parameter is a reference (5.20.3's example, pinned whole); T-M12 alone
//   for a variadic or unprototyped managed call with references.
// - RM5: external linkage is allowed (no T-R10); 'constructor',
//   'destructor', 'dllexport' and addresses are msil-ref-functions.c's.
// - RM9: tickets stay raw GCHandle numbers: GCHandle.Alloc as
//   'intptr_t f(Object ^)' ('ref;gch'), get_Target as 'Object ^f(intptr_t)'
//   ('this-gch;ref'), Free and FromIntPtr/ToIntPtr with intptr_t.
// - RM10: MC-1's handle rows are unchanged (a 'void *' or pointer typedef
//   handle beside a reference in one declaration, C strings for 'string'),
//   but a pointer to a cli_type struct is never a handle: T-R6 alone.
// - A 'msil_catch' with a reference result (MX2's zero is ldnull) and with
//   reference parameters.
// - T-M22: an asm label starting with '<castclass>', the cast stubs' symbol
//   (RM8), is reserved like '__msil_mc.' (the stub form of the text), on a
//   function declaration, a definition, a variable and a managed call, also
//   behind '\01'; labels that merely contain it, or differ, are not.
// Every text is pinned whole ('{{^}}' text '{{$}}'); CodeGen/MSIL/
// ref-managed-calls.c checks the conversion codes in the IR.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -std=c17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c23 -fsyntax-only -verify %s

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define NULL ((void *)0)

typedef __INTPTR_TYPE__ intptr_t;
typedef __CHAR16_TYPE__ char16_t;
typedef struct __dn_object *dn_handle; // an MC-1 handle type

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE(" System.String ") Text Text; // another spelling of String
typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.IO.Stream") Stream Stream;
typedef struct CLI_TYPE("System.IO.FileStream") FileStream FileStream;
typedef struct CLI_TYPE("System.Text.StringBuilder") StringBuilder StringBuilder;
typedef struct CLI_TYPE("System.IDisposable") IDisposable IDisposable;
typedef struct CLI_TYPE("Ns.Outer/Inner") Inner Inner;
typedef struct CLI_TYPE("Ns.Holder") Holder Holder;
typedef struct CLI_TYPE("uint8[]") Bytes Bytes;
typedef struct CLI_TYPE("string[]") Strings Strings;
typedef struct CLI_TYPE("class System.String[]") Strings2 Strings2;
typedef struct CLI_TYPE("object[]") Objects Objects;
typedef struct CLI_TYPE("class System.IO.Stream[]") Streams Streams;

//===----------------------------------------------------------------------===//
// Accepted forms (no diagnostics).
//===----------------------------------------------------------------------===//

// 'ref' parameters and results: string, object, class, arrays, and the
// 'class System.String' and 'class System.Object' spellings.
String ^r_concat(String ^a, Text ^b) __attribute__((msil_method("System.String", "Concat", "string(string, string)")));
String ^r_concat_class(String ^a, String ^b) __attribute__((msil_method("System.String", "Concat", "class System.String(class System.String, class System.String)")));
void r_keep(Object ^o) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)")));
void r_keep_class(Object ^o) __attribute__((msil_method("System.GC", "KeepAlive", "void(class System.Object)")));
Stream ^r_stdout(void) __attribute__((msil_method("System.Console", "OpenStandardOutput", "class System.IO.Stream()")));
String ^r_machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));
Strings ^r_args(void) __attribute__((msil_method("System.Environment", "GetCommandLineArgs", "string[]()")));
Strings2 ^r_args_class(void) __attribute__((msil_method("System.Environment", "GetCommandLineArgs", "class System.String[]()")));
Bytes ^r_bytes(int v) __attribute__((msil_method("System.BitConverter", "GetBytes", "uint8[](int32)")));
String ^r_join(String ^sep, Strings ^values) __attribute__((msil_method("System.String", "Join", "string(string, string[])")));
void r_copy(Bytes ^src, int start, void *dst, int n) __attribute__((msil_method("System.Runtime.InteropServices.Marshal", "Copy", "void(uint8[], int32, native int, int32)")));
Object ^r_objects(Objects ^a, int i) __attribute__((msil_method("Ns.Arrays", "At", "object(object[], int32)")));
// '.ctor' of a class and of arrays (newarr) with a reference result.
StringBuilder ^r_new_builder(void) __attribute__((msil_method("System.Text.StringBuilder", ".ctor", "instance void()")));
StringBuilder ^r_new_builder_s(String ^s) __attribute__((msil_method("System.Text.StringBuilder", ".ctor", "instance void(string)")));
Bytes ^r_new_bytes(int n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int32)")));
Strings ^r_new_strings(int n) __attribute__((msil_method("string[]", ".ctor", "instance void(int32)")));
Objects ^r_new_objects(int n) __attribute__((msil_method("object[]", ".ctor", "instance void(int32)")));
Streams ^r_new_streams(int n) __attribute__((msil_method("class System.IO.Stream[]", ".ctor", "instance void(int32)")));
// Fields: a static field, and an instance field through 'this-ref'.
String ^r_empty(void) __attribute__((msil_method("System.String", "Empty", "field string")));
String ^r_name(Holder ^h) __attribute__((msil_method("Ns.Holder", "Name", "instance field string")));
int r_count(Holder ^h) __attribute__((msil_method("Ns.Holder", "Count", "instance field int32")));
// 'this-ref': a class, an interface, a nested type, String and Object.
StringBuilder ^r_append(StringBuilder ^sb, String ^s) __attribute__((msil_method("System.Text.StringBuilder", "Append", "instance class System.Text.StringBuilder(string)")));
void r_write(Stream ^s, Bytes ^b, int off, int n) __attribute__((msil_method("System.IO.Stream", "Write", "instance void(uint8[], int32, int32)")));
void r_dispose(IDisposable ^d) __attribute__((msil_method("System.IDisposable", "Dispose", "instance void()")));
int r_inner(Inner ^i) __attribute__((msil_method("Ns.Outer/Inner", "get_Value", "instance int32()")));
int r_length(Text ^s) __attribute__((msil_method("System.String", "get_Length", "instance int32()")));
String ^r_to_string(Object ^o) __attribute__((msil_method("System.Object", "ToString", "instance string()")));
int r_hash(Object ^o) __attribute__((msil_method("System.Object", "GetHashCode", "instance int32()")));
int r_index_of(String ^s, char16_t c, int start) __attribute__((msil_method("System.String", "IndexOf", "instance int32(char, int32)")));
// RM5: internal linkage too.
static String ^r_static(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
// msil_catch with a reference result and reference parameters.
String ^r_read_all(String ^path, int *err) __attribute__((msil_method("System.IO.File", "ReadAllText", "string(string)"), msil_catch("System.IO.FileNotFoundException", 2), msil_catch("System.IO.IOException", 5)));
String ^r_catch_this(Object ^o, int *err) __attribute__((msil_method("System.Object", "ToString", "instance string()"), msil_catch("System.NullReferenceException", 3)));
// RM9: tickets.
intptr_t t_alloc(Object ^o) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Alloc", "valuetype System.Runtime.InteropServices.GCHandle(object)")));
Object ^t_target(intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "get_Target", "instance object()")));
void t_free(intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Free", "instance void()")));
intptr_t t_from(intptr_t p) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "FromIntPtr", "valuetype System.Runtime.InteropServices.GCHandle(native int)")));
// RM10: MC-1's handle rows and C strings beside references.
void h_write(Stream ^s, dn_handle bytes, int off, int n) __attribute__((msil_method("System.IO.Stream", "Write", "instance void(uint8[], int32, int32)")));
void h_text(dn_handle writer, String ^text) __attribute__((msil_method("System.IO.TextWriter", "Write", "instance void(string)")));
void *h_copy_handle(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
String ^h_from_handle(void *s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
String ^h_from_utf8(const char *s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
// Redeclarations repeat or inherit the attribute.
String ^r_redecl(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
String ^r_redecl(String ^s);
String ^r_redecl(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));

// Calls, with C's own conversions at the call site.
int use(void) {
  int err = 0;
  String ^m = r_machine();
  StringBuilder ^sb = r_append(r_new_builder(), m);
  intptr_t ticket = t_alloc(m);
  Object ^back = t_target(ticket);
  t_free(ticket);
  r_keep(sb);
  r_keep_class(m);
  r_dispose((IDisposable ^)back);
  String ^text = r_read_all(r_concat(m, r_static(m)), &err);
  return r_length(text) + r_hash(sb) + r_inner((Inner ^)back) + r_index_of(m, u'x', 0) + err +
         (r_args() == r_args_class()) + (r_to_string(m) == NULL);
}

//===----------------------------------------------------------------------===//
// RM2: T-M3.
//===----------------------------------------------------------------------===//

void m_string_for_object(String ^o) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)"))); // expected-error-re {{{{^}}parameter 1 of 'm_string_for_object' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'object'{{$}}}}
Object ^m_object_for_string(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()"))); // expected-error-re {{{{^}}the result of 'm_object_for_string' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'string'{{$}}}}
String ^m_result_object(Object ^o) __attribute__((msil_method("Ns.Objects", "Same", "object(object)"))); // expected-error-re {{{{^}}the result of 'm_result_object' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'object'{{$}}}}
void m_base_for_derived(Stream ^s) __attribute__((msil_method("System.GC", "KeepAlive", "void(class System.IO.FileStream)"))); // expected-error-re {{{{^}}parameter 1 of 'm_base_for_derived' has C type 'Stream ^' (aka 'System.IO.Stream ^'), which cannot carry CIL type 'class System.IO.FileStream'{{$}}}}
long long m_derived_this(FileStream ^s, long long off, int origin) __attribute__((msil_method("System.IO.Stream", "Seek", "instance int64(int64, valuetype System.IO.SeekOrigin)"))); // expected-error-re {{{{^}}the 'this' reference of 'm_derived_this' has C type 'FileStream ^' (aka 'System.IO.FileStream ^'), which cannot carry CIL type 'class System.IO.Stream'{{$}}}}
int m_object_this(Object ^s) __attribute__((msil_method("System.String", "get_Length", "instance int32()"))); // expected-error-re {{{{^}}the 'this' reference of 'm_object_this' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'class System.String'{{$}}}}
String ^m_field_this(String ^s) __attribute__((msil_method("Ns.Holder", "Name", "instance field string"))); // expected-error-re {{{{^}}the 'this' reference of 'm_field_this' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'class Ns.Holder'{{$}}}}
long long m_value_this(Object ^d) __attribute__((msil_method("valuetype System.DateTime", "get_Ticks", "instance int64()"))); // expected-error-re {{{{^}}the 'this' reference of 'm_value_this' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'valuetype System.DateTime'{{$}}}}
void m_gchandle_this(Object ^h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Free", "instance void()"))); // expected-error-re {{{{^}}the 'this' reference of 'm_gchandle_this' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'valuetype System.Runtime.InteropServices.GCHandle'{{$}}}}
int m_array_this(Bytes ^b) __attribute__((msil_method("System.Array", "get_Length", "instance int32()"))); // expected-error-re {{{{^}}the 'this' reference of 'm_array_this' has C type 'Bytes ^' (aka 'uint8[] ^'), which cannot carry CIL type 'class System.Array'{{$}}}}
Bytes ^m_array_ctor(int n) __attribute__((msil_method("int32[]", ".ctor", "instance void(int32)"))); // expected-error-re {{{{^}}the result of 'm_array_ctor' has C type 'Bytes ^' (aka 'uint8[] ^'), which cannot carry CIL type 'int32[]'{{$}}}}
Strings ^m_array_element(void) __attribute__((msil_method("Ns.Arrays", "Get", "object[]()"))); // expected-error-re {{{{^}}the result of 'm_array_element' has C type 'Strings ^' (aka 'string[] ^'), which cannot carry CIL type 'object[]'{{$}}}}
void m_int32(String ^s) __attribute__((msil_method("System.Math", "Abs", "int32(int32)"))); // expected-error-re {{{{^}}parameter 1 of 'm_int32' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'int32'{{$}}}}
void m_native_int(String ^s) __attribute__((msil_method("Ns.N", "Take", "void(native int)"))); // expected-error-re {{{{^}}parameter 1 of 'm_native_int' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'native int'{{$}}}}
void m_bool(Object ^b) __attribute__((msil_method("Ns.N", "Take", "void(bool)"))); // expected-error-re {{{{^}}parameter 1 of 'm_bool' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'bool'{{$}}}}
void m_enum(Object ^e) __attribute__((msil_method("Ns.N", "Take", "void(valuetype Ns.Kind)"))); // expected-error-re {{{{^}}parameter 1 of 'm_enum' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'valuetype Ns.Kind'{{$}}}}
void m_pointer(String ^p) __attribute__((msil_method("Ns.N", "Take", "void(int32*)"))); // expected-error-re {{{{^}}parameter 1 of 'm_pointer' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'int32*'{{$}}}}
String ^m_void(void) __attribute__((msil_method("System.GC", "Collect", "void()"))); // expected-error-re {{{{^}}the result of 'm_void' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'void'{{$}}}}
Object ^m_gchandle_result(intptr_t p) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "FromIntPtr", "valuetype System.Runtime.InteropServices.GCHandle(native int)"))); // expected-error-re {{{{^}}the result of 'm_gchandle_result' has C type 'Object ^' (aka 'System.Object ^'), which cannot carry CIL type 'valuetype System.Runtime.InteropServices.GCHandle'{{$}}}}
intptr_t m_string_alloc(String ^o) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Alloc", "valuetype System.Runtime.InteropServices.GCHandle(object)"))); // expected-error-re {{{{^}}parameter 1 of 'm_string_alloc' has C type 'String ^' (aka 'System.String ^'), which cannot carry CIL type 'object'{{$}}}}

//===----------------------------------------------------------------------===//
// RM4: T-M4 and T-M12.
//===----------------------------------------------------------------------===//

int m_arity(String ^s, char16_t c, int *err) __attribute__((msil_method("System.String", "IndexOf", "instance int32(char, int32)"), msil_catch("System.ArgumentOutOfRangeException", 34))); // expected-error-re {{{{^}}'m_arity' has 3 parameters; 'msil_method' needs 4 (a 'this' reference, 2 CIL parameters and the 'msil_catch' error code){{$}}}}
String ^m_variadic(String ^s, ...) __attribute__((msil_method("System.String", "Copy", "string(string)"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
#if __STDC_VERSION__ < 202311L
String ^m_unprototyped() __attribute__((msil_method("System.Environment", "get_MachineName", "string()"))); // expected-error-re {{{{^}}'msil_method' is only allowed on a prototyped, non-variadic function other than 'main'{{$}}}}
#endif

//===----------------------------------------------------------------------===//
// RM10: a pointer to a class struct is never a handle.
//===----------------------------------------------------------------------===//

void m_star(String *s) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)"))); // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
Object *m_star_result(void) __attribute__((msil_method("Ns.Objects", "Get", "object()"))); // expected-error-re {{{{^}}'Object *' points into C memory, which cannot hold the .NET class 'Object'; a reference to it is written 'Object ^'{{$}}}}

//===----------------------------------------------------------------------===//
// T-M22: '<castclass>' labels.
//===----------------------------------------------------------------------===//

int l_decl(int x) __asm__("<castclass>x");                       // expected-error-re {{{{^}}asm label '<castclass>x' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int l_def(int x) __asm__("<castclass>.__uniq.1");                // expected-error-re {{{{^}}asm label '<castclass>.__uniq.1' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int l_def(int x) { return x; }
int l_var __asm__("<castclass>") = 0;                             // expected-error-re {{{{^}}asm label '<castclass>' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int l_escaped(int x) __asm__("\01<castclass>.y");                // expected-error-re {{{{^}}asm label '<castclass>.y' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
String ^l_managed(String ^s) __asm__("<castclass>.__uniq.42") __attribute__((msil_method("System.String", "Copy", "string(string)"))); // expected-error-re {{{{^}}asm label '<castclass>.__uniq.42' is reserved for the MSIL toolchain (a managed-call stub){{$}}}}
int l_ok1(int x) __asm__("<castclas>x");
int l_ok2(int x) __asm__("x<castclass>");
int l_ok3(int x) __asm__("castclass");
int l_ok4(int x) __asm__("<cast>");
