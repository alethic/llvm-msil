// E3: are the addresses of [ThreadStatic] fields stable across compacting GCs?
// Option A (one [ThreadStatic] field per C thread-local variable) needs a stable address,
// because C code takes &tlsvar and keeps it as a plain pointer.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

[StructLayout(LayoutKind.Sequential, Size = 256)]
struct Big { public long A; }

unsafe static class E3Addr {
  [ThreadStatic] static int TsInt;
  [ThreadStatic] static long TsLong;
  [ThreadStatic] static IntPtr TsPtr;
  [ThreadStatic] static Big TsBig;      // a value type that is not a primitive: a boxed (GC) thread static
  static Big SBig;                      // the same as an ordinary static, for comparison

  static IntPtr AInt() { fixed (int* p = &TsInt) return (IntPtr)p; }
  static IntPtr ALong() { fixed (long* p = &TsLong) return (IntPtr)p; }
  static IntPtr APtr() { fixed (IntPtr* p = &TsPtr) return (IntPtr)p; }
  static IntPtr ABig() { fixed (Big* p = &TsBig) return (IntPtr)p; }
  static IntPtr ASBig() { fixed (Big* p = &SBig) return (IntPtr)p; }

  static List<object> keep = new List<object>();
  static void Churn(int round) {
    // Allocate, keep every other object, then drop them: compaction has something to move.
    var tmp = new List<object>();
    for (int i = 0; i < 200000; i++) { var o = new byte[64 + (i % 7) * 16]; if ((i & 1) == 0) tmp.Add(o); }
    if (round % 2 == 0) keep = tmp; else keep.Clear();
    GC.Collect(2, GCCollectionMode.Forced, true, true);
    GC.WaitForPendingFinalizers();
    GC.Collect(2, GCCollectionMode.Forced, true, true);
  }
  static string Probe(string who) {
    // Garbage before the first touch, so the boxes are allocated above garbage that is later freed.
    var pre = new List<object>(); for (int i = 0; i < 100000; i++) pre.Add(new byte[100]);
    TsInt = 1; TsLong = 2; TsPtr = (IntPtr)3; TsBig.A = 4; SBig.A = 5;
    IntPtr[] a0 = { AInt(), ALong(), APtr(), ABig(), ASBig() };
    pre = null;
    int[] moved = new int[5];
    for (int r = 0; r < 20; r++) {
      Churn(r);
      IntPtr[] a = { AInt(), ALong(), APtr(), ABig(), ASBig() };
      for (int k = 0; k < 5; k++) if (a[k] != a0[k]) moved[k]++;
      if (r == 19) {
        string[] names = { "int", "long", "IntPtr", "Big(256)", "static Big" };
        var s = who + ":";
        for (int k = 0; k < 5; k++) s += string.Format(" {0}={1}", names[k], moved[k] == 0 ? "stable" : "MOVED(" + moved[k] + "/20, 0x" + a0[k].ToString("x") + "->0x" + a[k].ToString("x") + ")");
        return s;
      }
    }
    return "";
  }
  static int Main() {
    Console.WriteLine("runtime {0} ({1}) server GC={2}", Environment.Version, RuntimeInformation.FrameworkDescription, System.Runtime.GCSettings.IsServerGC);
    Console.WriteLine(Probe("main thread"));
    string r2 = null;
    var t = new Thread(() => { r2 = Probe("new thread"); }); t.Start(); t.Join();
    Console.WriteLine(r2);
    return 0;
  }
}
