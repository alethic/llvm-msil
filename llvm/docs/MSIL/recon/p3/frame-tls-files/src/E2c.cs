// E2c: ways to find the current thread's block, per lookup (NoInlining caller, best of 5).
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
static class TS0 { [ThreadStatic] public static IntPtr P; [ThreadStatic] public static object O; }
static class E2c {
  static ThreadLocal<IntPtr> TL = new ThreadLocal<IntPtr>(() => (IntPtr)8);
  static IntPtr[] ById = new IntPtr[4096];
  static Thread[] Owner = new Thread[4096];
  [MethodImpl(MethodImplOptions.NoInlining)] static IntPtr TsPtr() { IntPtr p = TS0.P; return p != IntPtr.Zero ? p : (TS0.P = (IntPtr)8); }
  [MethodImpl(MethodImplOptions.NoInlining)] static object TsObj() { object o = TS0.O; return o ?? (TS0.O = new object()); }
  [MethodImpl(MethodImplOptions.NoInlining)] static IntPtr TlT() { return TL.Value; }
  [MethodImpl(MethodImplOptions.NoInlining)] static Thread Cur() { return Thread.CurrentThread; }
  [MethodImpl(MethodImplOptions.NoInlining)] static IntPtr ByIdLookup() {
    Thread t = Thread.CurrentThread; int id = t.ManagedThreadId;
    if ((uint)id < (uint)ById.Length && Owner[id] == t) return ById[id];
    Owner[id] = t; return ById[id] = (IntPtr)8;
  }
  static double T(Func<long> f, int n) { long acc = 0; for (int i = 0; i < 300000; i++) acc += f(); double best = 1e9;
    for (int r = 0; r < 5; r++) { var sw = Stopwatch.StartNew(); for (int i = 0; i < n; i++) acc += f(); best = Math.Min(best, sw.Elapsed.TotalMilliseconds * 1e6 / n); }
    return best + (acc == 7 ? 1 : 0); }
  static int Main() {
    const int N = 3000000;
    Console.WriteLine("runtime {0}", Environment.Version);
    Console.WriteLine("  baseline (delegate + empty call) {0,7:F2} ns", T(() => 1, N));
    Console.WriteLine("  [ThreadStatic] IntPtr + check    {0,7:F2} ns", T(() => (long)TsPtr(), N));
    Console.WriteLine("  [ThreadStatic] object + check    {0,7:F2} ns", T(() => TsObj() == null ? 0 : 1, N));
    Console.WriteLine("  ThreadLocal<IntPtr>.Value        {0,7:F2} ns", T(() => (long)TlT(), N));
    Console.WriteLine("  Thread.CurrentThread             {0,7:F2} ns", T(() => Cur() == null ? 0 : 1, N));
    Console.WriteLine("  CurrentThread.ManagedThreadId tbl{0,7:F2} ns", T(() => (long)ByIdLookup(), N));
    return 0;
  }
}
