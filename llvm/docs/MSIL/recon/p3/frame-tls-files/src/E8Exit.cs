// E8: a C thread that ends with pthread_exit (Linux) or ExitThread (Windows) instead of returning
// from its start routine, called from managed code on a native thread (start routine = delegate thunk).
// Does the process survive, and does a [ThreadStatic] holder still get finalized?
using System;
using System.Runtime.InteropServices;
using System.Threading;
sealed class H { ~H() { Interlocked.Increment(ref E8Exit.Fin); } }
static class E8Exit {
  public static int Fin;
  [ThreadStatic] static H Hold;
  delegate uint WinStart(IntPtr arg);
  delegate IntPtr PosixStart(IntPtr arg);
  [DllImport("kernel32.dll")] static extern IntPtr CreateThread(IntPtr a, UIntPtr s, IntPtr start, IntPtr arg, uint f, IntPtr id);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] static extern void ExitThread(uint code);
  [DllImport("libc.so.6")] static extern int pthread_create(out IntPtr t, IntPtr attr, IntPtr start, IntPtr arg);
  [DllImport("libc.so.6")] static extern int pthread_join(IntPtr t, IntPtr r);
  [DllImport("libc.so.6")] static extern void pthread_exit(IntPtr r);
  static int reached;
  static void Deep(int n) { if (n > 0) { Deep(n - 1); return; } Hold = new H(); Interlocked.Increment(ref reached);
    if (Environment.OSVersion.Platform == PlatformID.Win32NT) ExitThread(7); else pthread_exit((IntPtr)7);
    Console.WriteLine("returned from thread exit?!"); }
  static readonly WinStart ws = a => { try { Deep(3); } finally { Console.WriteLine("  finally ran on exit"); } return 0; };
  static readonly PosixStart ps = a => { try { Deep(3); } finally { Console.WriteLine("  finally ran on exit"); } return IntPtr.Zero; };
  static int Main(string[] args) {
    Console.WriteLine("runtime {0}", Environment.Version);
    bool win = Environment.OSVersion.Platform == PlatformID.Win32NT;
    for (int i = 0; i < 3; i++) {
      if (win) { IntPtr h = CreateThread(IntPtr.Zero, UIntPtr.Zero, Marshal.GetFunctionPointerForDelegate(ws), IntPtr.Zero, 0, IntPtr.Zero); WaitForSingleObject(h, 0xFFFFFFFF); }
      else { IntPtr t; pthread_create(out t, IntPtr.Zero, Marshal.GetFunctionPointerForDelegate(ps), IntPtr.Zero); pthread_join(t, IntPtr.Zero); }
    }
    for (int k = 0; k < 3; k++) { GC.Collect(); GC.WaitForPendingFinalizers(); }
    Console.WriteLine("threads reached exit call: {0}, holders finalized: {1}", reached, Fin);
    return 0;
  }
}
