// E6: errno through P/Invoke, as a C program on a host-libc triple reads it: the failing call, then
// *__errno_location() (glibc) or *_errno() (UCRT), each a separate blittable P/Invoke.
//  1. the first call of the errno accessor (IL stub generation and JIT on this thread in between)
//  2. 2M iterations while another thread allocates (GC suspensions at the P/Invoke returns)
//  3. per-thread errno: two threads with different failures at the same time
using System;
using System.Runtime.InteropServices;
using System.Threading;
unsafe static class E6Errno {
  static bool Win = Environment.OSVersion.Platform == PlatformID.Win32NT;
  [DllImport("libc.so.6", EntryPoint = "open")] static extern int l_open(byte* path, int flags);
  [DllImport("libc.so.6", EntryPoint = "strtol")] static extern long l_strtol(byte* s, IntPtr end, int b);
  [DllImport("libc.so.6", EntryPoint = "__errno_location")] static extern int* l_errno();
  [DllImport("ucrtbase.dll", EntryPoint = "_open")] static extern int w_open(byte* path, int flags);
  [DllImport("ucrtbase.dll", EntryPoint = "strtol")] static extern int w_strtol(byte* s, IntPtr end, int b);
  [DllImport("ucrtbase.dll", EntryPoint = "_errno")] static extern int* w_errno();
  static byte[] Path = System.Text.Encoding.ASCII.GetBytes("/nonexistent/dir/file\0");
  static int Open() { fixed (byte* p = Path) return Win ? w_open(p, 0) : l_open(p, 0); }
  static byte[] Big = System.Text.Encoding.ASCII.GetBytes("999999999999999999999999\0");
  static long Range() { fixed (byte* p = Big) return Win ? w_strtol(p, IntPtr.Zero, 10) : l_strtol(p, IntPtr.Zero, 10); }
  static int Errno() { return Win ? *w_errno() : *l_errno(); }
  const int ENOENT = 2, ERANGE = 34;
  static volatile bool stop;
  static int Main() {
    Console.WriteLine("runtime {0} ({1})", Environment.Version, Win ? "UCRT _errno" : "glibc __errno_location");
    // 1. first call of the accessor right after the failure
    int r = Open(); int e = Errno();
    Console.WriteLine("1. open -> {0}, first errno read -> {1} (expected {2}) {3}", r, e, ENOENT, e == ENOENT ? "ok" : "CLOBBERED");
    // 2. under GC pressure
    var gcThread = new Thread(() => { object o = null; while (!stop) { o = new byte[1000]; if (o == null) break; } }); gcThread.IsBackground = true; gcThread.Start();
    int bad = 0, n = Win ? 100000 : 2000000; int gcs0 = GC.CollectionCount(0);
    for (int i = 0; i < n; i++) { Open(); if (Errno() != ENOENT) bad++; }
    stop = true; gcThread.Join();
    Console.WriteLine("2. {0} iterations, {1} gen0 GCs meanwhile, mismatches {2}", n, GC.CollectionCount(0) - gcs0, bad);
    // 3. two threads, two different errno values
    int badA = 0, badB = 0;
    var a = new Thread(() => { for (int i = 0; i < (Win ? 50000 : 500000); i++) { Open(); if (Errno() != ENOENT) badA++; } });
    var b = new Thread(() => { for (int i = 0; i < (Win ? 50000 : 500000); i++) { Range(); if (Errno() != ERANGE) badB++; } });
    a.Start(); b.Start(); a.Join(); b.Join();
    Console.WriteLine("3. per-thread errno: thread A (ENOENT) mismatches {0}, thread B (ERANGE from strtol) mismatches {1}", badA, badB);
    return 0;
  }
}
