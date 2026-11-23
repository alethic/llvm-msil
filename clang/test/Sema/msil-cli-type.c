// The 'cli_type' attribute in C (ImplementationPlan.md 5.21.2 RT1-RT8 and
// 5.21.14 RV1, milestone R-a): __attribute__((cli_type(N))) and
// [[clang::cli_type(N)]] mark a struct as the C name of a .NET reference type,
// so that 'T ^' can hold a reference to it (msil-ref-type.c and the other
// msil-ref-*.c tests cover the references themselves).
// - RT1: the attribute exists only on the MSIL object-format triples (the
//   eight host triples, msil64-unknown-dotnet and msil32-unknown-dotnet,
//   TargetMSIL): __has_attribute(cli_type) and
//   __has_c_attribute(clang::cli_type) are 1 there and 0 on native x86_64
//   Linux and wasm32, which ignore it with clang's 'unknown attribute ...
//   ignored' warning (the NATIVE runs). Subjects: structs only (ErrorDiag),
//   never a union, an enum, a variable or a function; one string argument
//   (the fake second argument, which Sema fills with the key, is never
//   written); clang's own texts for the argument errors.
// - RT2: the TYPE grammar of MC-1 (5.20.2 MA1) minus 'valuetype': "Ns.Name",
//   "Ns.Outer/Inner", a name without a namespace, and "<elem>[]" with a
//   listing primitive, 'string', 'object' or 'class Ns.T' element. Each
//   refusal is T-R1 with its reason, pinned whole: 'valuetype' (also as an
//   array element), a listing primitive alone ('string', 'object', 'int32',
//   'bool', 'native int'), a resolution scope (leading, also on an array's
//   element, after 'class ' too, and alone), a pointer (also of an array),
//   'class ' outside an array, and MC-1's own refusals (empty, byref,
//   generic, an invalid name, an unknown array element).
// - RT3: the key folds every level and is the canonical listing text: the
//   AST dump (KEYS) shows it as the attribute's second argument, and two
//   spellings of one class or array are one type, while different keys,
//   case included, and a nested type against a namespace of the same dotted
//   name, are different types (_Static_assert with
//   __builtin_types_compatible_p on the references).
// - RT4: on the struct's first declaration only; a later declaration
//   repeats the key (in any spelling) or omits it and inherits it; anything
//   else is T-R2 with the note 'first declared here' at the first
//   declaration, also when the first declaration is the implicit one of an
//   elaborated type specifier ('struct Q *q;'); a refused attribute (T-R1)
//   is never attached, so it counts as no attribute. A block-scope struct is
//   a new struct (RT7). Two cli_type attributes on one declaration: clang's
//   duplicate-attribute texts. The CHAIN run pins the note at the first of
//   several declarations that repeat the key ('@-N').
// - RT5: a cli_type struct never has a body (T-R3, the BODY run): a body on
//   the first declaration, on a later declaration that inherits the
//   attribute, and on an anonymous struct; the .NET class is named by its
//   display name (RC4: 'System.String', 'string[]' for the key of
//   'class System.String[]', 'class System.IO.Stream[]').
// - The PRINT run: -ast-print keeps both spellings with exactly the written
//   name.
// Every text is pinned whole with a regex directive anchored at both ends
// ('{{^}}' text '{{$}}'), as clang/test/Sema/attr-msil-method.c (MC-1) does.

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
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c17 -fsyntax-only -DNATIVE -verify=native %s
// RUN: %clang_cc1 -triple wasm32-unknown-unknown -std=c17 -fsyntax-only -DNATIVE -verify=native %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DCHAIN -verify=chain %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DCHAIN -verify=chain %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DCHAIN -verify=chain %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -fsyntax-only -DBODY -verify=body %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -fsyntax-only -DBODY -verify=body %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c17 -fsyntax-only -DBODY -verify=body %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c17 -DKEYS -ast-dump %s \
// RUN:   | FileCheck %s --check-prefix=KEYS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c17 -DPRINT -ast-print %s \
// RUN:   | FileCheck %s --check-prefix=PRINT

#if defined(NATIVE)

