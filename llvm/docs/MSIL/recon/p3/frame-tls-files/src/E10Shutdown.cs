// E10: at process exit, are finalizers of still-reachable [ThreadStatic] holders run (and while a
// background thread still uses its block)? Each finalizer appends a line to the file given as argv[0].
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
sealed class Holder {
  public string Who; public IntPtr Block;
  ~Holder() { try { File.AppendAllText(E10Shutdown.LogPath, string.Format("finalizer({0}) HasShutdownStarted={1}\n", Who, Environment.HasShutdownStarted)); } catch { } }
}
unsafe static class E10Shutdown {
  public static string LogPath;
  [ThreadStatic] static Holder H;
  static void Touch(string who) { H = new Holder { Who = who, Block = Marshal.AllocHGlobal(64) }; }
  static int Main(string[] a) {
    LogPath = a[0]; File.WriteAllText(LogPath, "runtime " + Environment.Version + "\n");
    Touch("main thread");
    var t = new Thread(() => { Touch("background thread, still running"); long* p = (long*)H.Block; while (true) { (*p)++; } });
    t.IsBackground = true; t.Start(); Thread.Sleep(100);
    var u = new Thread(() => Touch("finished thread")); u.Start(); u.Join();
    return 0;   // no GC.Collect: whatever runs now runs at shutdown
  }
}
