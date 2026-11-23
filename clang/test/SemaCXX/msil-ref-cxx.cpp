// .NET references in C++ (ImplementationPlan.md 5.21.3 RC6, 5.21.2 RT7 and
// 5.21.14 RV9, milestone R-a). R-a is C only: C++ accepts the 'cli_type'
// attribute, so a C header that declares class structs and managed calls with
// MC-1's handles compiles as C++, but the caret over a class struct is T-R5,
// "yet" (R-a2 lifts it, 5.21.16 RN1), in every position: variables,
// parameters, results, typedefs and alias declarations, template arguments
// and defaults, sizeof, C-style and static_cast casts, lambda parameters and
// class members, also with -fblocks. A caret over anything else that is not a
// function type is T-R4 as in C, a dependent 'T ^' included (5.21.16 RN2 is
// R-a2's); a function pointee stays a block pointer. The permanent rules on
// the class struct hold in C++ too: T-R6 for a pointer to it (RS10), T-R7 for
// it by value (RS11), and T-R3 for a body (RT5, the BODY runs). Native C++
// ignores the attribute and keeps clang's block errors (NATIVE).
// Every text is pinned whole ('{{^}}' text '{{$}}').

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++17 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++20 -fsyntax-only -verify=expected,noblocks %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++17 -fblocks -fsyntax-only -verify=expected %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -std=c++20 -fblocks -fsyntax-only -verify=expected %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=c++17 -fsyntax-only -DBODY -verify=body %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=c++17 -fsyntax-only -DBODY -verify=body %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fsyntax-only -DNATIVE -verify=native %s

#if defined(NATIVE)

#if __has_attribute(cli_type) || __has_cpp_attribute(clang::cli_type)
#error "cli_type exists only on MSIL triples"
#endif
struct __attribute__((cli_type("System.String"))) String; // native-warning-re {{{{^}}unknown attribute 'cli_type' ignored{{$}}}}
// native-error-re@+2 {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}
// native-error-re@+1 {{{{^}}block pointer to non-function type is invalid{{$}}}}
String ^n_ref;

#elif defined(BODY)

// body-error-re@+1 {{{{^}}'cli_type' struct 'B' names the .NET class 'System.String', which has no layout in C memory; a class struct has no body{{$}}}}
struct __attribute__((cli_type("System.String"))) B { int x; };

#else // the MSIL triples

#if !__has_attribute(cli_type) || !__has_cpp_attribute(clang::cli_type)
#error "cli_type is an MSIL attribute"
#endif

// RT7: the attribute in C++, in both spellings, in 'extern "C"' (a C header
// with MC-1's handles), at namespace and block scope.
extern "C" {
typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type("System.IO.Stream"))) Stream Stream;
typedef struct __dn_object *dn_handle;
dn_handle c_stdout(void) __attribute__((msil_method("System.Console", "OpenStandardOutput", "class System.IO.Stream()")));
void *c_copy(void *s) __attribute__((msil_method("System.String", "Copy", "string(string)")));
}
struct [[clang::cli_type("System.Object")]] Object;
namespace ns {
struct [[clang::cli_type("System.Text.StringBuilder")]] StringBuilder;
}
void block_scope() { struct __attribute__((cli_type("System.Uri"))) Uri; }
struct Plain;
template <typename T> void tf(T t);
template <typename T> struct Holder { T value; };

// RC6: T-R5 at every caret over a class struct.
String ^g_variable;                    // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
static void f_param(String ^s);       // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
static Object ^f_result();            // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
typedef String ^SRef;                 // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
using ORef = Object ^;                // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
struct Member { ns::StringBuilder ^m; }; // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
template <typename T = Stream ^> struct Defaulted {}; // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
static void uses() {
  tf<String ^>(nullptr);              // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  (void)sizeof(String ^);             // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  (void)(String ^)nullptr;            // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  (void)static_cast<Object ^>(nullptr); // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  auto l = [](String ^s) { return 0; }; // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  Holder<Object ^> h;                 // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
  String ^local;                      // expected-error-re {{{{^}}.NET references in C++ are not supported for MSIL yet{{$}}}}
}

// RC2: T-R4 for every other caret that is not over a function type, a
// dependent pointee included; a function pointee is a block pointer.
int ^t_int;                            // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'int' is not one{{$}}}}
Plain ^t_plain;                        // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'Plain' is not one{{$}}}}
template <typename T> struct Dependent { T ^m; }; // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'T' is not one{{$}}}}
template <typename T> void dependent(T ^p);       // expected-error-re {{{{^}}'^' needs a struct with the 'cli_type' attribute of a .NET class; 'T' is not one{{$}}}}
void (^t_block)(void);                 // noblocks-error-re {{{{^}}blocks support disabled - compile with -fblocks or pick a deployment target that supports them{{$}}}}

// RS10, RS11: permanent rules on the class struct.
String *p_pointer;                     // expected-error-re {{{{^}}'String *' points into C memory, which cannot hold the .NET class 'String'; a reference to it is written 'String ^'{{$}}}}
extern String v_extern;                // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
String v_result();                     // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}
void v_param(String a);                // expected-error-re {{{{^}}the .NET class 'String' has no value in C; a reference to it is written 'String ^'{{$}}}}

// Subjects: structs only, as in C.
union __attribute__((cli_type("System.String"))) u_union; // expected-error-re {{{{^}}'cli_type' attribute only applies to structs{{$}}}}

#endif