#if __has_attribute(cli_type)
#error "cli_type exists only on MSIL triples"
#endif
#if __has_c_attribute(clang::cli_type)
#error "cli_type exists only on MSIL triples"
#endif

struct __attribute__((cli_type("System.String"))) n_gnu; // native-warning-re {{{{^}}unknown attribute 'cli_type' ignored{{$}}}}
struct [[clang::cli_type("System.String")]] n_c23;      // native-warning-re {{{{^}}unknown attribute 'clang::cli_type' ignored{{$}}}}

#elif defined(CHAIN)

// The first of several declarations that repeat the key (in three
// spellings) and one that inherits it: the note of the 'differs' form is at
// the first declaration.
struct __attribute__((cli_type("string[]"))) chain; // chain-note-re {{{{^}}first declared here{{$}}}}
struct __attribute__((cli_type("class System.String[]"))) chain;
struct __attribute__((cli_type("string []"))) chain;
struct chain;
// chain-error-re@+1 {{{{^}}'cli_type' on this declaration of 'chain' differs from its first declaration{{$}}}}
struct __attribute__((cli_type("object[]"))) chain;

#elif defined(BODY)

// T-R3: a class struct never gets a body (RT5), wherever the body is
// written.
// body-error-re@+1 {{{{^}}'cli_type' struct 'b_first' names the .NET class 'System.String', which has no layout in C memory; a class struct has no body{{$}}}}
struct __attribute__((cli_type("System.String"))) b_first { int x; };
struct __attribute__((cli_type("System.Object"))) b_later;
// body-error-re@+1 {{{{^}}'cli_type' struct 'b_later' names the .NET class 'System.Object', which has no layout in C memory; a class struct has no body{{$}}}}
struct b_later { int y; };
// body-error-re@+1 {{{{^}}'cli_type' struct 'b_array' names the .NET class 'string[]', which has no layout in C memory; a class struct has no body{{$}}}}
struct __attribute__((cli_type("class System.String[]"))) b_array { char c; };
// body-error-re@+1 {{{{^}}'cli_type' struct 'b_streams' names the .NET class 'class System.IO.Stream[]', which has no layout in C memory; a class struct has no body{{$}}}}
struct __attribute__((cli_type("class System.IO.Stream[]"))) b_streams { long l; };
// body-error-re@+1 {{{{^}}'cli_type' struct '{{.+}}' names the .NET class 'Ns.Outer/Inner', which has no layout in C memory; a class struct has no body{{$}}}}
typedef struct __attribute__((cli_type("Ns.Outer/Inner"))) { int z; } b_anon;
// A body inside a function.
void b_local(void) {
  // body-error-re@+1 {{{{^}}'cli_type' struct 'b_in_function' names the .NET class 'System.Text.StringBuilder', which has no layout in C memory; a class struct has no body{{$}}}}
  struct __attribute__((cli_type("System.Text.StringBuilder"))) b_in_function { int w; };
}

#elif defined(KEYS)

