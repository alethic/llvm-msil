using System;
using System.Diagnostics;
using System.Threading;
static class P {
  static void Main() {
    const int N = 2000000;
    for (int rep = 0; rep < 3; rep++) {
      var sw = Stopwatch.StartNew();
      for (int i = 0; i < N; i++) Thread.SpinWait(1);
      double spin = sw.Elapsed.TotalMilliseconds * 1e6 / N;
#if NET7_0_OR_GREATER
      sw.Restart();
      for (int i = 0; i < N; i++) System.Runtime.Intrinsics.X86.X86Base.Pause();
      double pause = sw.Elapsed.TotalMilliseconds * 1e6 / N;
      sw.Restart(); uint c = 0;
      for (uint i = 0; i < N; i++) c = System.Runtime.Intrinsics.X86.Sse42.Crc32(c, i);
      double crc = sw.Elapsed.TotalMilliseconds * 1e6 / N;
      Console.WriteLine($"rep {rep}: SpinWait(1) {spin:F1} ns, X86Base.Pause {pause:F1} ns, Sse42.Crc32 {crc:F2} ns ({c:x8}), IsSupported sse42={System.Runtime.Intrinsics.X86.Sse42.IsSupported}");
#else
      Console.WriteLine($"rep {rep}: SpinWait(1) {spin:F1} ns");
#endif
    }
    Console.WriteLine("runtime " + Environment.Version + " ProcessorCount " + Environment.ProcessorCount);
  }
}
