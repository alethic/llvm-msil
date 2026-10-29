// E2b: [ThreadStatic] access cost on .NET 8 in isolation: a class with nothing but the
// thread-static field (no .cctor), on the main thread and on a new thread.
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Threading;
static class TS0 { [ThreadStatic] public static int C; [ThreadStatic] public static IntPtr P; }
static class TS1 { [ThreadStatic] public static int C; static int x = Environment.TickCount; public static int X { get { return x; } } }
static class E2b {
  [MethodImpl(MethodImplOptions.NoInlining)] static int A0() { return ++TS0.C; }
  [MethodImpl(MethodImplOptions.NoInlining)] static int A1() { return ++TS1.C; }
  [MethodImpl(MethodImplOptions.NoInlining)] static IntPtr P0() { IntPtr p = TS0.P; return p != IntPtr.Zero ? p : (TS0.P = (IntPtr)8); }
  static double T(Func<int> f, int n) { int acc = 0; for (int i = 0; i < 300000; i++) acc += f(); double best = 1e9;
    for (int r = 0; r < 5; r++) { var sw = Stopwatch.StartNew(); for (int i = 0; i < n; i++) acc += f(); best = Math.Min(best, sw.Elapsed.TotalMilliseconds * 1e6 / n); }
    return best + (acc == 7 ? 1 : 0); }
  static void Run(string who) {
    Console.WriteLine("{0}: no-cctor class {1:F2} ns, class with cctor {2:F2} ns, IntPtr check {3:F2} ns, baseline delegate+call {4:F2} ns",
      who, T(A0, 3000000), T(A1, 3000000), T(() => (int)P0(), 3000000), T(() => 1, 3000000));
  }
  static int Main() {
    Console.WriteLine("runtime {0}", Environment.Version); var x = TS1.X;
    Run("main thread"); var t = new Thread(() => Run("new thread")); t.Start(); t.Join(); return 0;
  }
}