// RT3: the key is the canonical listing text, folded at every level.
// KEYS:      RecordDecl {{.*}} struct k_string
// KEYS-NEXT: CLITypeAttr {{.*}} "System.String" "string"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_trimmed
// KEYS-NEXT: CLITypeAttr {{.*}} " System.String " "string"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_object
// KEYS-NEXT: CLITypeAttr {{.*}} "System.Object" "object"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_class
// KEYS-NEXT: CLITypeAttr {{.*}} "System.IO.Stream" "class System.IO.Stream"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_interface
// KEYS-NEXT: CLITypeAttr {{.*}} "System.IDisposable" "class System.IDisposable"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_nested
// KEYS-NEXT: CLITypeAttr {{.*}} "Ns.Outer/Inner" "class Ns.Outer/Inner"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_global
// KEYS-NEXT: CLITypeAttr {{.*}} "Foo" "class Foo"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_strings
// KEYS-NEXT: CLITypeAttr {{.*}} "string[]" "string[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_strings_class
// KEYS-NEXT: CLITypeAttr {{.*}} "class System.String[]" "string[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_strings_space
// KEYS-NEXT: CLITypeAttr {{.*}} "string []" "string[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_objects
// KEYS-NEXT: CLITypeAttr {{.*}} "object[]" "object[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_objects_class
// KEYS-NEXT: CLITypeAttr {{.*}} "class System.Object[]" "object[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_bytes
// KEYS-NEXT: CLITypeAttr {{.*}} "uint8[]" "uint8[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_nints
// KEYS-NEXT: CLITypeAttr {{.*}} "native int[]" "native int[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_streams
// KEYS-NEXT: CLITypeAttr {{.*}} "class System.IO.Stream[]" "class System.IO.Stream[]"{{$}}
// KEYS:      RecordDecl {{.*}} struct k_inners
// KEYS-NEXT: CLITypeAttr {{.*}} "class Ns.Outer/Inner[]" "class Ns.Outer/Inner[]"{{$}}
struct __attribute__((cli_type("System.String"))) k_string;
struct __attribute__((cli_type(" System.String "))) k_trimmed;
struct __attribute__((cli_type("System.Object"))) k_object;
struct __attribute__((cli_type("System.IO.Stream"))) k_class;
struct __attribute__((cli_type("System.IDisposable"))) k_interface;
struct __attribute__((cli_type("Ns.Outer/Inner"))) k_nested;
struct __attribute__((cli_type("Foo"))) k_global;
struct __attribute__((cli_type("string[]"))) k_strings;
struct __attribute__((cli_type("class System.String[]"))) k_strings_class;
struct __attribute__((cli_type("string []"))) k_strings_space;
struct __attribute__((cli_type("object[]"))) k_objects;
struct __attribute__((cli_type("class System.Object[]"))) k_objects_class;
struct __attribute__((cli_type("uint8[]"))) k_bytes;
struct __attribute__((cli_type("native int[]"))) k_nints;
struct __attribute__((cli_type("class System.IO.Stream[]"))) k_streams;
struct __attribute__((cli_type("class Ns.Outer/Inner[]"))) k_inners;

#elif defined(PRINT)

// PRINT:      {{^}}struct __attribute__((cli_type("System.String"))) p_gnu;{{$}}
// PRINT-NEXT: {{^}}struct {{\[\[}}clang::cli_type("System.Object")]] p_c23;{{$}}
// PRINT-NEXT: {{^}}struct __attribute__((cli_type("class System.String[]"))) p_array;{{$}}
// PRINT-NEXT: {{^}}struct __attribute__((cli_type(" Ns.Outer/Inner "))) p_nested;{{$}}
struct __attribute__((cli_type("System.String"))) p_gnu;
struct [[clang::cli_type("System.Object")]] p_c23;
struct __attribute__((cli_type("class System.String[]"))) p_array;
struct __attribute__((cli_type(" Ns.Outer/Inner "))) p_nested;

#else // the MSIL triples

#if !__has_attribute(cli_type)
#error "cli_type is an MSIL attribute"
#endif
#if !__has_c_attribute(clang::cli_type)
#error "cli_type is an MSIL attribute"
#endif

#define CLI_TYPE(N) __attribute__((cli_type(N)))
#define SAME(A, B) _Static_assert(__builtin_types_compatible_p(A, B), #A " and " #B " are one type")
#define DIFFERENT(A, B) _Static_assert(!__builtin_types_compatible_p(A, B), #A " and " #B " differ")
// The same checks inside a function body, as expressions (a negative array
// size is an error); msil-ref-ops.c covers '_Static_assert' in a body.
#define SAME_IN_BODY(A, B) (void)sizeof(char[__builtin_types_compatible_p(A, B) ? 1 : -1])
#define DIFFERENT_IN_BODY(A, B) (void)sizeof(char[__builtin_types_compatible_p(A, B) ? -1 : 1])

//===----------------------------------------------------------------------===//
// Accepted forms (no diagnostics).
//===----------------------------------------------------------------------===//

// RT1: both spellings, before and after 'struct', through a typedef of the
// struct, and at block scope (RT7).
struct __attribute__((cli_type("System.String"))) s_gnu;
struct [[clang::cli_type("System.String")]] s_c23;
typedef struct CLI_TYPE("System.Object") Object Object;
typedef struct CLI_TYPE("System.String") String String;
struct CLI_TYPE("System.String") __attribute__((unused)) s_more_attrs;

