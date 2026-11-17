// The IR of managed-call declarations (ImplementationPlan.md 5.20.4, milestone
// MC-1):
// - MR2: the IR declaration of a function with msil_method is named
//   '__msil_mc.<symbol>', <symbol> being the name it would otherwise have: the
//   C name, an asm label, the Itanium or the MSVC mangled name of a C++
//   function (overloads each have their own), the C name inside 'extern "C"'.
//   No declaration of the bare name is emitted, and calls stay ordinary calls
//   of the declaration (MR3), at -O0 and -O2.
// - MR1: the declaration carries "msil-managed-type", "msil-managed-member",
//   "msil-managed-sig" (each as written), "msil-managed-asm" only when
//   ASSEMBLY is given, "msil-managed-conv" (one MS5 code per C parameter in
//   order, ';', the result's code) and, with msil_catch, "msil-managed-catch"
//   (the ordered 'Type=code' list joined by ';'). LLVM prints string
//   attributes sorted by key, so the msil-managed-* keys are adjacent and in
//   the order asm, catch, conv, member, sig, type; a declaration without
//   ASSEMBLY or msil_catch has "msil-managed-conv" first among them. Every
//   conversion code appears: val, bool, enum, utf8, utf16, utf32 (wchar_t is
//   utf16 on the Windows triple and utf32 on the Linux triple and both
//   portable triples), obj, boxed, gch, this-obj, this-val, this-gch, void,
//   err.
// - The C runs cover the two host triples and both portable triples
//   (msil64-unknown-dotnet and msil32-unknown-dotnet, -O0) with the same
//   attributes and '!msil.managed.calls' operands; only the IR width of a
//   pointer-sized C type differs: intptr_t is i64 on the 64-bit triples (P64)
//   and i32 on msil32-unknown-dotnet (P32), in m_galloc's result and
//   m_toptr's parameter and result, while 'long long' is i64 on every triple.
// - The attributes survive redeclarations: r_used_first is called before a
//   redeclaration without the attribute, r_redecl_first is redeclared before
//   its call.
//   clang emits r_used_first's declaration at its first use, inside
//   call_before, so its check follows call_before's.
// - No two managed-call declarations share TYPE, MEMBER, SIGNATURE and
//   conversions: identical attribute sets share one '#N' group, and two
//   CHECK-DAG lines cannot match the same 'attributes #N' line. The x86_64
//   Windows ABI puts no 'zeroext' on small integer parameters.
// - MR4: one '!msil.managed.calls' operand per managed-call declaration,
//   !{IR name, type, member, signature, assembly, conv, catch}, with "" for an
//   absent assembly or catch list.
// - Internal linkage (5.20.4 as amended in section 12, the rows '5.20.4 (IR
//   name, MR4) and T-M17 amended (...)' item (1) of 2026-11-04 and '5.20.4
//   (IR name, MR4) amended again (...)'): a C 'static' managed-call
//   declaration (m_static) and one in a C++ anonymous namespace (anon_f) get
//   the IR name '__msil_mc.<symbol>.__uniq.<n>' in the declaration, the calls
//   and the '!msil.managed.calls' operand alike (matched by pattern); <n>
//   hashes the declaration's '!msil.managed.calls' operands 1-6 and its
//   function type, both as C types and as the IR types and extensions that
//   the C ABI gives the result and the parameters, not the main file's name
//   (msil-method-uniq.c checks that). External declarations have no postfix.
// - With the backend (msil-registered-target), the -S text (whole-program
//   text) defines the stub '__msil_mc.m_abs' and calls it, and assembles
//   (llvm/test/CodeGen/MSIL/managed-call-*.ll check the bodies). Text forms
//   that clang -S reaches:
//   - A 'this-val' or 'this-gch' method without "msil-managed-decl" never
//     has the placeholder in text (the row '5.20.5 ML2 (2) and 5.20.6 MW2
//     amended; refines (d) ...' of 2026-11-04): 'call instance' for
//     DateTime::get_Ticks and GCHandle::Free, with no 'constrained.';
//     'constrained. V' then 'callvirt instance <sig>
//     [mscorlib]System.Object::M' for GetHashCode and ToString (the
//     IL-TICKS, IL-FREE and IL-MORE checks). Their 'this-val' handle is
//     unboxed to the TypeSpec 'valuetype [mscorlib]System.DateTime', the
//     kind check that -S text, which no link checks, makes at run time
//     (msil-method-text-checks.c and llvm/test/CodeGen/MSIL/
//     managed-call-text-checks.ll pin the run-time checks whole).
//   - A C '_Bool' result of the 'val' row on uint8 and of the 'enum' row
//     ends 'ldc.i4.0; cgt.un; ret' (the row '5.20.3 MS5 amended (...
//     _Bool/bool ... out column is ldc.i4.0; cgt.un)' of 2026-11-04).
//   - An int64 result ends 'conv.i; ret' (m_ticks): a C 64-bit integer is
//     native int in the stub's signature, and the user's mc1-int64 pick says
//     "Fix it inside MC-1: conv.i8 on int64 arguments, conv.i on int64
//     results".
//   - An ASSEMBLY that is not an ILAsm dotted name is printed single-quoted
//     ('my lib', 'a]b'), a dotted one (System.Runtime) as it is (the row
//     '5.20.3 T-M2 and 5.20.6 MW2 amended (...)' of 2026-11-04).
//   The stubs of use_more follow use's in the text, in use_more's call
//   order.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o %t.lnx.ll %s
// RUN: FileCheck %s --input-file=%t.lnx.ll --check-prefixes=CHECK,ATTR,MD,P64 -DWCONV=utf32 --implicit-check-not="declare {{.*}}@m_"
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o %t.win.ll %s
// RUN: FileCheck %s --input-file=%t.win.ll --check-prefixes=CHECK,ATTR,MD,P64 -DWCONV=utf16 --implicit-check-not="declare {{.*}}@m_"
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o %t.p64.ll %s
// RUN: FileCheck %s --input-file=%t.p64.ll --check-prefixes=CHECK,ATTR,MD,P64 -DWCONV=utf32 --implicit-check-not="declare {{.*}}@m_"
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -emit-llvm -o %t.p32.ll %s
// RUN: FileCheck %s --input-file=%t.p32.ll --check-prefixes=CHECK,ATTR,MD,P32 -DWCONV=utf32 --implicit-check-not="declare {{.*}}@m_"
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-llvm -o %t.lnx2.ll %s
// RUN: FileCheck %s --input-file=%t.lnx2.ll --check-prefixes=CHECK,ATTR,MD,P64 -DWCONV=utf32 --implicit-check-not="declare {{.*}}@m_"
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -emit-llvm -o %t.win2.ll %s
// RUN: FileCheck %s --input-file=%t.win2.ll --check-prefixes=CHECK,ATTR,MD,P64 -DWCONV=utf16 --implicit-check-not="declare {{.*}}@m_"

