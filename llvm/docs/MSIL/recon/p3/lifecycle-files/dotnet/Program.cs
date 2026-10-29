// lifecycle probe (P3 lifecycle recon): when AppDomain.ProcessExit runs, on which thread, in which order
// with the host C library's exit processing, and what the exit code becomes. The trace goes to file
// descriptor 2 through the host C library (_write on Windows, write on Linux), unbuffered, so it interleaves
// exactly with native output. args[0] = mode.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

static class P {
  static readonly bool Win = Environment.OSVersion.Platform == PlatformID.Win32NT;
  static readonly Stopwatch Clock = Stopwatch.StartNew();
  static int MainThread;

  [DllImport("ucrtbase", EntryPoint = "_write")] static extern int WriteWin(int fd, byte[] buf, uint n);
  [DllImport("libc", EntryPoint = "write")] static extern IntPtr WriteLinux(int fd, byte[] buf, IntPtr n);
  [DllImport("ucrtbase", EntryPoint = "exit")] static extern void ExitWin(int code);
  [DllImport("libc", EntryPoint = "exit")] static extern void ExitLinux(int code);
  [DllImport("ucrtbase", EntryPoint = "_exit")] static extern void UExitWin(int code);
  [DllImport("libc", EntryPoint = "_exit")] static extern void UExitLinux(int code);
  [DllImport("ucrtbase", EntryPoint = "quick_exit")] static extern void QuickExitWin(int code);
  [DllImport("libc", EntryPoint = "quick_exit")] static extern void QuickExitLinux(int code);
  [DllImport("ucrtbase", EntryPoint = "abort")] static extern void AbortWin();
  [DllImport("libc", EntryPoint = "abort")] static extern void AbortLinux();
  [DllImport("ucrtbase", EntryPoint = "puts")] static extern int PutsWin(byte[] s);
  [DllImport("libc", EntryPoint = "puts")] static extern int PutsLinux(byte[] s);
  [DllImport("ucrtbase", EntryPoint = "_crt_atexit")] static extern int AtExitWin(IntPtr fn);
  [DllImport("libc", EntryPoint = "__cxa_atexit")] static extern int CxaAtExit(IntPtr fn, IntPtr arg, IntPtr dso);
  [DllImport("kernel32")] static extern void ExitProcess(uint code);
  [DllImport("kernel32")] static extern IntPtr GetCurrentProcess();
  [DllImport("kernel32")] static extern bool TerminateProcess(IntPtr p, uint code);
  [DllImport("ucrtbase", EntryPoint = "qsort")] static extern void QsortWin(IntPtr b, UIntPtr n, UIntPtr w, IntPtr cmp);
  [DllImport("libc", EntryPoint = "qsort")] static extern void QsortLinux(IntPtr b, UIntPtr n, UIntPtr w, IntPtr cmp);
  [DllImport("kernel32")] static extern IntPtr CreateThread(IntPtr a, UIntPtr s, IntPtr start, IntPtr arg, uint f, IntPtr id);
  [DllImport("libc")] static extern int pthread_create(out IntPtr t, IntPtr attr, IntPtr start, IntPtr arg);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Cmp(IntPtr a, IntPtr b);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr ThreadProc(IntPtr arg);
  static Cmp cmpKeep = (a, b) => { Tr("qsort comparator (reverse P/Invoke) calls Environment.Exit(6)"); Environment.Exit(6); return 0; };
  static ThreadProc procKeep = a => { Tr("native thread (reverse P/Invoke) calls Environment.Exit(8)"); Environment.Exit(8); return IntPtr.Zero; };