// RT6: classes, interfaces, nested types, a type without a namespace,
// System.String, System.Object, and arrays of every element kind.
typedef struct CLI_TYPE("System.IO.Stream") Stream Stream;
typedef struct CLI_TYPE("System.IDisposable") IDisposable IDisposable;
typedef struct CLI_TYPE("Ns.Outer/Inner") Inner Inner;
typedef struct CLI_TYPE("Ns.Outer.Inner") NsInner NsInner;
typedef struct CLI_TYPE("Foo") Foo Foo;
typedef struct CLI_TYPE("uint8[]") Bytes Bytes;
typedef struct CLI_TYPE("native int[]") NInts NInts;
typedef struct CLI_TYPE("string[]") Strings Strings;
typedef struct CLI_TYPE("object[]") Objects Objects;
typedef struct CLI_TYPE("class System.IO.Stream[]") Streams Streams;
typedef struct CLI_TYPE("class Ns.Outer/Inner[]") Inners Inners;

// RT3: other spellings of the same keys.
typedef struct CLI_TYPE(" System.String ") Text Text;
typedef struct CLI_TYPE("System.Object") Obj2 Obj2;
typedef struct CLI_TYPE("class System.String[]") Strings2 Strings2;
typedef struct CLI_TYPE("string []") Strings3 Strings3;
typedef struct CLI_TYPE("class System.Object[]") Objects2 Objects2;
typedef struct CLI_TYPE("System.IO.Stream") Stream2 Stream2;
SAME(String ^, Text ^);
SAME(String ^, struct s_gnu ^);
SAME(struct s_gnu ^, struct s_c23 ^);
SAME(Object ^, Obj2 ^);
SAME(Strings ^, Strings2 ^);
SAME(Strings ^, Strings3 ^);
SAME(Objects ^, Objects2 ^);
SAME(Stream ^, Stream2 ^);
SAME(String ^const, Text ^);

// Different keys are different types: other classes, a string against an
// array of strings, an object against an array of objects, an array of
// streams against a stream, a nested type against a namespace of the same
// dotted name, and names that differ only in case.
typedef struct CLI_TYPE("system.string") LowerString LowerString;
DIFFERENT(String ^, Object ^);
DIFFERENT(String ^, Stream ^);
DIFFERENT(String ^, Strings ^);
DIFFERENT(Object ^, Objects ^);
DIFFERENT(Stream ^, Streams ^);
DIFFERENT(Inner ^, NsInner ^);
DIFFERENT(Inner ^, Inners ^);
DIFFERENT(Strings ^, Objects ^);
DIFFERENT(Bytes ^, NInts ^);
DIFFERENT(String ^, LowerString ^);
DIFFERENT(IDisposable ^, Stream ^);
DIFFERENT(Foo ^, Object ^);

// RT4: a later declaration repeats the key, in any spelling, or omits it.
struct CLI_TYPE("string[]") r_arrays;
struct CLI_TYPE("class System.String[]") r_arrays;
struct CLI_TYPE(" string [] ") r_arrays;
struct r_arrays;
struct CLI_TYPE("string[]") r_arrays;
typedef struct r_arrays r_arrays_t;
SAME(struct r_arrays ^, Strings ^);
SAME(r_arrays_t ^, Strings ^);
struct String;
struct CLI_TYPE("System.String") String;

// RT7: a block-scope struct is a struct of its own; one with the name of a
// file-scope struct is a new first declaration.
struct p_plain;
void block_scope(void) {
  struct CLI_TYPE("System.Text.StringBuilder") StringBuilder;
  struct CLI_TYPE("System.Object") p_plain;
  struct CLI_TYPE("System.Object") String;
  SAME_IN_BODY(struct p_plain ^, Object ^);
  SAME_IN_BODY(struct String ^, Object ^);
  DIFFERENT_IN_BODY(struct String ^, Text ^);
  DIFFERENT_IN_BODY(struct StringBuilder ^, Object ^);
}

//===----------------------------------------------------------------------===//
// RT4: T-R2.
//===----------------------------------------------------------------------===//

