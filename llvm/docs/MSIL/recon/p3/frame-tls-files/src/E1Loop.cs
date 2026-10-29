// E1: one localloc (C# stackalloc) per iteration of a loop, as option A would lower a VLA in a loop:
// localloc memory is released only when the method returns. Prints the stack growth.
using System;
unsafe class E1Loop {
  static int Run(int n, int iters) {
    byte* first = null; long sum = 0; int next = 1;
    for (int i = 0; i < iters; i++) {
      byte* p = stackalloc byte[n];
      p[0] = (byte)i; p[n - 1] = 1; sum += p[0] + p[n - 1];
      if (first == null) first = p;
      if (i + 1 == next || i + 1 == iters) {
        Console.WriteLine("iter {0,8}: growth {1,10} bytes ({2:F1} per iteration)", i + 1, (long)(first - p) + n,
                          ((long)(first - p) + n) / (double)(i + 1));
        Console.Out.Flush();
        next = next < 16384 ? next * 2 : next + 1024;
      }
    }
    return (int)(sum & 0x7f);
  }
  static int Main(string[] a) {
    int n = int.Parse(a[0]), iters = int.Parse(a[1]);
    Console.WriteLine("n={0} iters={1} 64-bit={2} runtime={3}", n, iters, IntPtr.Size == 8, Environment.Version);
    int r = Run(n, iters);
    Console.WriteLine("completed");
    return 0;
  }
}
