// E2/E4: per-call cost of the dynamic-alloca strategies and per-access cost of the TLS strategies.
// Every measured function is NoInlining (an out-of-line C function); the loop calls it N times.
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

// The shadow-stack / TLS control block that option B keeps per thread, in unmanaged memory.
[StructLayout(LayoutKind.Sequential)]
unsafe struct Block { public byte* Sp; public byte* Limit; public byte* Base; public long Pad; }

// No static constructor: beforefieldinit, as a private '<msil>Thread' class would be.
unsafe static class TS {
  [ThreadStatic] public static Block* Blk;         // option B: one pointer per thread
  [ThreadStatic] public static byte* Sp;            // option B1: the shadow SP itself
  [ThreadStatic] public static byte* Limit;
  [ThreadStatic] public static int Counter;         // option A: one [ThreadStatic] field per TLS variable
  public static readonly IntPtr TlsOff = (IntPtr)24; // a TLS symbol's offset field (initonly)
  public static IntPtr Global;                       // a plain global's address field (Phase 2 data)

  [MethodImpl(MethodImplOptions.NoInlining)]
  public static Block* InitBlock() {
    Block* b = (Block*)Marshal.AllocHGlobal(sizeof(Block) + 64 + (1 << 20));
    b->Base = (byte*)b + sizeof(Block) + 64; b->Sp = b->Base + (1 << 20); b->Limit = b->Base;
    Blk = b; return b;
  }
  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static Block* Get() { Block* b = Blk; return b != null ? b : InitBlock(); }
  [MethodImpl(MethodImplOptions.NoInlining)]
  public static Block* GetNoInline() { Block* b = Blk; return b != null ? b : InitBlock(); }
  [MethodImpl(MethodImplOptions.NoInlining)]
  public static void Overflow() { throw new InsufficientExecutionStackException(); }
  [MethodImpl(MethodImplOptions.NoInlining)]
  public static void InitSp() { byte* b = (byte*)Marshal.AllocHGlobal(1 << 20); Limit = b; Sp = b + (1 << 20); }
}

// The same thread-static field on a class with a static constructor (not beforefieldinit),
// as a field of <Module> (whose .cctor is the module initializer) would be.
unsafe static class TSC {
  [ThreadStatic] public static Block* Blk;
  public static int X;
  static TSC() { X = 1; }
  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static Block* Get() { Block* b = Blk; return b != null ? b : (Blk = TS.InitBlock()); }
}

unsafe static class E2Cost {
  static ThreadLocal<IntPtr> TL = new ThreadLocal<IntPtr>(() => Marshal.AllocHGlobal(64));

