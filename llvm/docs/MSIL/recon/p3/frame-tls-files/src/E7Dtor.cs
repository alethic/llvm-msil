// E7: native thread-exit callbacks into managed code. Rust std registers its TLS destructors with
// FlsAlloc(callback) on Windows and __cxa_thread_atexit_impl or pthread_key_create(destructor) on
// Linux; a C11 tss_create destructor or an exit hook for the TLS block would use the same.
// Does the callback (a delegate thunk) run, on which thread, and can it run managed code?
using System;
using System.Runtime.InteropServices;
using System.Threading;

static class E7Dtor {
  delegate void FlsCallback(IntPtr data);
  delegate void KeyDtor(IntPtr data);
  delegate uint WinStart(IntPtr arg);
  delegate IntPtr PosixStart(IntPtr arg);
  [DllImport("kernel32.dll")] static extern uint FlsAlloc(IntPtr cb);
  [DllImport("kernel32.dll")] static extern bool FlsSetValue(uint idx, IntPtr v);
  [DllImport("kernel32.dll")] static extern IntPtr CreateThread(IntPtr a, UIntPtr s, IntPtr start, IntPtr arg, uint f, IntPtr id);
  [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("libc.so.6")] static extern int pthread_key_create(out uint key, IntPtr dtor);
  [DllImport("libc.so.6")] static extern int pthread_setspecific(uint key, IntPtr v);
  [DllImport("libc.so.6")] static extern int pthread_create(out IntPtr t, IntPtr attr, IntPtr start, IntPtr arg);
  [DllImport("libc.so.6")] static extern int pthread_join(IntPtr t, IntPtr r);
  [DllImport("libc.so.6")] static extern IntPtr pthread_self();

  static int calls; static string log = "";
  [ThreadStatic] static int tsMark;
  static uint key;
  static readonly FlsCallback flsCb = Callback;
  static readonly KeyDtor keyCb = Callback;
  static void Callback(IntPtr data) {
    // Runs during thread exit. Use managed code: allocation, a string, a [ThreadStatic] read.
    var o = new object[4]; string s = string.Format("cb(data={0}, tsMark={1}, managedId={2})", data, tsMark, Thread.CurrentThread.ManagedThreadId);
    lock (typeof(E7Dtor)) { calls++; log += "  " + s + "\n"; }
  }
  static bool Win { get { return Environment.OSVersion.Platform == PlatformID.Win32NT; } }
  static void Register(int tag) {
    tsMark = tag;
    if (Win) FlsSetValue(key, (IntPtr)tag); else pthread_setspecific(key, (IntPtr)tag);
  }
  static readonly WinStart ws = a => { Register((int)a); return 0; };
  static readonly PosixStart ps = a => { Register((int)a); return IntPtr.Zero; };

  static int Main(string[] args) {
    string only = args.Length > 0 ? args[0] : "both";
    Console.WriteLine("runtime {0} ({1})", Environment.Version, RuntimeInformation.FrameworkDescription);
    if (Win) key = FlsAlloc(Marshal.GetFunctionPointerForDelegate(flsCb));
    else if (pthread_key_create(out key, Marshal.GetFunctionPointerForDelegate(keyCb)) != 0) throw new Exception("key");
    // 1. managed threads
    if (only != "native") for (int i = 1; i <= 3; i++) { var t = new Thread(() => Register(100 + i)); t.Start(); t.Join(); }
    Thread.Sleep(200);
    if (only != "native") Console.WriteLine("managed threads: {0} callbacks", calls); Console.Write(log); calls = 0; log = "";
    // 2. native threads whose start routine is a delegate thunk
    if (only != "managed") for (int i = 1; i <= 3; i++) {
      if (Win) { IntPtr h = CreateThread(IntPtr.Zero, UIntPtr.Zero, Marshal.GetFunctionPointerForDelegate(ws), (IntPtr)(200 + i), 0, IntPtr.Zero); WaitForSingleObject(h, 0xFFFFFFFF); }
      else { IntPtr t; pthread_create(out t, IntPtr.Zero, Marshal.GetFunctionPointerForDelegate(ps), (IntPtr)(200 + i)); pthread_join(t, IntPtr.Zero); }
    }
    Thread.Sleep(200);
    Console.WriteLine("native threads: {0} callbacks", calls); Console.Write(log);
    return 0;
  }
}
