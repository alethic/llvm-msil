using System;
using System.Runtime.InteropServices;
static class P {
  [DllImport("ucrtbase", EntryPoint = "exit")] static extern void ExitWin(int code);
  [DllImport("libc", EntryPoint = "exit")] static extern void ExitLinux(int code);
  [DllImport("ucrtbase", EntryPoint = "_crt_atexit")] static extern int AtExitWin(IntPtr fn);
  [DllImport("libc", EntryPoint = "__cxa_atexit")] static extern int CxaAtExit(IntPtr fn, IntPtr arg, IntPtr dso);
  static int AtExitLinux(IntPtr fn) => CxaAtExit(fn, IntPtr.Zero, IntPtr.Zero);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Cb();
  static Cb keep = () => Console.WriteLine("native atexit handler ran");
  static void Main(string[] args) {
    bool win = Environment.OSVersion.Platform == PlatformID.Win32NT;
    AppDomain.CurrentDomain.ProcessExit += (s, e) => Console.WriteLine("ProcessExit ran");
    IntPtr fp = Marshal.GetFunctionPointerForDelegate(keep);
    if (win) AtExitWin(fp); else AtExitLinux(fp);
    Console.WriteLine("calling native exit(3)");
    Console.Out.Flush();
    if (win) ExitWin(3); else ExitLinux(3);
    Console.WriteLine("not reached");
  }
}