  static byte[] Z(string s) { return System.Text.Encoding.UTF8.GetBytes(s + "\0"); }
  static void Tr(string s) {
    var t = Thread.CurrentThread;
    string who = t.ManagedThreadId == MainThread ? "main" : ("thread " + t.ManagedThreadId + (t.IsBackground ? " bg" : " fg") + (t.IsThreadPoolThread ? " pool" : ""));
    var b = System.Text.Encoding.UTF8.GetBytes(string.Format("[{0,5} ms {1}] {2}\n", Clock.ElapsedMilliseconds, who, s));
    if (Win) WriteWin(2, b, (uint)b.Length); else WriteLinux(2, b, (IntPtr)b.Length);
  }
  static void NativePuts(string s) { if (Win) PutsWin(Z(s)); else PutsLinux(Z(s)); }
  static void NativeExit(int c) { if (Win) ExitWin(c); else ExitLinux(c); }

  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Cb();
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void CbArg(IntPtr a);
  static Cb keep = () => { Tr("native atexit callback (registered with the host C library) ran"); NativePuts("stdout(native puts): from native atexit callback"); };
  static CbArg keepArg = a => { Tr("native __cxa_atexit callback ran"); NativePuts("stdout(native puts): from native atexit callback"); };
  static void RegisterNativeAtExit() {
    if (Win) Tr("_crt_atexit returned " + AtExitWin(Marshal.GetFunctionPointerForDelegate(keep)));
    else Tr("__cxa_atexit returned " + CxaAtExit(Marshal.GetFunctionPointerForDelegate(keepArg), IntPtr.Zero, IntPtr.Zero));
  }

  class Fin { readonly string n; public Fin(string n) { this.n = n; } ~Fin() { Tr("finalizer ran: " + n); } }
  static Fin reachable;

  static string mode;
  static int Main(string[] args) {
    MainThread = Thread.CurrentThread.ManagedThreadId;
    mode = args.Length > 0 ? args[0] : "ret";
    Tr("Main: mode=" + mode + " runtime=" + RuntimeInformation.FrameworkDescription);
    AppDomain.CurrentDomain.ProcessExit += OnExitA;
    AppDomain.CurrentDomain.ProcessExit += OnExitB;
    AppDomain.CurrentDomain.DomainUnload += (s, e) => Tr("DomainUnload ran");
    AppDomain.CurrentDomain.UnhandledException += (s, e) => Tr("UnhandledException ran (IsTerminating=" + e.IsTerminating + ")");
    switch (mode) {
      case "ret": Tr("Main returns 7"); return 7;
      case "ret300": Tr("Main returns 300"); return 300;
      case "envexit": Tr("Environment.Exit(7)"); Environment.Exit(7); break;
      case "envexit300": Tr("Environment.Exit(300)"); Environment.Exit(300); break;
      case "envexit-thread": {
        var t = new Thread(() => { Tr("Environment.Exit(7) from a second thread"); Environment.Exit(7); });
        t.Start(); Thread.Sleep(Timeout.Infinite); break;
      }
      case "exitcode": Environment.ExitCode = 5; Tr("Environment.ExitCode = 5, Main returns 7"); return 7;
      case "unhandled": Tr("throwing"); throw new InvalidOperationException("boom");
      case "nativeexit":
        RegisterNativeAtExit(); NativePuts("stdout(native puts): buffered line from Main");
        Tr("native exit(7)"); NativeExit(7); break;
      case "native_exit":
        RegisterNativeAtExit(); NativePuts("stdout(native puts): buffered line from Main");
        Tr("native _exit(7)"); if (Win) UExitWin(7); else UExitLinux(7); break;
      case "quick":
        NativePuts("stdout(native puts): buffered line from Main");
        Tr("native quick_exit(7)"); if (Win) QuickExitWin(7); else QuickExitLinux(7); break;
      case "abort":
        NativePuts("stdout(native puts): buffered line from Main");
        Tr("native abort()"); if (Win) AbortWin(); else AbortLinux(); break;
      case "exitprocess": RegisterNativeAtExit(); NativePuts("stdout(native puts): buffered line from Main"); Tr("kernel32 ExitProcess(7)"); ExitProcess(7); break;
      case "terminate": NativePuts("stdout(native puts): buffered line from Main"); Tr("kernel32 TerminateProcess(7)"); TerminateProcess(GetCurrentProcess(), 7); break;
      case "native-ret":
        RegisterNativeAtExit(); NativePuts("stdout(native puts): buffered line from Main");
        Tr("Main returns 7 after registering a native atexit callback"); return 7;
      case "native-envexit":
        RegisterNativeAtExit(); NativePuts("stdout(native puts): buffered line from Main");
        Tr("Environment.Exit(7) after registering a native atexit callback"); Environment.Exit(7); break;
      case "tick-envexit": Tr("Environment.Exit(7)"); Environment.Exit(7); break;
      case "tick": case "tick2": case "slow": case "envexit-in-pe": case "nativeexit-in-pe": case "late-subscribe": case "pe-exitcode":
      case "stdio-pe": case "bg-thread-in-pe":
        Tr("Main returns 7"); return 7;
      case "finalizer":
        reachable = new Fin("reachable from a static"); new Fin("unreachable");
        Tr("Main returns 7"); return 7;
      case "exit-in-callback": {
        IntPtr buf = Marshal.AllocHGlobal(16); Marshal.WriteInt32(buf, 0, 2); Marshal.WriteInt32(buf, 4, 1);
        IntPtr cmp = Marshal.GetFunctionPointerForDelegate(cmpKeep);
        Tr("calling native qsort");
        if (Win) QsortWin(buf, (UIntPtr)2, (UIntPtr)4, cmp); else QsortLinux(buf, (UIntPtr)2, (UIntPtr)4, cmp);
        Tr("qsort returned"); return 1;
      }
      case "exit-on-native-thread": {
        IntPtr p = Marshal.GetFunctionPointerForDelegate(procKeep);
        if (Win) CreateThread(IntPtr.Zero, UIntPtr.Zero, p, IntPtr.Zero, 0, IntPtr.Zero);
        else { IntPtr t; pthread_create(out t, IntPtr.Zero, p, IntPtr.Zero); }
        Tr("main sleeps 10 s"); Thread.Sleep(10000); Tr("main woke up"); return 1;
      }
      case "fg-thread": {
        var t = new Thread(() => { Thread.Sleep(3000); Tr("foreground thread done"); }); t.IsBackground = false; t.Start();
        Tr("Main returns 7 while a foreground thread sleeps 3 s"); return 7;
      }
      case "fg-thread-envexit": {
        var t = new Thread(() => { Thread.Sleep(3000); Tr("foreground thread done"); }); t.IsBackground = false; t.Start();
        Tr("Environment.Exit(7) while a foreground thread sleeps 3 s"); Environment.Exit(7); break;
      }
    }
    Tr("unknown mode"); return 1;
  }