// RUN: %clang_cc1 -x c++ -std=c++20 -triple x86_64-unknown-linux-gnu-msil -DCXX -emit-llvm -o %t.cxx-lnx.ll %s
// RUN: FileCheck %s --input-file=%t.cxx-lnx.ll --check-prefix=ITANIUM --implicit-check-not="declare {{.*}}@{{_Z|ec}}"
// RUN: %clang_cc1 -x c++ -std=c++20 -triple msil64-unknown-dotnet -DCXX -emit-llvm -o %t.cxx-p64.ll %s
// RUN: FileCheck %s --input-file=%t.cxx-p64.ll --check-prefix=ITANIUM --implicit-check-not="declare {{.*}}@{{_Z|ec}}"
// RUN: %clang_cc1 -x c++ -std=c++20 -triple x86_64-pc-windows-msvc-msil -DCXX -emit-llvm -o %t.cxx-win.ll %s
// RUN: FileCheck %s --input-file=%t.cxx-win.ll --check-prefix=MSVC --implicit-check-not="declare {{.*}}@{{\"\?|ec}}"

// RUN: %if msil-registered-target %{ %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -S -o %t.il %s %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL --input-file=%t.il %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL-USE --input-file=%t.il %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL-TICKS --input-file=%t.il %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL-FREE --input-file=%t.il %}
// RUN: %if msil-registered-target %{ FileCheck %s --check-prefix=IL-MORE --input-file=%t.il %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t.dll %t.il %}

#ifdef CXX

// ITANIUM-LABEL: define {{.*}}i32 @_Z7use_cxxv()
// ITANIUM:       call {{.*}}i32 @__msil_mc._Z5cxx_fi(i32 noundef 1)
// ITANIUM:       call {{.*}}i32 @__msil_mc._ZN2ns1gEi(i32 noundef 2)
// ITANIUM:       call {{.*}}i32 @__msil_mc._Z2ovi(i32 noundef 3)
// ITANIUM:       call {{.*}}i64 @__msil_mc._Z2ovx(i64 noundef 4)
// ITANIUM:       call {{.*}}i32 @__msil_mc.ec(i32 noundef 5)
// ITANIUM:       call {{.*}}i32 @__msil_mc._ZN12_GLOBAL__N_16anon_fEii.__uniq.[[UNIQ:[0-9]+]](i32 noundef 6, i32 noundef 7)
// ITANIUM-DAG:   declare {{.*}}i32 @__msil_mc._Z5cxx_fi(i32 noundef){{.*}} [[A_F:#[0-9]+]]
// ITANIUM-DAG:   declare {{.*}}i32 @__msil_mc._ZN2ns1gEi(i32 noundef){{.*}} [[A_G:#[0-9]+]]
// ITANIUM-DAG:   declare {{.*}}i32 @__msil_mc._Z2ovi(i32 noundef){{.*}} [[A_OVI:#[0-9]+]]
// ITANIUM-DAG:   declare {{.*}}i64 @__msil_mc._Z2ovx(i64 noundef){{.*}} [[A_OVX:#[0-9]+]]
// ITANIUM-DAG:   declare {{.*}}i32 @__msil_mc.ec(i32 noundef){{.*}} [[A_EC:#[0-9]+]]
// ITANIUM-DAG:   declare {{.*}}i32 @__msil_mc._ZN12_GLOBAL__N_16anon_fEii.__uniq.[[UNIQ]](i32 noundef, i32 noundef){{.*}} [[A_ANON:#[0-9]+]]
// ITANIUM-DAG:   attributes [[A_F]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ITANIUM-DAG:   attributes [[A_G]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Sign" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ITANIUM-DAG:   attributes [[A_OVI]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Negate" "msil-managed-sig"="int32(int32)" "msil-managed-type"="Ns.Ov" {{.*}}}
// ITANIUM-DAG:   attributes [[A_OVX]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Negate" "msil-managed-sig"="int64(int64)" "msil-managed-type"="Ns.Ov" {{.*}}}
// ITANIUM-DAG:   attributes [[A_EC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Max" "msil-managed-sig"="int32(int32)" "msil-managed-type"="Ns.Ec" {{.*}}}
// ITANIUM-DAG:   attributes [[A_ANON]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val,val;val" "msil-managed-member"="Min" "msil-managed-sig"="int32(int32, int32)" "msil-managed-type"="System.Math" {{.*}}}
// ITANIUM-DAG:   !{!"__msil_mc._Z5cxx_fi", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
// ITANIUM-DAG:   !{!"__msil_mc._ZN2ns1gEi", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}
// ITANIUM-DAG:   !{!"__msil_mc._Z2ovi", !"Ns.Ov", !"Negate", !"int32(int32)", !"", !"val;val", !""}
// ITANIUM-DAG:   !{!"__msil_mc._Z2ovx", !"Ns.Ov", !"Negate", !"int64(int64)", !"", !"val;val", !""}
// ITANIUM-DAG:   !{!"__msil_mc.ec", !"Ns.Ec", !"Max", !"int32(int32)", !"", !"val;val", !""}
// ITANIUM-DAG:   !{!"__msil_mc._ZN12_GLOBAL__N_16anon_fEii.__uniq.[[UNIQ]]", !"System.Math", !"Min", !"int32(int32, int32)", !"", !"val,val;val", !""}
// ITANIUM-DAG:   !msil.managed.calls = !{

