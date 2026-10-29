using System;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

class LjEx : Exception { public LjEx() : base("longjmp") { } }

unsafe static class P {
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Cmp(IntPtr a, IntPtr b);
  [DllImport("ucrtbase.dll", EntryPoint = "qsort", CallingConvention = CallingConvention.Cdecl)]
  static extern void qsort_win(IntPtr b, UIntPtr n, UIntPtr s, IntPtr cmp);
  [DllImport("libc.so.6", EntryPoint = "qsort", CallingConvention = CallingConvention.Cdecl)]
  static extern void qsort_lin(IntPtr b, UIntPtr n, UIntPtr s, IntPtr cmp);
  static bool IsUnix => Environment.OSVersion.Platform == PlatformID.Unix;
  static void qsort(IntPtr b, int n, int s, IntPtr cmp) {
    if (IsUnix) qsort_lin(b, (UIntPtr)n, (UIntPtr)s, cmp); else qsort_win(b, (UIntPtr)n, (UIntPtr)s, cmp);
  }
  static int calls, throwAt;
  static int CmpImpl(IntPtr a, IntPtr b) {
    if (++calls == throwAt) throw new LjEx();
    return Marshal.ReadInt32(a) - Marshal.ReadInt32(b);
  }
  static readonly Cmp keep = CmpImpl;
#if NET
  [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
  static int CmpUco(IntPtr a, IntPtr b) => CmpImpl(a, b);
#endif
  static bool Note(string s) { Console.WriteLine("  filter ran: " + s); return true; }

  static IntPtr MakeArray(int n) {
    IntPtr p = Marshal.AllocHGlobal(n * 4);
    for (int i = 0; i < n; i++) Marshal.WriteInt32(p, i * 4, (i * 7919) % n);
    return p;
  }
  static void Run(string mode, IntPtr fp) {
    IntPtr arr = MakeArray(64);
    calls = 0; throwAt = 5;
    try {
      qsort(arr, 64, 4, fp);
      Console.WriteLine(mode + ": no exception (calls=" + calls + ")");
    } catch (LjEx e) when (Note(mode)) {
      Console.WriteLine(mode + ": caught " + e.GetType().Name + " after " + calls + " comparator calls");
    }
    // Sort again without throwing: is the runtime still sane?
    calls = 0; throwAt = -1;
    qsort(arr, 64, 4, fp);
    bool sorted = true;
    for (int i = 1; i < 64; i++) if (Marshal.ReadInt32(arr, i * 4) < Marshal.ReadInt32(arr, (i - 1) * 4)) sorted = false;
    Console.WriteLine(mode + ": second sort " + (sorted ? "sorted" : "NOT sorted") + ", calls=" + calls);
    GC.Collect(); GC.WaitForPendingFinalizers();
    Console.WriteLine(mode + ": GC ok");
  }
  // Managed code calls a native-callable pointer (calli unmanaged) with no native frame in between.
  static void CalliRun(string mode, IntPtr fp) {
    IntPtr a = MakeArray(2);
    calls = 0; throwAt = 1;
    try {
#if NET
      int r = ((delegate* unmanaged[Cdecl]<IntPtr, IntPtr, int>)fp)(a, a + 4);
#else
      int r = CallViaDelegate(fp, a);
#endif
      Console.WriteLine(mode + ": no exception " + r);
    } catch (LjEx e) when (Note(mode)) {
      Console.WriteLine(mode + ": caught " + e.GetType().Name);
    }
    GC.Collect(); GC.WaitForPendingFinalizers();
    Console.WriteLine(mode + ": GC ok");
  }
#if !NET
  static int CallViaDelegate(IntPtr fp, IntPtr a) {
    var d = (Cmp)Marshal.GetDelegateForFunctionPointer(fp, typeof(Cmp));
    return d(a, a + 4);
  }
#endif
  static int Main(string[] args) {
    Console.WriteLine(RuntimeInformation.FrameworkDescription + " " + RuntimeInformation.OSDescription.Split('#')[0].Trim() + " " + RuntimeInformation.ProcessArchitecture);
    string m = args.Length > 0 ? args[0] : "delegate";
    if (m == "delegate") Run("delegate", Marshal.GetFunctionPointerForDelegate(keep));
#if NET
    else if (m == "uco") Run("uco", (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, int>)&CmpUco);
#endif
    else if (m == "calli-delegate") CalliRun("calli-delegate", Marshal.GetFunctionPointerForDelegate(keep));
#if NET
    else if (m == "calli-uco") CalliRun("calli-uco", (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, int>)&CmpUco);
#endif
    else { Console.WriteLine("unknown mode " + m); return 2; }
    Console.WriteLine("exit normally");
    return 0;
  }
}
