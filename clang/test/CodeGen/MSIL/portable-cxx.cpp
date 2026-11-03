// C++ on the portable triples (ImplementationPlan.md 5.18.2 PC1 and PC2,
// milestone P-1a; the exit criterion 'compiles C (and freestanding C++) to IL
// and links'): MSILPortableTargetInfo uses the GenericItanium C++ ABI, so
// - functions, members and template instantiations get Itanium mangled
//   names, and the wasm-style type spellings (PC2: long is 'l', size_t is
//   unsigned long, 'm') give the same names on msil64 and msil32;
// - a namespace-scope object with a constructor gets a dynamic initializer,
//   __cxx_global_var_init, called from the translation unit's
//   _GLOBAL__sub_I_ function, which llvm.global_ctors lists;
// - a class with a virtual function has an Itanium vtable, _ZTV<class>;
// - an extern "C" function, main included, keeps its C name.
// Freestanding C++ without exceptions or RTTI: cc1 enables no exceptions by
// default, and -fno-rtti leaves out the type_info objects (_ZTI).
// clang/test/Driver/msil-dotnet.c checks the driver's jobs for a .cpp input.

// REQUIRES: msil-registered-target

// RUN: %clang_cc1 -triple msil64-unknown-dotnet -x c++ -emit-llvm -fno-rtti -Wno-main -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,P64 -DL=i64 --implicit-check-not=_ZTI \
// RUN:     --implicit-check-not=__gxx_personality
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -x c++ -emit-llvm -fno-rtti -Wno-main -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,P32 -DL=i32 --implicit-check-not=_ZTI \
// RUN:     --implicit-check-not=__gxx_personality

// In the IL, every function is a method under its mangled name, the vtable's
// slot is filled with the address of C::n, and <Module>::.cctor runs the
// translation unit's initializers after the guard and the data image; main
// is the entry point's callee.
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -x c++ -S -fno-rtti -Wno-main -o %t.il %s
// RUN: FileCheck %s --check-prefix=IL --input-file=%t.il
// RUN: %if ilasm %{ %ilasm-verify -output=%t.dll %t.il %}
// IL-DAG: {{^}}.method assembly static void '__cxx_global_var_init'() cil managed{{$}}
// IL-DAG: {{^}}.method public static {{.*}} '_Z5use_tl'({{.*}}) cil managed{{$}}
// IL-DAG: {{^}}.method public static {{.*}} '_Z1tIlET_S0_'({{.*}}) cil managed{{$}}
// IL-DAG: {{^}}.method public static {{.*}} '_ZN1C1nEv'(native int) cil managed{{$}}
// IL-DAG: {{^}}.method public static int32 'c_entry'(int32) cil managed{{$}}
// IL-DAG: {{^}}.field {{.*}}native int '_ZTV1C'{{( //.*)?}}{{$}}
// IL-LABEL: {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
// IL:       {{^}}  call uint8[] [mscorlib]System.BitConverter::GetBytes(int32){{$}}
// IL:       {{^}}  ldftn int32 '_ZN1C1nEv'(native int){{$}}
// IL:       {{^}}  call void '_GLOBAL__sub_I_{{[^']+}}'(){{$}}
// IL-NEXT:  {{^}}  ret{{$}}
// IL-LABEL: {{^}}.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed{{$}}
// IL:       {{^}}  call int32 'main'(){{$}}

// P64: {{^}}target triple = "msil64-unknown-dotnet"{{$}}
// P32: {{^}}target triple = "msil32-unknown-dotnet"{{$}}

// The vtable of C: offset-to-top, a null RTTI slot (-fno-rtti), then n.
// CHECK: {{^}}@_ZTV1C = linkonce_odr unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN1C1nEv] }
// The dynamic initializer is reached through llvm.global_ctors.
// CHECK: {{^}}@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @_GLOBAL__sub_I_[[TU:[^,]+]], ptr null }]

// CHECK-LABEL: define internal void @__cxx_global_var_init()
// CHECK:       call void @_ZN1CC1Ev(ptr {{.*}}@_ZL1g)

template <typename T> T t(T x) { return x + x; }

struct C {
  int v;
  C() : v(41) {}
  virtual int n() { return ++v; }
};

static C g;

// CHECK:       define {{[^@]*}}[[L]] @_Z5use_tl([[L]] noundef %x)
// CHECK:       call noundef [[L]] @_Z1tIlET_S0_([[L]] noundef
// CHECK:       define linkonce_odr noundef [[L]] @_Z1tIlET_S0_([[L]] noundef %x)
long use_t(long x) { return t(x); }

// CHECK:       define {{[^@]*}}[[L]] @_Z4sizem([[L]] noundef %n)
__SIZE_TYPE__ size(__SIZE_TYPE__ n) { return n; }

// CHECK-LABEL: define {{[^@]*}}i32 @c_entry(i32 noundef %x)
extern "C" int c_entry(int x) { return x; }

// CHECK-LABEL: define {{[^@]*}}i32 @main()
// CHECK:       call noundef i32 @_ZN1C1nEv(ptr {{.*}}@_ZL1g)
// CHECK:       call noundef [[L]] @_Z1tIlET_S0_([[L]] noundef 21)
extern "C" int main() { return g.n() == 42 && t<long>(21) == 42 ? 0 : 1; }

// The constructor stores the vtable's address point.
// CHECK-LABEL: define linkonce_odr noundef i32 @_ZN1C1nEv(ptr
// CHECK-LABEL: define linkonce_odr void @_ZN1CC2Ev(ptr
// CHECK:       store ptr getelementptr inbounds inrange({{[^)]*}}) (i8, ptr @_ZTV1C, [[L]] {{8|16}}), ptr

// CHECK:       define internal void @_GLOBAL__sub_I_[[TU]]()
// CHECK:       call void @__cxx_global_var_init()