// MSVC-LABEL: define {{.*}}i32 @"?use_cxx@@YAHXZ"()
// MSVC:       call {{.*}}i32 @"__msil_mc.?cxx_f@@YAHH@Z"(i32 noundef 1)
// MSVC:       call {{.*}}i32 @"__msil_mc.?g@ns@@YAHH@Z"(i32 noundef 2)
// MSVC:       call {{.*}}i32 @"__msil_mc.?ov@@YAHH@Z"(i32 noundef 3)
// MSVC:       call {{.*}}i64 @"__msil_mc.?ov@@YA_J_J@Z"(i64 noundef 4)
// MSVC:       call {{.*}}i32 @__msil_mc.ec(i32 noundef 5)
// MSVC:       call {{.*}}i32 @"__msil_mc.[[ANON:\?anon_f@\?A0x[0-9A-Fa-f]+@@YAHHH@Z]].__uniq.[[UNIQ:[0-9]+]]"(i32 noundef 6, i32 noundef 7)
// MSVC-DAG:   declare {{.*}}i32 @"__msil_mc.?cxx_f@@YAHH@Z"(i32 noundef){{.*}} [[A_F:#[0-9]+]]
// MSVC-DAG:   declare {{.*}}i32 @"__msil_mc.?g@ns@@YAHH@Z"(i32 noundef){{.*}} [[A_G:#[0-9]+]]
// MSVC-DAG:   declare {{.*}}i32 @"__msil_mc.?ov@@YAHH@Z"(i32 noundef){{.*}} [[A_OVI:#[0-9]+]]
// MSVC-DAG:   declare {{.*}}i64 @"__msil_mc.?ov@@YA_J_J@Z"(i64 noundef){{.*}} [[A_OVX:#[0-9]+]]
// MSVC-DAG:   declare {{.*}}i32 @__msil_mc.ec(i32 noundef){{.*}} [[A_EC:#[0-9]+]]
// MSVC-DAG:   declare {{.*}}i32 @"__msil_mc.[[ANON]].__uniq.[[UNIQ]]"(i32 noundef, i32 noundef){{.*}} [[A_ANON:#[0-9]+]]
// MSVC-DAG:   attributes [[A_F]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// MSVC-DAG:   attributes [[A_G]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Sign" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// MSVC-DAG:   attributes [[A_OVI]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Negate" "msil-managed-sig"="int32(int32)" "msil-managed-type"="Ns.Ov" {{.*}}}
// MSVC-DAG:   attributes [[A_OVX]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Negate" "msil-managed-sig"="int64(int64)" "msil-managed-type"="Ns.Ov" {{.*}}}
// MSVC-DAG:   attributes [[A_EC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Max" "msil-managed-sig"="int32(int32)" "msil-managed-type"="Ns.Ec" {{.*}}}
// MSVC-DAG:   attributes [[A_ANON]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val,val;val" "msil-managed-member"="Min" "msil-managed-sig"="int32(int32, int32)" "msil-managed-type"="System.Math" {{.*}}}
// MSVC-DAG:   !{!"__msil_mc.?cxx_f@@YAHH@Z", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
// MSVC-DAG:   !{!"__msil_mc.?g@ns@@YAHH@Z", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}
// MSVC-DAG:   !{!"__msil_mc.?ov@@YAHH@Z", !"Ns.Ov", !"Negate", !"int32(int32)", !"", !"val;val", !""}
// MSVC-DAG:   !{!"__msil_mc.?ov@@YA_J_J@Z", !"Ns.Ov", !"Negate", !"int64(int64)", !"", !"val;val", !""}
// MSVC-DAG:   !{!"__msil_mc.ec", !"Ns.Ec", !"Max", !"int32(int32)", !"", !"val;val", !""}
// MSVC-DAG:   !{!"__msil_mc.[[ANON]].__uniq.[[UNIQ]]", !"System.Math", !"Min", !"int32(int32, int32)", !"", !"val,val;val", !""}
// MSVC-DAG:   !msil.managed.calls = !{

