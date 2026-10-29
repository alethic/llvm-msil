// E5: freeing a per-thread unmanaged block (TLS block / shadow stack) at thread exit.
//  mode "holder": a finalizable holder object in a [ThreadStatic] field frees the block in its finalizer
//  mode "none":   no holder; measures the leak per thread
// Threads: "managed" (new Thread), "native" (CreateThread on Windows, pthread_create on Linux,
// whose start routine is a delegate thunk: the thread becomes a managed thread on the reverse P/Invoke).
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

unsafe sealed class Holder {
  public IntPtr Block;
  public Holder(IntPtr b) { Block = b; }
  ~Holder() {
    if (Environment.HasShutdownStarted) { E5Exit.FinalizedAtShutdown++; return; }   // never free while the process ends
    Marshal.FreeHGlobal(Block); Interlocked.Increment(ref E5Exit.Freed);
  }
}

unsafe static class E5Exit {
  public static int Freed, Allocated, FinalizedAtShutdown;
  static int BlockSize = 64 * 1024;
  static bool UseHolder;
  [ThreadStatic] static IntPtr Block;
  [ThreadStatic] static Holder Hold;
  [ThreadStatic] static int Id;

  static IntPtr Get() {
    IntPtr b = Block;
    if (b == IntPtr.Zero) {
      b = Marshal.AllocHGlobal(BlockSize);
      for (int i = 0; i < BlockSize; i += 4096) ((byte*)b)[i] = 1;   // touch every page, like a used TLS block
      Block = b; Interlocked.Increment(ref Allocated);
      if (UseHolder) Hold = new Holder(b);
    }
    return b;
  }
  static int distinct;
  static void Body() {
    IntPtr b = Get();
    if (Id != 0) throw new Exception("ThreadStatic not fresh on this thread");
    Id = Thread.CurrentThread.ManagedThreadId; *(int*)b = Id;
    if (*(int*)Get() == Id) Interlocked.Increment(ref distinct);
  }

  // --- native threads ---
  delegate uint WinStart(IntPtr arg);
  delegate IntPtr PosixStart(IntPtr arg);
  [DllImport("kernel32.dll")] static extern IntPtr CreateThread(IntPtr a, UIntPtr stack, IntPtr start, IntPtr arg, uint flags, IntPtr id);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("libc.so.6")] static extern int pthread_create(out IntPtr t, IntPtr attr, IntPtr start, IntPtr arg);
  [DllImport("libc.so.6")] static extern int pthread_join(IntPtr t, IntPtr ret);
  static WinStart winStart = a => { Body(); return 0; };
  static PosixStart posixStart = a => { Body(); return IntPtr.Zero; };

  static void RunThreads(string kind, int n) {
    bool win = Environment.OSVersion.Platform == PlatformID.Win32NT;
    IntPtr fp = win ? Marshal.GetFunctionPointerForDelegate(winStart) : Marshal.GetFunctionPointerForDelegate(posixStart);
    for (int i = 0; i < n; i++) {
      if (kind == "managed") { var t = new Thread(Body); t.Start(); t.Join(); }
      else if (win) { IntPtr h = CreateThread(IntPtr.Zero, UIntPtr.Zero, fp, IntPtr.Zero, 0, IntPtr.Zero); WaitForSingleObject(h, 0xFFFFFFFF); CloseHandle(h); }
      else { IntPtr t; if (pthread_create(out t, IntPtr.Zero, fp, IntPtr.Zero) != 0) throw new Exception("pthread_create"); pthread_join(t, IntPtr.Zero); }
    }
  }
  static long Priv() { var p = Process.GetCurrentProcess(); p.Refresh(); return win() ? p.PrivateMemorySize64 : p.WorkingSet64; }
  static bool win() { return Environment.OSVersion.Platform == PlatformID.Win32NT; }

  static int Main(string[] a) {
    string kind = a[0]; UseHolder = a[1] == "holder"; int n = int.Parse(a[2]);
    Console.WriteLine("runtime {0} ({1}) threads={2} kind={3} mode={4} block={5} KiB",
      Environment.Version, RuntimeInformation.FrameworkDescription, n, kind, a[1], BlockSize / 1024);
    RunThreads(kind, 50); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
    long m0 = Priv(); int f0 = Freed, a0 = Allocated;
    RunThreads(kind, n);
    long m1 = Priv();
    for (int k = 0; k < 3; k++) { GC.Collect(); GC.WaitForPendingFinalizers(); }
    long m2 = Priv();
    Console.WriteLine("allocated {0}, distinct per-thread values {1}, freed by finalizers {2}", Allocated - a0, distinct - 50, Freed - f0);
    Console.WriteLine("memory ({3}): +{0:F1} KiB/thread after the threads, +{1:F1} KiB/thread after GC+finalizers (total {2:F1} MiB)",
      (m1 - m0) / 1024.0 / n, (m2 - m0) / 1024.0 / n, (m2 - m0) / 1048576.0, win() ? "private bytes" : "RSS");
    // A holder of the main thread stays reachable: does a finalizer run for it at process exit?
    Get();
    return 0;
  }
}
