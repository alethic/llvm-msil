"""Prototype post-processor for clang -S IL (recon atomics-threads, E8/E4/E6).

1. Every P/Invoke declaration of the pseudo module "msil.atomic" becomes a managed method whose body
   uses System.Threading.Interlocked / Volatile (the proposed lowering, as out-of-line helpers;
   addresses are native int values passed where T& is expected).
2. Every address field set in <Module>::.cctor by 'ldftn <sig> <name>(<params>); stsfld native int
   <field>' whose function is listed with --thunk becomes a native-callable delegate thunk (Design.md
   4.5 default): a private delegate type, a static field that keeps the delegate alive, and
   Marshal.GetFunctionPointerForDelegate. Usage: post.py in.il out.il [--thunk name]...
"""
import re, sys

MSCORLIB = "[mscorlib]"
IL = "[mscorlib]System.Threading.Interlocked::"
BODIES = {
 "__atomic_fetch_add_4": ("int32", "native int p, int32 v, int32 o",
   f"ldarg.0 ldarg.1 call int32 {IL}Add(int32&, int32) ldarg.1 sub ret"),
 "__atomic_fetch_add_8": ("native int", "native int p, native int v, int32 o",
   f"ldarg.0 ldarg.1 conv.i8 call int64 {IL}Add(int64&, int64) ldarg.1 conv.i8 sub conv.i ret"),
 "__atomic_load_4": ("int32", "native int p, int32 o", "ldarg.0 volatile. ldind.i4 ret"),
 "__atomic_load_8": ("native int", "native int p, int32 o", "ldarg.0 volatile. ldind.i ret"),
 "__atomic_store_4": ("void", "native int p, int32 v, int32 o",
   f"ldarg.0 ldarg.1 volatile. stind.i4 ldarg.2 ldc.i4.5 bne.un.s D call void {IL}MemoryBarrier() D: ret"),
 "__atomic_store_8": ("void", "native int p, native int v, int32 o",
   f"ldarg.0 ldarg.1 volatile. stind.i ldarg.2 ldc.i4.5 bne.un.s D call void {IL}MemoryBarrier() D: ret"),
 "__atomic_exchange_4": ("int32", "native int p, int32 v, int32 o",
   f"ldarg.0 ldarg.1 call int32 {IL}Exchange(int32&, int32) ret"),
 "__atomic_compare_exchange_4": ("int32", "native int p, native int e, int32 d, int32 so, int32 fo",
   f".locals (int32 exp, int32 old) ldarg.1 ldind.i4 stloc.0 ldarg.0 ldarg.2 ldloc.0 "
   f"call int32 {IL}CompareExchange(int32&, int32, int32) stloc.1 ldloc.1 ldloc.0 bne.un.s F "
   f"ldc.i4.1 ret F: ldarg.1 ldloc.1 stind.i4 ldc.i4.0 ret"),
 "__atomic_compare_exchange_8": ("int32", "native int p, native int e, native int d, int32 so, int32 fo",
   f".locals (native int exp, native int old) ldarg.1 ldind.i stloc.0 ldarg.0 ldarg.2 ldloc.0 "
   f"call native int {IL}CompareExchange(native int&, native int, native int) stloc.1 ldloc.1 ldloc.0 bne.un.s F "
   f"ldc.i4.1 ret F: ldarg.1 ldloc.1 stind.i ldc.i4.0 ret"),
}


def main():
    src, dst = sys.argv[1], sys.argv[2]
    thunks = [sys.argv[i + 1] for i, a in enumerate(sys.argv) if a == "--thunk"]
    il = open(src, encoding="utf-8-sig").read()
    # 1. atomic helpers
    def repl(m):
        name = m.group(2)
        if name not in BODIES:
            raise SystemExit("post.py: no body for " + name)
        ret, params, body = BODIES[name]
        return (f".method assembly static {ret} '{name}'({params}) cil managed aggressiveinlining\n"
                f"{{\n  .maxstack 8\n  {body}\n}}")
    il = re.sub(r'\.method public hidebysig static pinvokeimpl\("msil\.atomic"[^)]*\) (.*?) \'([^\']+)\'\(.*?\) cil managed preservesig \{\}',
                repl, il)
    # 2. delegate thunks
    extra = []
    for name in thunks:
        m = re.search(r"ldftn (.+?) '" + re.escape(name) + r"'\((.*?)\)\n\s*stsfld native int ('[^']+')", il)
        if not m:
            raise SystemExit("post.py: no address field for " + name)
        ret, params, field = m.group(1), m.group(2), m.group(3)
        dlg = f"'<msil>dlg_{name}'"
        keep = f"'<msil>keep_{name}'"
        new = (f"ldnull\n  ldftn {ret} '{name}'({params})\n"
               f"  newobj instance void {dlg}::.ctor(object, native int)\n  dup\n"
               f"  stsfld class {dlg} {keep}\n"
               f"  call native int {MSCORLIB}System.Runtime.InteropServices.Marshal::GetFunctionPointerForDelegate(class {MSCORLIB}System.Delegate)\n"
               f"  stsfld native int {field}")
        il = il[:m.start()] + new + il[m.end():]
        extra.append(f".field private static class {dlg} {keep}")
        extra.append(f""".class private auto ansi sealed {dlg} extends {MSCORLIB}System.MulticastDelegate
{{
  .method public hidebysig specialname rtspecialname instance void .ctor(object o, native int m) runtime managed {{}}
  .method public hidebysig newslot virtual instance {ret} Invoke({params}) runtime managed {{}}
}}""")
    if thunks:
        il = re.sub(r"(\.method private hidebysig specialname rtspecialname static void \.cctor\(\) cil managed\n\{\n  \.maxstack )(\d+)",
                    lambda m: m.group(1) + str(max(4, int(m.group(2)))), il)
    il += "\n" + "\n".join(extra) + "\n"
    open(dst, "w", encoding="utf-8").write(il)


main()