int cxx_f(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
namespace ns {
int g(int x) __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
}
int ov(int x) __attribute__((msil_method("Ns.Ov", "Negate", "int32(int32)")));
long long ov(long long x) __attribute__((msil_method("Ns.Ov", "Negate", "int64(int64)")));
extern "C" int ec(int x) __attribute__((msil_method("Ns.Ec", "Max", "int32(int32)")));
namespace {
int anon_f(int x, int y) __attribute__((msil_method("System.Math", "Min", "int32(int32, int32)")));
}

int use_cxx() { return cxx_f(1) + ns::g(2) + ov(3) + (int)ov(4LL) + ec(5) + anon_f(6, 7); }

#else // C

typedef __SIZE_TYPE__ size_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef struct __dn_object *dn_handle;

int m_abs(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)")));
_Bool m_isdigit(char16_t c) __attribute__((msil_method("System.Char", "IsDigit", "bool(char)")));
void m_cursor(int on) __attribute__((msil_method("System.Console", "set_CursorVisible", "void(bool)")));
dn_handle m_fs_open(const char *path, int mode, int access, int share, int bufsize, int *err)
    __attribute__((msil_method("System.IO.FileStream", ".ctor",
                               "instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, "
                               "valuetype System.IO.FileShare, int32)"),
                   msil_catch("System.IO.FileNotFoundException", 2),
                   msil_catch("System.IO.DirectoryNotFoundException", 2),
                   msil_catch("System.UnauthorizedAccessException", 13),
                   msil_catch("System.IO.PathTooLongException", 36),
                   msil_catch("System.Text.DecoderFallbackException", 84),
                   msil_catch("System.IO.IOException", 5)));
_Bool m_exists16(const char16_t *p) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool m_exists32(const char32_t *p) __attribute__((msil_method("System.IO.File", "Exists", "bool(string)")));
_Bool m_existsw(const wchar_t *p) __attribute__((msil_method("System.IO.Directory", "Exists", "bool(string)")));
void m_keepalive(void *o) __attribute__((msil_method("System.GC", "KeepAlive", "void(object)")));
dn_handle m_stdout(void) __attribute__((msil_method("System.Console", "OpenStandardOutput", "class System.IO.Stream()")));
int m_cmpdate(dn_handle a, dn_handle b) __attribute__((msil_method("valuetype System.DateTime", "Compare", "int32(valuetype System.DateTime, valuetype System.DateTime)")));
dn_handle m_now(void) __attribute__((msil_method("valuetype System.DateTime", "get_Now", "valuetype System.DateTime()")));
dn_handle m_mkdate(long long ticks) __attribute__((msil_method("valuetype System.DateTime", ".ctor", "instance void(int64)")));
long long m_ticks(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "get_Ticks", "instance int64()")));
void m_write(dn_handle s, dn_handle a, int off, int n) __attribute__((msil_method("System.IO.Stream", "Write", "instance void(uint8[], int32, int32)")));
void m_free(dn_handle h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Free", "instance void()")));
intptr_t m_galloc(void *o) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "Alloc", "valuetype System.Runtime.InteropServices.GCHandle(object)")));
intptr_t m_toptr(intptr_t h) __attribute__((msil_method("valuetype System.Runtime.InteropServices.GCHandle", "ToIntPtr", "native int(valuetype System.Runtime.InteropServices.GCHandle)")));
dn_handle m_newbytes(int n) __attribute__((msil_method("uint8[]", ".ctor", "instance void(int32)")));
long long m_freq(void) __attribute__((msil_method("System.Diagnostics.Stopwatch", "Frequency", "field int64", "System")));
int m_count(dn_handle h) __attribute__((msil_method("Ns.Holder", "Count", "instance field int32")));
void m_memcpy(void *d, void *s, long long ds, long long n) __attribute__((msil_method("System.Buffer", "MemoryCopy", "void(void*, void*, int64, int64)")));
long long m_enum64(unsigned char k) __attribute__((msil_method("Ns.Enums", "Widen", "int64(valuetype Ns.Kind)")));
void m_delete(const char *path, int *err) __attribute__((msil_method("System.IO.File", "Delete", "void(string)"), msil_catch("System.UnauthorizedAccessException", 13), msil_catch("System.IO.IOException", 5)));
int m_lab(int x) __asm__("m_lab_sym") __attribute__((msil_method("System.Math", "Sign", "int32(int32)")));
_Bool m_tobyte(int v) __attribute__((msil_method("System.Convert", "ToByte", "uint8(int32)")));
_Bool m_latency(void) __attribute__((msil_method("System.Runtime.GCSettings", "get_LatencyMode", "valuetype System.Runtime.GCLatencyMode()")));
int m_dthash(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "GetHashCode", "instance int32()")));
dn_handle m_dtstr(dn_handle d) __attribute__((msil_method("valuetype System.DateTime", "ToString", "instance string()")));
int m_asm_sp(int x) __attribute__((msil_method("System.Math", "Max", "int32(int32)", "my lib")));
int m_asm_br(int x) __attribute__((msil_method("System.Math", "Min", "int32(int32)", "a]b")));
int m_asm_dot(int x) __attribute__((msil_method("System.Math", "Abs", "int32(int32)", "System.Runtime")));

// Internal linkage (5.20.3 as amended in section 12 on 2026-11-04): a
// 'static' managed-call declaration has the same attributes and operand as
// an external one, and its IR name is '__msil_mc.<symbol>.__uniq.<n>'
// (5.20.4 as amended, the same row's item (1), and amended again: <n> hashes
// the operands and the function type, as C types and as the IR types and
// extensions that the C ABI gives the result and the parameters), so the IR
// linker merges it only with another unit's declaration that is
// interchangeable with it (msil-method-uniq.c). clang emits it at its first
// use, inside use_static, so its check follows use_static's.
static long long m_static(long long x) __attribute__((msil_method("System.Math", "Abs", "int64(int64)")));
int use_static(void) { return (int)m_static(-9); }

// CHECK-LABEL: define {{.*}}i32 @use_static()
// CHECK:       call i64 @__msil_mc.m_static.__uniq.[[SUNIQ:[0-9]+]](i64 noundef -9)
// ATTR:        declare {{.*}}i64 @__msil_mc.m_static.__uniq.[[SUNIQ]](i64 noundef){{.*}} [[A_STATIC:#[0-9]+]]