// A different key on a later declaration.
struct CLI_TYPE("System.String") d_differs; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' on this declaration of 'd_differs' differs from its first declaration{{$}}}}
struct CLI_TYPE("System.Object") d_differs;
// After the error the struct keeps its first key.
SAME(struct d_differs ^, String ^);
// Case is part of the key.
struct CLI_TYPE("System.String") d_case; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' on this declaration of 'd_case' differs from its first declaration{{$}}}}
struct CLI_TYPE("system.string") d_case;
// An array against its element.
struct CLI_TYPE("string[]") d_array; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' on this declaration of 'd_array' differs from its first declaration{{$}}}}
struct CLI_TYPE("System.String") d_array;

// The attribute first written on a later declaration.
struct m_later; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' must appear on the first declaration of 'm_later'{{$}}}}
struct CLI_TYPE("System.String") m_later;
// The first declaration is the elaborated type specifier of a pointer
// declaration (a pointer to a struct that is not a class).
struct m_implicit *m_implicit_ptr; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' must appear on the first declaration of 'm_implicit'{{$}}}}
struct CLI_TYPE("System.String") m_implicit;
// A typedef of the plain struct comes first.
typedef struct m_typedef m_typedef_t; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' must appear on the first declaration of 'm_typedef'{{$}}}}
struct CLI_TYPE("System.String") m_typedef;
// A refused attribute is not attached, so a later valid one is not on the
// first declaration.
// expected-error-re@+1 {{{{^}}'cli_type' type 'valuetype System.DateTime' is not valid: value types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("valuetype System.DateTime") m_refused; // expected-note-re {{{{^}}first declared here{{$}}}}
// expected-error-re@+1 {{{{^}}'cli_type' must appear on the first declaration of 'm_refused'{{$}}}}
struct CLI_TYPE("System.DateTime") m_refused;
// A refused attribute on a redeclaration: T-R1 alone, and the struct keeps
// its key.
struct CLI_TYPE("System.String") m_keeps;
// expected-error-re@+1 {{{{^}}'cli_type' type 'string' is not valid: 'string' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
struct CLI_TYPE("string") m_keeps;
SAME(struct m_keeps ^, String ^);

// Two cli_type attributes on one declaration: clang's duplicate texts.
// expected-warning-re@+1 {{{{^}}attribute 'cli_type' is already applied{{$}}}}
struct __attribute__((cli_type("System.String"), cli_type("System.String"))) dup_same;
// expected-error-re@+2 {{{{^}}attribute 'cli_type' cannot appear more than once on a declaration{{$}}}}
// expected-note-re@+1 {{{{^}}previous attribute is here{{$}}}}
struct __attribute__((cli_type("System.String"), cli_type("System.Object"))) dup_differs;

//===----------------------------------------------------------------------===//
// RT2: T-R1.
//===----------------------------------------------------------------------===//