  static void OnExitA(object s, EventArgs e) {
    Tr("ProcessExit handler A start (sender=" + (s == null ? "null" : s.GetType().Name) + ", ExitCode=" + Environment.ExitCode + ")");
    switch (mode) {
      case "tick": case "tick-envexit": case "tick2":
        for (int i = 1; i <= (mode == "tick2" ? 6 : 40); i++) { Thread.Sleep(250); Tr("A tick " + i); }
        break;
      case "slow": Tr("A sleeps 4000 ms"); Thread.Sleep(4000); Tr("A woke up"); break;
      case "envexit-in-pe": Tr("A calls Environment.Exit(9)"); Environment.Exit(9); Tr("A: Environment.Exit returned"); break;
      case "nativeexit-in-pe": Tr("A calls native exit(9)"); NativeExit(9); break;
      case "late-subscribe": AppDomain.CurrentDomain.ProcessExit += (x, y) => Tr("ProcessExit handler C (subscribed during ProcessExit) ran"); Tr("A subscribed C"); break;
      case "pe-exitcode": Environment.ExitCode = 9; Tr("A set Environment.ExitCode = 9"); break;
      case "stdio-pe": NativePuts("stdout(native puts): from ProcessExit handler A"); break;
      case "bg-thread-in-pe": {
        var t = new Thread(() => { Tr("thread started by A runs"); Thread.Sleep(500); Tr("thread started by A done"); });
        t.Start(); t.Join(); Tr("A joined its thread"); break;
      }
    }
    Tr("ProcessExit handler A end");
  }
  static void OnExitB(object s, EventArgs e) {
    Tr("ProcessExit handler B (ExitCode=" + Environment.ExitCode + ")");
    if (mode == "tick2") for (int i = 1; i <= 40; i++) { Thread.Sleep(250); Tr("B tick " + i); }
  }
}