int r_used_first(int x) __attribute__((msil_method("System.Convert", "ToInt32", "int32(int32)")));
int call_before(void) { return r_used_first(1); }
int r_used_first(int x);
int r_redecl_first(int x) __attribute__((msil_method("System.Buffers.Binary.BinaryPrimitives", "ReverseEndianness", "int32(int32)")));
int r_redecl_first(int x);

// CHECK-LABEL: define {{.*}}i32 @call_before()
// CHECK:       call i32 @__msil_mc.r_used_first(i32 noundef 1)
// ATTR:        declare {{.*}}i32 @__msil_mc.r_used_first(i32 noundef){{.*}} [[A_RUF:#[0-9]+]]

// CHECK-LABEL: define {{.*}}i32 @use(
// CHECK-DAG:   call i32 @__msil_mc.m_abs(
// CHECK-DAG:   call {{.*}}i1 @__msil_mc.m_isdigit(
// CHECK-DAG:   call void @__msil_mc.m_cursor(
// CHECK-DAG:   call ptr @__msil_mc.m_fs_open(
// CHECK-DAG:   call {{.*}}i1 @__msil_mc.m_exists16(
// CHECK-DAG:   call {{.*}}i1 @__msil_mc.m_exists32(
// CHECK-DAG:   call {{.*}}i1 @__msil_mc.m_existsw(
// CHECK-DAG:   call void @__msil_mc.m_keepalive(
// CHECK-DAG:   call ptr @__msil_mc.m_stdout()
// CHECK-DAG:   call i32 @__msil_mc.m_cmpdate(
// CHECK-DAG:   call ptr @__msil_mc.m_now()
// CHECK-DAG:   call ptr @__msil_mc.m_mkdate(
// CHECK-DAG:   call i64 @__msil_mc.m_ticks(
// CHECK-DAG:   call void @__msil_mc.m_write(
// CHECK-DAG:   call void @__msil_mc.m_free(
// P64-DAG:     call i64 @__msil_mc.m_galloc(
// P32-DAG:     call i32 @__msil_mc.m_galloc(
// P64-DAG:     call i64 @__msil_mc.m_toptr(
// P32-DAG:     call i32 @__msil_mc.m_toptr(
// CHECK-DAG:   call ptr @__msil_mc.m_newbytes(
// CHECK-DAG:   call i64 @__msil_mc.m_freq()
// CHECK-DAG:   call i32 @__msil_mc.m_count(
// CHECK-DAG:   call void @__msil_mc.m_memcpy(
// CHECK-DAG:   call i64 @__msil_mc.m_enum64(
// CHECK-DAG:   call void @__msil_mc.m_delete(
// CHECK-DAG:   call i32 @__msil_mc.m_lab_sym(
// CHECK-DAG:   call i32 @__msil_mc.r_redecl_first(
// CHECK:       ret i32
int use(void) {
  int err = 0;
  char buf[16];
  dn_handle s = m_stdout();
  dn_handle f = m_fs_open("f", 3, 1, 1, 4096, &err);
  m_write(s, m_newbytes(4), 0, 4);
  m_free(f);
  m_cursor(1);
  m_keepalive(s);
  m_memcpy(buf, buf + 8, 8, 4);
  m_delete("g", &err);
  return m_abs(-1) + m_isdigit(u'1') + m_exists16(u"a") + m_exists32(U"b") + m_existsw(L"c") +
         m_cmpdate(m_now(), m_mkdate(5)) + (int)m_ticks(s) + (int)m_galloc(s) + (int)m_toptr(7) +
         (int)m_freq() + m_count(s) + (int)m_enum64(2) + m_lab(3) + r_redecl_first(4) + err;
}

// ATTR-DAG: declare {{.*}}i32 @__msil_mc.m_abs(i32 noundef){{.*}} [[A_ABS:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i1 @__msil_mc.m_isdigit(i16 noundef{{( zeroext)?}}){{.*}} [[A_ISDIGIT:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_cursor(i32 noundef){{.*}} [[A_CURSOR:#[0-9]+]]
// ATTR-DAG: declare {{.*}}ptr @__msil_mc.m_fs_open(ptr noundef, i32 noundef, i32 noundef, i32 noundef, i32 noundef, ptr noundef){{.*}} [[A_FSOPEN:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i1 @__msil_mc.m_exists16(ptr noundef){{.*}} [[A_E16:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i1 @__msil_mc.m_exists32(ptr noundef){{.*}} [[A_E32:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i1 @__msil_mc.m_existsw(ptr noundef){{.*}} [[A_EW:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_keepalive(ptr noundef){{.*}} [[A_KEEP:#[0-9]+]]
// ATTR-DAG: declare {{.*}}ptr @__msil_mc.m_stdout(){{.*}} [[A_STDOUT:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i32 @__msil_mc.m_cmpdate(ptr noundef, ptr noundef){{.*}} [[A_CMP:#[0-9]+]]
// ATTR-DAG: declare {{.*}}ptr @__msil_mc.m_now(){{.*}} [[A_NOW:#[0-9]+]]
// ATTR-DAG: declare {{.*}}ptr @__msil_mc.m_mkdate(i64 noundef){{.*}} [[A_MKDATE:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i64 @__msil_mc.m_ticks(ptr noundef){{.*}} [[A_TICKS:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_write(ptr noundef, ptr noundef, i32 noundef, i32 noundef){{.*}} [[A_WRITE:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_free(ptr noundef){{.*}} [[A_FREE:#[0-9]+]]
// P64-DAG:  declare {{.*}}i64 @__msil_mc.m_galloc(ptr noundef){{.*}} [[A_GALLOC:#[0-9]+]]
// P32-DAG:  declare {{.*}}i32 @__msil_mc.m_galloc(ptr noundef){{.*}} [[A_GALLOC:#[0-9]+]]
// P64-DAG:  declare {{.*}}i64 @__msil_mc.m_toptr(i64 noundef){{.*}} [[A_TOPTR:#[0-9]+]]
// P32-DAG:  declare {{.*}}i32 @__msil_mc.m_toptr(i32 noundef){{.*}} [[A_TOPTR:#[0-9]+]]
// ATTR-DAG: declare {{.*}}ptr @__msil_mc.m_newbytes(i32 noundef){{.*}} [[A_NEWBYTES:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i64 @__msil_mc.m_freq(){{.*}} [[A_FREQ:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i32 @__msil_mc.m_count(ptr noundef){{.*}} [[A_COUNT:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_memcpy(ptr noundef, ptr noundef, i64 noundef, i64 noundef){{.*}} [[A_MEMCPY:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i64 @__msil_mc.m_enum64(i8 noundef{{( zeroext)?}}){{.*}} [[A_ENUM64:#[0-9]+]]
// ATTR-DAG: declare {{.*}}void @__msil_mc.m_delete(ptr noundef, ptr noundef){{.*}} [[A_DELETE:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i32 @__msil_mc.m_lab_sym(i32 noundef){{.*}} [[A_LAB:#[0-9]+]]
// ATTR-DAG: declare {{.*}}i32 @__msil_mc.r_redecl_first(i32 noundef){{.*}} [[A_RRF:#[0-9]+]]

