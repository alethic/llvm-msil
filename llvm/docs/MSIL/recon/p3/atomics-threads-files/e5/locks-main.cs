using System; using System.Threading; using System.Diagnostics; using System.Runtime.InteropServices;
public static unsafe class P {
  static object[] table = new object[1024];
  static int Main() {
    bool same = true;
    for (int i = 0; i < 1024; i++) { object a = L1.Get(i), b = L2.Get(i); if (!ReferenceEquals(a, b)) same = false; table[i] = a; }
    Console.WriteLine("interned lock objects shared across assemblies: " + same + " (" + Environment.Version + ")");
    long* p = (long*)Marshal.AllocHGlobal(64); for (int k = 0; k < 8; k++) p[k] = 0;
    const int N = 20000000;
    for (int rep = 0; rep < 2; rep++) {
      var sw = Stopwatch.StartNew();
      for (int i = 0; i < N; i++) { object l = table[(int)(((long)p >> 4) & 1023)]; Monitor.Enter(l); p[0]++; p[1]++; Monitor.Exit(l); }
      double mon = sw.Elapsed.TotalMilliseconds * 1e6 / N;
      long* lk = p + 4;
      sw = Stopwatch.StartNew();
      for (int i = 0; i < N; i++) { while (Interlocked.CompareExchange(ref *lk, 1, 0) != 0) { } p[0]++; p[1]++; Volatile.Write(ref *lk, 0); }
      double spin = sw.Elapsed.TotalMilliseconds * 1e6 / N;
      sw = Stopwatch.StartNew();
      for (int i = 0; i < N; i++) { Interlocked.Increment(ref p[2]); }
      double inc = sw.Elapsed.TotalMilliseconds * 1e6 / N;
      Console.WriteLine(string.Format("ns/op: Monitor on interned string {0:F1}, spinlock CAS+release {1:F1}, Interlocked.Increment {2:F1}", mon, spin, inc));
    }
    return 0;
  }
}