  // --- dynamic alloca ---
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int StaticFrame(int n) { byte* p = stackalloc byte[64]; p[0] = (byte)n; p[63] = 1; return p[0] + p[63]; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int Localloc(int n) { byte* p = stackalloc byte[n]; p[0] = (byte)n; p[n - 1] = 1; return p[0] + p[n - 1]; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int ShadowSpField(int n) {   // B1: [ThreadStatic] SP and limit fields
    byte* sp0 = TS.Sp; if (sp0 == null) { TS.InitSp(); sp0 = TS.Sp; }
    byte* p = (byte*)((long)(sp0 - n) & -16L); if (p < TS.Limit) TS.Overflow(); TS.Sp = p;
    p[0] = (byte)n; p[n - 1] = 1; int r = p[0] + p[n - 1];
    TS.Sp = sp0; return r;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int ShadowBlock(int n) {     // B2: one [ThreadStatic] pointer to the per-thread block
    Block* b = TS.Get(); byte* sp0 = b->Sp;
    byte* p = (byte*)((long)(sp0 - n) & -16L); if (p < b->Limit) TS.Overflow(); b->Sp = p;
    p[0] = (byte)n; p[n - 1] = 1; int r = p[0] + p[n - 1];
    b->Sp = sp0; return r;
  }
  // --- thread-local variable access (read-modify-write of one int) ---
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int GlobalRmw() { int* p = (int*)TS.Global; return ++*p; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int ThreadStaticField() { return ++TS.Counter; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsBlockInline() { int* p = (int*)((byte*)TS.Get() + (long)TS.TlsOff); return ++*p; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsBlockCall() { int* p = (int*)((byte*)TS.GetNoInline() + (long)TS.TlsOff); return ++*p; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsBlockCctorClass() { int* p = (int*)((byte*)TSC.Get() + (long)TS.TlsOff); return ++*p; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsThreadLocalT() { int* p = (int*)((byte*)TL.Value + 8); return ++*p; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsBlockLoop4() {        // one base per function, four accesses (hoisted TP)
    byte* t = (byte*)TS.Get(); int* p = (int*)(t + (long)TS.TlsOff);
    int* q = (int*)(t + 32); int* r = (int*)(t + 40); int* s = (int*)(t + 48);
    return ++*p + ++*q + ++*r + ++*s;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int TlsBlockEach4() {        // one base per access, four accesses
    int* p = (int*)((byte*)TS.Get() + (long)TS.TlsOff); int* q = (int*)((byte*)TS.Get() + 32);
    int* r = (int*)((byte*)TS.Get() + 40); int* s = (int*)((byte*)TS.Get() + 48);
    return ++*p + ++*q + ++*r + ++*s;
  }

  static readonly string[] Names = { "alloca: static 64-byte localloc", "alloca: localloc(n) n=64", "alloca: shadow SP [ThreadStatic] x2", "alloca: shadow SP in TLS block", "tls: plain global (address field)", "tls: A [ThreadStatic] int field", "tls: B block, inline check", "tls: B block, helper call", "tls: B block, class with .cctor", "tls: C ThreadLocal<IntPtr>", "tls: B 4 vars, one base", "tls: B 4 vars, base per access" };
  static int Loop(int v, int iters) {
    int acc = 0;
    switch (v) {
      case 0: for (int i = 0; i < iters; i++) acc += StaticFrame(64); break;
      case 1: for (int i = 0; i < iters; i++) acc += Localloc(64); break;
      case 2: for (int i = 0; i < iters; i++) acc += ShadowSpField(64); break;
      case 3: for (int i = 0; i < iters; i++) acc += ShadowBlock(64); break;
      case 4: for (int i = 0; i < iters; i++) acc += GlobalRmw(); break;
      case 5: for (int i = 0; i < iters; i++) acc += ThreadStaticField(); break;
      case 6: for (int i = 0; i < iters; i++) acc += TlsBlockInline(); break;
      case 7: for (int i = 0; i < iters; i++) acc += TlsBlockCall(); break;
      case 8: for (int i = 0; i < iters; i++) acc += TlsBlockCctorClass(); break;
      case 9: for (int i = 0; i < iters; i++) acc += TlsThreadLocalT(); break;
      case 10: for (int i = 0; i < iters; i++) acc += TlsBlockLoop4(); break;
      case 11: for (int i = 0; i < iters; i++) acc += TlsBlockEach4(); break;
    }
    return acc;
  }
  static int Main(string[] a) {
    int iters = a.Length > 0 ? int.Parse(a[0]) : 5000000;
    int rounds = a.Length > 1 ? int.Parse(a[1]) : 7;
    TS.Global = Marshal.AllocHGlobal(64);
    Console.WriteLine("runtime {0} ({1})", Environment.Version, RuntimeInformation.FrameworkDescription);
    int n = Names.Length; double[] best = new double[n]; int sink = 0;
    for (int v = 0; v < n; v++) { best[v] = double.MaxValue; sink += Loop(v, 300000); }
    Thread.Sleep(300);
    for (int v = 0; v < n; v++) sink += Loop(v, 300000);
    for (int r = 0; r < rounds; r++)
      for (int v = 0; v < n; v++) {
        var sw = Stopwatch.StartNew(); sink += Loop(v, iters); sw.Stop();
        best[v] = Math.Min(best[v], sw.Elapsed.TotalMilliseconds * 1e6 / iters);
      }
    for (int v = 0; v < n; v++) Console.WriteLine("{0,-36} {1,7:F2} ns/call", Names[v], best[v]);
    return sink == 42 ? 1 : 0;
  }
}