// use_more's declarations follow use's in the IR.
// CHECK-LABEL: define {{.*}}i32 @use_more(
// CHECK:       call {{.*}}i1 @__msil_mc.m_tobyte(i32 noundef 2)
// CHECK:       call {{.*}}i1 @__msil_mc.m_latency()
// CHECK:       call i32 @__msil_mc.m_dthash(
// CHECK:       call ptr @__msil_mc.m_dtstr(
// CHECK:       call i32 @__msil_mc.m_asm_sp(i32 noundef 1)
// CHECK:       call i32 @__msil_mc.m_asm_br(i32 noundef 2)
// CHECK:       call i32 @__msil_mc.m_asm_dot(i32 noundef 3)
// ATTR:        declare {{.*}}i1 @__msil_mc.m_tobyte(i32 noundef){{.*}} [[A_TOBYTE:#[0-9]+]]
// ATTR:        declare {{.*}}i1 @__msil_mc.m_latency(){{.*}} [[A_LATENCY:#[0-9]+]]
// ATTR:        declare {{.*}}i32 @__msil_mc.m_dthash(ptr noundef){{.*}} [[A_DTHASH:#[0-9]+]]
// ATTR:        declare {{.*}}ptr @__msil_mc.m_dtstr(ptr noundef){{.*}} [[A_DTSTR:#[0-9]+]]
// ATTR:        declare {{.*}}i32 @__msil_mc.m_asm_sp(i32 noundef){{.*}} [[A_ASMSP:#[0-9]+]]
// ATTR:        declare {{.*}}i32 @__msil_mc.m_asm_br(i32 noundef){{.*}} [[A_ASMBR:#[0-9]+]]
// ATTR:        declare {{.*}}i32 @__msil_mc.m_asm_dot(i32 noundef){{.*}} [[A_ASMDOT:#[0-9]+]]
int use_more(dn_handle d) {
  int r = m_tobyte(2);
  r += m_latency();
  r += m_dthash(d);
  r += m_dtstr(d) != 0;
  r += m_asm_sp(1);
  r += m_asm_br(2);
  r += m_asm_dot(3);
  return r;
}

