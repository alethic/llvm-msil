// Threads.cs: the CLR side of the thread-test shapes: Interlocked on unmanaged memory vs a plain
// increment, [ThreadStatic] per-thread values, and a Volatile ping-pong. Built with the .NET
// Framework csc (C# 5, /unsafe), so one image runs on Framework 4.8 and, through dotnet exec with
// a runtimeconfig, on .NET 8 and 10 (the harness's way).
using System;
using System.Runtime.InteropServices;
using System.Threading;
unsafe static class P {
  static long iters; static int* cell; static volatile int go;
  [ThreadStatic] static long tl;
  static long[] tlr = new long[16];
  static int Main(string[] a) {
    string mode = a.Length > 0 ? a[0] : "atomic";
    int n = a.Length > 1 ? int.Parse(a[1]) : 4;
    iters = a.Length > 2 ? long.Parse(a[2]) : 1000000;
    cell = (int*)Marshal.AllocHGlobal(64);
    *cell = 0;
    var sw = System.Diagnostics.Stopwatch.StartNew();
    var t = new Thread[n];
    if (mode == "atomic" || mode == "plain") {
      bool plain = mode == "plain";
      for (int i = 0; i < n; ++i) {
        t[i] = new Thread(() => {
          while (go == 0) { }
          if (plain) { int* p = cell; for (long k = 0; k < iters; ++k) { Volatile.Write(ref *p, Volatile.Read(ref *p) + 1); } }
          else { for (long k = 0; k < iters; ++k) Interlocked.Increment(ref *cell); }
        });
        t[i].Start();
      }
      go = 1;
      foreach (var x in t) x.Join();
      long total = *cell, want = n * iters;
      Console.WriteLine(plain ? "plain: lost {0} of {1} ({2} ms)" : "atomic: {3} (expected {1}) ({2} ms)",
                        want - total, want, sw.ElapsedMilliseconds, total);
      return plain ? 0 : (total == want ? 0 : 1);
    }
    if (mode == "tls") {
      for (int i = 0; i < n; ++i) { int id = i; t[i] = new Thread(() => { for (long k = 0; k < iters; ++k) tl += 1; tlr[id] = tl; }); t[i].Start(); }
      foreach (var x in t) x.Join();
      int bad = 0; for (int i = 0; i < n; ++i) if (tlr[i] != iters) bad++;
      Console.WriteLine("tls: {0} threads, {1} wrong, main's tl {2} ({3} ms)", n, bad, tl, sw.ElapsedMilliseconds);
      return bad;
    }
    return 2;
  }
}
