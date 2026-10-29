# Generates bench.il: single-thread cost of each atomic lowering shape.
bodies = {
 'plain_add':   "ldarg.0 ldarg.0 ldind.i4 ldc.i4.1 add stind.i4",
 'direct_add':  "ldarg.0 ldc.i4.1 call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32) pop",
 'asref_add':   "ldarg.0 call int32& B::'asref_i4'(native int) ldc.i4.1 call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32) pop",
 'ophelper_add':"ldarg.0 ldc.i4.1 call int32 B::'atomic_add_4'(native int, int32) pop",
 'noinl_add':   "ldarg.0 ldc.i4.1 call int32 B::'atomic_add_4_noinl'(native int, int32) pop",
 'cas_add':     "ldarg.0 ldc.i4.1 call int32 B::'cas_add_4'(native int, int32) pop",
 'byte_cas_add':"ldarg.0 ldc.i4.1 call int32 B::'cas_add_1'(native int, int32) pop",
 'vol_read':    "ldarg.0 volatile. ldind.i4 pop",
 'Vread':       "ldarg.0 call int32& B::'asref_i4'(native int) call int32 [mscorlib]System.Threading.Volatile::Read(int32&) pop",
 'vol_write':   "ldarg.0 ldloc.0 volatile. stind.i4",
 'xchg_store':  "ldarg.0 call int32& B::'asref_i4'(native int) ldloc.0 call int32 [mscorlib]System.Threading.Interlocked::Exchange(int32&, int32) pop",
 'write_fence': "ldarg.0 ldloc.0 volatile. stind.i4 call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()",
 'add64':       "ldarg.0 call int64& B::'asref_i8'(native int) ldc.i4.1 conv.i8 call int64 [mscorlib]System.Threading.Interlocked::Add(int64&, int64) pop",
 'pinvoke_add': "ldarg.0 ldc.i4.1 ldc.i4.5 call int32 B::'__atomic_fetch_add_4'(native int, int32, int32) pop",
}
il = []
il.append(""".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly bench { }
.module bench.exe
.class public auto ansi abstract sealed B extends [mscorlib]System.Object {
  .method assembly static int32& 'asref_i4'(native int p) cil managed aggressiveinlining { ldarg.0 ret }
  .method assembly static int64& 'asref_i8'(native int p) cil managed aggressiveinlining { ldarg.0 ret }
  .method assembly static int32 'atomic_add_4'(native int p, int32 v) cil managed aggressiveinlining {
    ldarg.0 ldarg.1 call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32) ret }
  .method assembly static int32 'atomic_add_4_noinl'(native int p, int32 v) cil managed noinlining {
    ldarg.0 ldarg.1 call int32 [mscorlib]System.Threading.Interlocked::Add(int32&, int32) ret }
  // CAS loop: what AtomicExpand builds for atomicrmw max/nand/fadd
  .method assembly static int32 'cas_add_4'(native int p, int32 v) cil managed {
    .locals (int32 old, int32 seen)
    ldarg.0 volatile. ldind.i4 stloc.0
  L: ldarg.0 call int32& B::'asref_i4'(native int) ldloc.0 ldarg.1 add ldloc.0
    call int32 [mscorlib]System.Threading.Interlocked::CompareExchange(int32&, int32, int32)
    stloc.1 ldloc.1 ldloc.0 beq.s D
    ldloc.1 stloc.0 br.s L
  D: ldloc.0 ret }
  // byte add through a CAS on the containing aligned int32 (little endian)
  .method assembly static int32 'cas_add_1'(native int p, int32 v) cil managed {
    .locals (native int w, int32 sh, int32 mask, int32 old, int32 nw, int32 seen)
    ldarg.0 ldc.i4.3 not conv.i and stloc.0
    ldarg.0 conv.i4 ldc.i4.3 and ldc.i4.3 shl stloc.1
    ldc.i4 255 ldloc.1 shl stloc.2
    ldloc.0 volatile. ldind.i4 stloc.3
  L: ldloc.3 ldloc.2 not and
     ldloc.3 ldloc.1 shr.un ldarg.1 add ldc.i4 255 and ldloc.1 shl or stloc.s nw
     ldloc.0 call int32& B::'asref_i4'(native int) ldloc.s nw ldloc.3
     call int32 [mscorlib]System.Threading.Interlocked::CompareExchange(int32&, int32, int32)
     stloc.s seen ldloc.s seen ldloc.3 beq.s D
     ldloc.s seen stloc.3 br.s L
  D: ldloc.3 ldloc.1 shr.un ldc.i4 255 and ret }
  .method assembly static pinvokeimpl("libatomic.so.1" nomangle cdecl) int32 '__atomic_fetch_add_4'(native int, int32, int32) cil managed preservesig {}
""")
for name, body in bodies.items():
    il.append(f"""  .method public static int64 'b_{name}'(native int p, int32 n) cil managed noinlining {{
    .locals init (int32 i, int64 t0)
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() stloc.1
    ldc.i4.0 stloc.0 br.s C
  L: {body}
    ldloc.0 ldc.i4.1 add stloc.0
  C: ldloc.0 ldarg.1 blt.s L
    call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp() ldloc.1 sub ret
  }}""")
il.append("""  .method public static void run(string name, int64 ticks, int32 n) cil managed {
    ldarg.0 call void [mscorlib]System.Console::Write(string)
    ldstr " " call void [mscorlib]System.Console::Write(string)
    ldarg.1 conv.r8 ldc.r8 1e9 mul ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency conv.r8 div ldarg.2 conv.r8 div
    call void [mscorlib]System.Console::WriteLine(float64) ret }
  .method public static int32 Main(string[] args) cil managed {
    .entrypoint
    .locals init (native int p, int32 n, int32 rep, bool linux)
    ldc.i4 4096 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) ldc.i4.s 64 conv.i add stloc.0
    ldc.i4 50000000 stloc.1
    ldarg.0 ldlen conv.i4 ldc.i4.0 cgt stloc.3
    ldc.i4.0 stloc.2
  R:""")
for name in bodies:
    if name == 'pinvoke_add':
        il.append("    ldloc.3 brfalse.s SKIP")
    il.append(f"    ldstr \"{name}\" ldloc.0 ldloc.1 call int64 B::'b_{name}'(native int, int32) ldloc.1 call void B::run(string, int64, int32)")
il.append("""  SKIP:
    ldloc.2 ldc.i4.1 add stloc.2 ldloc.2 ldc.i4.2 blt R
    ldc.i4.0 ret }
}""")
open('bench.il','w').write('\n'.join(il))