// ATTR-DAG: attributes [[A_ABS]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ATTR-DAG: attributes [[A_ISDIGIT]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;bool" "msil-managed-member"="IsDigit" "msil-managed-sig"="bool(char)" "msil-managed-type"="System.Char" {{.*}}}
// ATTR-DAG: attributes [[A_CURSOR]] = { {{([a-z_]+ )*}}"msil-managed-conv"="bool;void" "msil-managed-member"="set_CursorVisible" "msil-managed-sig"="void(bool)" "msil-managed-type"="System.Console" {{.*}}}
// ATTR-DAG: attributes [[A_FSOPEN]] = { {{([a-z_]+ )*}}"msil-managed-catch"="System.IO.FileNotFoundException=2;System.IO.DirectoryNotFoundException=2;System.UnauthorizedAccessException=13;System.IO.PathTooLongException=36;System.Text.DecoderFallbackException=84;System.IO.IOException=5" "msil-managed-conv"="utf8,enum,enum,enum,val,err;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, valuetype System.IO.FileShare, int32)" "msil-managed-type"="System.IO.FileStream" {{.*}}}
// ATTR-DAG: attributes [[A_E16]] = { {{([a-z_]+ )*}}"msil-managed-conv"="utf16;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" {{.*}}}
// ATTR-DAG: attributes [[A_E32]] = { {{([a-z_]+ )*}}"msil-managed-conv"="utf32;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" {{.*}}}
// ATTR-DAG: attributes [[A_EW]] = { {{([a-z_]+ )*}}"msil-managed-conv"="[[WCONV]];bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.Directory" {{.*}}}
// ATTR-DAG: attributes [[A_KEEP]] = { {{([a-z_]+ )*}}"msil-managed-conv"="obj;void" "msil-managed-member"="KeepAlive" "msil-managed-sig"="void(object)" "msil-managed-type"="System.GC" {{.*}}}
// ATTR-DAG: attributes [[A_STDOUT]] = { {{([a-z_]+ )*}}"msil-managed-conv"=";obj" "msil-managed-member"="OpenStandardOutput" "msil-managed-sig"="class System.IO.Stream()" "msil-managed-type"="System.Console" {{.*}}}
// ATTR-DAG: attributes [[A_CMP]] = { {{([a-z_]+ )*}}"msil-managed-conv"="boxed,boxed;val" "msil-managed-member"="Compare" "msil-managed-sig"="int32(valuetype System.DateTime, valuetype System.DateTime)" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_NOW]] = { {{([a-z_]+ )*}}"msil-managed-conv"=";boxed" "msil-managed-member"="get_Now" "msil-managed-sig"="valuetype System.DateTime()" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_MKDATE]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_TICKS]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-val;val" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_WRITE]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-obj,obj,val,val;void" "msil-managed-member"="Write" "msil-managed-sig"="instance void(uint8[], int32, int32)" "msil-managed-type"="System.IO.Stream" {{.*}}}
// ATTR-DAG: attributes [[A_FREE]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-gch;void" "msil-managed-member"="Free" "msil-managed-sig"="instance void()" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" {{.*}}}
// ATTR-DAG: attributes [[A_GALLOC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="obj;gch" "msil-managed-member"="Alloc" "msil-managed-sig"="valuetype System.Runtime.InteropServices.GCHandle(object)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" {{.*}}}
// ATTR-DAG: attributes [[A_TOPTR]] = { {{([a-z_]+ )*}}"msil-managed-conv"="gch;val" "msil-managed-member"="ToIntPtr" "msil-managed-sig"="native int(valuetype System.Runtime.InteropServices.GCHandle)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" {{.*}}}
// ATTR-DAG: attributes [[A_NEWBYTES]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;obj" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int32)" "msil-managed-type"="uint8[]" {{.*}}}
// ATTR-DAG: attributes [[A_FREQ]] = { {{([a-z_]+ )*}}"msil-managed-asm"="System" "msil-managed-conv"=";val" "msil-managed-member"="Frequency" "msil-managed-sig"="field int64" "msil-managed-type"="System.Diagnostics.Stopwatch" {{.*}}}
// ATTR-DAG: attributes [[A_COUNT]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-obj;val" "msil-managed-member"="Count" "msil-managed-sig"="instance field int32" "msil-managed-type"="Ns.Holder" {{.*}}}
// ATTR-DAG: attributes [[A_MEMCPY]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val,val,val,val;void" "msil-managed-member"="MemoryCopy" "msil-managed-sig"="void(void*, void*, int64, int64)" "msil-managed-type"="System.Buffer" {{.*}}}
// ATTR-DAG: attributes [[A_ENUM64]] = { {{([a-z_]+ )*}}"msil-managed-conv"="enum;val" "msil-managed-member"="Widen" "msil-managed-sig"="int64(valuetype Ns.Kind)" "msil-managed-type"="Ns.Enums" {{.*}}}
// ATTR-DAG: attributes [[A_DELETE]] = { {{([a-z_]+ )*}}"msil-managed-catch"="System.UnauthorizedAccessException=13;System.IO.IOException=5" "msil-managed-conv"="utf8,err;void" "msil-managed-member"="Delete" "msil-managed-sig"="void(string)" "msil-managed-type"="System.IO.File" {{.*}}}
// ATTR-DAG: attributes [[A_LAB]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Sign" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ATTR-DAG: attributes [[A_RUF]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="ToInt32" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Convert" {{.*}}}
// ATTR-DAG: attributes [[A_RRF]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="ReverseEndianness" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Buffers.Binary.BinaryPrimitives" {{.*}}}
// ATTR-DAG: attributes [[A_TOBYTE]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="ToByte" "msil-managed-sig"="uint8(int32)" "msil-managed-type"="System.Convert" {{.*}}}
// ATTR-DAG: attributes [[A_LATENCY]] = { {{([a-z_]+ )*}}"msil-managed-conv"=";enum" "msil-managed-member"="get_LatencyMode" "msil-managed-sig"="valuetype System.Runtime.GCLatencyMode()" "msil-managed-type"="System.Runtime.GCSettings" {{.*}}}
// ATTR-DAG: attributes [[A_DTHASH]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-val;val" "msil-managed-member"="GetHashCode" "msil-managed-sig"="instance int32()" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_DTSTR]] = { {{([a-z_]+ )*}}"msil-managed-conv"="this-val;obj" "msil-managed-member"="ToString" "msil-managed-sig"="instance string()" "msil-managed-type"="valuetype System.DateTime" {{.*}}}
// ATTR-DAG: attributes [[A_ASMSP]] = { {{([a-z_]+ )*}}"msil-managed-asm"="my lib" "msil-managed-conv"="val;val" "msil-managed-member"="Max" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ATTR-DAG: attributes [[A_ASMBR]] = { {{([a-z_]+ )*}}"msil-managed-asm"="a]b" "msil-managed-conv"="val;val" "msil-managed-member"="Min" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ATTR-DAG: attributes [[A_ASMDOT]] = { {{([a-z_]+ )*}}"msil-managed-asm"="System.Runtime" "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" {{.*}}}
// ATTR-DAG: attributes [[A_STATIC]] = { {{([a-z_]+ )*}}"msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int64(int64)" "msil-managed-type"="System.Math" {{.*}}}