// Value types (also as an array element).
struct CLI_TYPE("valuetype System.DateTime") v_valuetype;              // expected-error-re {{{{^}}'cli_type' type 'valuetype System.DateTime' is not valid: value types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("valuetype Ns.Kind") v_enum;                          // expected-error-re {{{{^}}'cli_type' type 'valuetype Ns.Kind' is not valid: value types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("valuetype System.DateTime[]") v_valuetype_array;    // expected-error-re {{{{^}}'cli_type' type 'valuetype System.DateTime[]' is not valid: value types are not supported for MSIL yet{{$}}}}
// Listing primitives alone.
struct CLI_TYPE("string") v_string;          // expected-error-re {{{{^}}'cli_type' type 'string' is not valid: 'string' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
struct CLI_TYPE("object") v_object;          // expected-error-re {{{{^}}'cli_type' type 'object' is not valid: 'object' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
struct CLI_TYPE("int32") v_int32;            // expected-error-re {{{{^}}'cli_type' type 'int32' is not valid: 'int32' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
struct CLI_TYPE("bool") v_bool;              // expected-error-re {{{{^}}'cli_type' type 'bool' is not valid: 'bool' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
struct CLI_TYPE("native int") v_native_int;  // expected-error-re {{{{^}}'cli_type' type 'native int' is not valid: 'native int' is a signature type; name the type by its full name, for example 'System.String'{{$}}}}
// A resolution scope, also on an array's element and alone.
struct CLI_TYPE("[mscorlib]System.String") v_scope;   // expected-error-re {{{{^}}'cli_type' type '[mscorlib]System.String' is not valid: a type is named without a resolution scope{{$}}}}
struct CLI_TYPE("[mscorlib]System.String[]") v_scope_array; // expected-error-re {{{{^}}'cli_type' type '[mscorlib]System.String[]' is not valid: a type is named without a resolution scope{{$}}}}
struct CLI_TYPE("class [x]Ns.T[]") v_scope_class_array; // expected-error-re {{{{^}}'cli_type' type 'class [x]Ns.T[]' is not valid: a type is named without a resolution scope{{$}}}}
struct CLI_TYPE("[mscorlib]") v_scope_alone;          // expected-error-re {{{{^}}'cli_type' type '[mscorlib]' is not valid: a type is named without a resolution scope{{$}}}}
// Pointers.
struct CLI_TYPE("System.String*") v_pointer;          // expected-error-re {{{{^}}'cli_type' type 'System.String*' is not valid: a pointer type is not a .NET reference type{{$}}}}
struct CLI_TYPE("string*") v_string_pointer;          // expected-error-re {{{{^}}'cli_type' type 'string*' is not valid: a pointer type is not a .NET reference type{{$}}}}
struct CLI_TYPE("int32[]*") v_array_pointer;          // expected-error-re {{{{^}}'cli_type' type 'int32[]*' is not valid: a pointer type is not a .NET reference type{{$}}}}
// 'class ' outside an array.
struct CLI_TYPE("class System.String") v_class;       // expected-error-re {{{{^}}'cli_type' type 'class System.String' is not valid: a reference type is named without 'class'{{$}}}}
struct CLI_TYPE("class System.IO.Stream") v_class2;   // expected-error-re {{{{^}}'cli_type' type 'class System.IO.Stream' is not valid: a reference type is named without 'class'{{$}}}}
// MC-1's own refusals.
struct CLI_TYPE("") v_empty;                          // expected-error-re {{{{^}}'cli_type' type '' is not valid: the type is empty{{$}}}}
struct CLI_TYPE("System.String&") v_byref;            // expected-error-re {{{{^}}'cli_type' type 'System.String&' is not valid: byref types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("string[]&") v_array_byref;           // expected-error-re {{{{^}}'cli_type' type 'string[]&' is not valid: byref types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("List`1") v_generic;                  // expected-error-re {{{{^}}'cli_type' type 'List`1' is not valid: generic types are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("Ns.Generic<int32>") v_instance;      // expected-error-re {{{{^}}'cli_type' type 'Ns.Generic<int32>' is not valid: generic instantiations are not supported for MSIL yet{{$}}}}
struct CLI_TYPE("<Module>") v_module;                 // expected-error-re {{{{^}}'cli_type' type '<Module>' is not valid: '<Module>' is not a valid type name{{$}}}}
struct CLI_TYPE("System.Int32[]") v_element;          // expected-error-re {{{{^}}'cli_type' type 'System.Int32[]' is not valid: unknown type 'System.Int32'{{$}}}}

//===----------------------------------------------------------------------===//
// RT1: subjects and arguments (clang's texts).
//===----------------------------------------------------------------------===//

union CLI_TYPE("System.String") u_union;                    // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}
enum CLI_TYPE("System.String") e_enum { e_enum_value };      // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}
int CLI_TYPE("System.String") v_variable;                   // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}
void f_function(void) CLI_TYPE("System.String");            // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}
typedef int t_typedef CLI_TYPE("System.String");            // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}
struct __attribute__((cli_type)) a_none;                    // expected-error-re {{{{^}}'cli_type' attribute takes one argument{{$}}}}
struct __attribute__((cli_type("System.String", "string"))) a_two; // expected-error-re {{{{^}}'cli_type' attribute takes one argument{{$}}}}
struct __attribute__((cli_type(1))) a_int;                  // expected-error-re {{{{^}}expected string literal as argument of 'cli_type' attribute{{$}}}}

#endif