// MD-DAG: !msil.managed.calls = !{
// MD-DAG: !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
// MD-DAG: !{!"__msil_mc.m_isdigit", !"System.Char", !"IsDigit", !"bool(char)", !"", !"val;bool", !""}
// MD-DAG: !{!"__msil_mc.m_fs_open", !"System.IO.FileStream", !".ctor", !"instance void(string, valuetype System.IO.FileMode, valuetype System.IO.FileAccess, valuetype System.IO.FileShare, int32)", !"", !"utf8,enum,enum,enum,val,err;obj", !"System.IO.FileNotFoundException=2;System.IO.DirectoryNotFoundException=2;System.UnauthorizedAccessException=13;System.IO.PathTooLongException=36;System.Text.DecoderFallbackException=84;System.IO.IOException=5"}
// MD-DAG: !{!"__msil_mc.m_existsw", !"System.IO.Directory", !"Exists", !"bool(string)", !"", !"[[WCONV]];bool", !""}
// MD-DAG: !{!"__msil_mc.m_freq", !"System.Diagnostics.Stopwatch", !"Frequency", !"field int64", !"System", !";val", !""}
// MD-DAG: !{!"__msil_mc.m_delete", !"System.IO.File", !"Delete", !"void(string)", !"", !"utf8,err;void", !"System.UnauthorizedAccessException=13;System.IO.IOException=5"}
// MD-DAG: !{!"__msil_mc.m_lab_sym", !"System.Math", !"Sign", !"int32(int32)", !"", !"val;val", !""}
// MD-DAG: !{!"__msil_mc.m_asm_sp", !"System.Math", !"Max", !"int32(int32)", !"my lib", !"val;val", !""}
// MD-DAG: !{!"__msil_mc.m_latency", !"System.Runtime.GCSettings", !"get_LatencyMode", !"valuetype System.Runtime.GCLatencyMode()", !"", !";enum", !""}
// MD-DAG: !{!"__msil_mc.r_used_first", !"System.Convert", !"ToInt32", !"int32(int32)", !"", !"val;val", !""}
// MD-DAG: !{!"__msil_mc.m_static.__uniq.[[SUNIQ]]", !"System.Math", !"Abs", !"int64(int64)", !"", !"val;val", !""}

// IL-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
// IL:       call int32 [mscorlib]System.Math::Abs(int32)
// IL:       ret
// IL-NEXT:  } // end of method '__msil_mc.m_abs'
// IL-USE-LABEL: .method public static int32 'use'() cil managed
// IL-USE:       call int32 '__msil_mc.m_abs'(int32)

// IL-TICKS-LABEL: .method assembly static native int '__msil_mc.m_ticks'(native int) cil managed
// IL-TICKS:       unbox valuetype [mscorlib]System.DateTime
// IL-TICKS-NEXT:  call instance int64 [mscorlib]System.DateTime::get_Ticks()
// IL-TICKS-NEXT:  conv.i
// IL-TICKS-NEXT:  ret
// IL-TICKS-NEXT:  } // end of method '__msil_mc.m_ticks'

// IL-FREE-LABEL: .method assembly static void '__msil_mc.m_free'(native int) cil managed
// IL-FREE:       .maxstack {{[0-9]+}}
// IL-FREE-NEXT:  ldarg.0
// IL-FREE-NEXT:  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int)
// IL-FREE-NEXT:  box [mscorlib]System.Runtime.InteropServices.GCHandle
// IL-FREE-NEXT:  unbox [mscorlib]System.Runtime.InteropServices.GCHandle
// IL-FREE-NEXT:  call instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()
// IL-FREE-NEXT:  ret
// IL-FREE-NEXT:  } // end of method '__msil_mc.m_free'

// IL-MORE-LABEL: .method assembly static int32 '__msil_mc.m_tobyte'(int32) cil managed
// IL-MORE:       call uint8 [mscorlib]System.Convert::ToByte(int32)
// IL-MORE-NEXT:  ldc.i4.0
// IL-MORE-NEXT:  cgt.un
// IL-MORE-NEXT:  ret
// IL-MORE-NEXT:  } // end of method '__msil_mc.m_tobyte'
// IL-MORE-LABEL: .method assembly static {{.+}} '__msil_mc.m_latency'() cil managed
// IL-MORE:       call valuetype [mscorlib]System.Runtime.GCLatencyMode [mscorlib]System.Runtime.GCSettings::get_LatencyMode()
// IL-MORE-NEXT:  ldc.i4.0
// IL-MORE-NEXT:  cgt.un
// IL-MORE-NEXT:  ret
// IL-MORE-NEXT:  } // end of method '__msil_mc.m_latency'
// IL-MORE-LABEL: .method assembly static int32 '__msil_mc.m_dthash'(native int) cil managed
// IL-MORE:       unbox valuetype [mscorlib]System.DateTime
// IL-MORE-NEXT:  constrained. [mscorlib]System.DateTime
// IL-MORE-NEXT:  callvirt instance int32 [mscorlib]System.Object::GetHashCode()
// IL-MORE-NEXT:  ret
// IL-MORE-LABEL: .method assembly static native int '__msil_mc.m_dtstr'(native int) cil managed
// IL-MORE:       unbox valuetype [mscorlib]System.DateTime
// IL-MORE-NEXT:  constrained. [mscorlib]System.DateTime
// IL-MORE-NEXT:  callvirt instance string [mscorlib]System.Object::ToString()
// IL-MORE-LABEL: .method assembly static int32 '__msil_mc.m_asm_sp'(int32) cil managed
// IL-MORE:       call int32 ['my lib']System.Math::Max(int32)
// IL-MORE-LABEL: .method assembly static int32 '__msil_mc.m_asm_br'(int32) cil managed
// IL-MORE:       call int32 ['a]b']System.Math::Min(int32)
// IL-MORE-LABEL: .method assembly static int32 '__msil_mc.m_asm_dot'(int32) cil managed
// IL-MORE:       call int32 [System.Runtime]System.Math::Abs(int32)

#endif
