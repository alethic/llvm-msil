// VaProbe: how the CLR passes C variadic arguments to native code.
// Built with the .NET Framework csc (C# 5), references mscorlib only, so the
// same image runs on .NET Framework 4.8 and, with a runtimeconfig, on .NET 8/10
// (Windows and Linux).
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

static unsafe class Win {
  // Variadic: the CLR vararg calling convention (__arglist).
  [DllImport("msvcrt.dll", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_va(IntPtr b, IntPtr f, __arglist);
  // Fixed-arity P/Invokes of the variadic sprintf, one per call-site shape.
  [DllImport("msvcrt.dll", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_i_d_p_d(IntPtr b, IntPtr f, int a, double c, IntPtr s, double d);
  [DllImport("msvcrt.dll", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_d_d_i_d(IntPtr b, IntPtr f, double a, double c, int i, double d);
  // Same, with the FP values passed as their 64-bit patterns (integer registers).
  [DllImport("msvcrt.dll", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_l_l_i_l(IntPtr b, IntPtr f, long a, long c, int i, long d);
  // v-variants.
  [DllImport("msvcrt.dll", EntryPoint = "vsprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int vsprintf(IntPtr b, IntPtr f, IntPtr va);
  [DllImport("ucrtbase.dll", EntryPoint = "__stdio_common_vsprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int stdio_common_vsprintf(ulong opts, IntPtr b, IntPtr n, IntPtr f, IntPtr loc, IntPtr va);
}

static unsafe class Lnx {
  [DllImport("libc.so.6", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_va(IntPtr b, IntPtr f, __arglist);
  [DllImport("libc.so.6", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_i_d_p_d(IntPtr b, IntPtr f, int a, double c, IntPtr s, double d);
  [DllImport("libc.so.6", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_d_d_i_d(IntPtr b, IntPtr f, double a, double c, int i, double d);
  [DllImport("libc.so.6", EntryPoint = "sprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int sprintf_d(IntPtr b, IntPtr f, double a);
  [DllImport("libc.so.6", EntryPoint = "vsprintf", CallingConvention = CallingConvention.Cdecl)]
  public static extern int vsprintf(IntPtr b, IntPtr f, IntPtr va);
  // AL probe (libalprobe.so: movzbl %al,%eax; ret).
  [DllImport("libalprobe.so", EntryPoint = "al_probe", CallingConvention = CallingConvention.Cdecl)]
  public static extern int al_d(double a);
  [DllImport("libalprobe.so", EntryPoint = "al_probe", CallingConvention = CallingConvention.Cdecl)]
  public static extern int al_i(int a);
  [DllImport("libalprobe.so", EntryPoint = "al_probe", CallingConvention = CallingConvention.Cdecl)]
  public static extern int al_ddd(double a, double b, double c);
  [DllImport("libalprobe.so", EntryPoint = "al_probe", CallingConvention = CallingConvention.Cdecl)]
  public static extern int al_pd(IntPtr p, double a);
  [DllImport("libalprobe.so", EntryPoint = "al_probe_z", CallingConvention = CallingConvention.Cdecl)]
  public static extern int alz_d(double a);
  [DllImport("libalprobe.so", EntryPoint = "va_probe_z", CallingConvention = CallingConvention.Cdecl)]
  public static extern double vaz_d(double a);
  [DllImport("libalprobe.so", EntryPoint = "va_probe_nz", CallingConvention = CallingConvention.Cdecl)]
  public static extern double vanz_d(double a);
}

static unsafe class P {
  static bool unix = Environment.OSVersion.Platform == PlatformID.Unix;
  static IntPtr buf = Marshal.AllocHGlobal(512);
  static IntPtr S(string s) { return Marshal.StringToHGlobalAnsi(s); }
  static string B() { return Marshal.PtrToStringAnsi(buf); }
  static void R(string name, Func<string> f) {
    string r;
    try { r = f(); } catch (Exception e) { r = "EXCEPTION " + e.GetType().FullName + ": " + e.Message.Replace("\n", " ").Replace("\r", ""); }
    Console.WriteLine(name + ": " + r);
  }

  // A native va_list over an argument buffer of 8-byte slots (ints widened to
  // 64 bits, doubles as IEEE bits, pointers): Win64 char*, or the SysV
  // __va_list_tag with gp_offset=48 and fp_offset=176 (registers exhausted)
  // and overflow_arg_area pointing at the buffer.
  static IntPtr BuildVaList(long[] slots) {
    IntPtr area = Marshal.AllocHGlobal(slots.Length * 8 + 8);
    for (int i = 0; i < slots.Length; i++) Marshal.WriteInt64(area, i * 8, slots[i]);
    if (!unix) return area;
    IntPtr tag = Marshal.AllocHGlobal(24);
    Marshal.WriteInt32(tag, 0, 48);
    Marshal.WriteInt32(tag, 4, 176);
    Marshal.WriteIntPtr(tag, 8, area);
    Marshal.WriteIntPtr(tag, 16, IntPtr.Zero);
    return tag;
  }
  // SysV only: a va_list whose first ints and doubles are in a register save
  // area (as a native va_start leaves it), the rest in the overflow area.
  static IntPtr BuildVaListRegs(long[] gp, double[] fp, long[] overflow, int fixedGp) {
    IntPtr rsa = Marshal.AllocHGlobal(176);
    for (int i = 0; i < 176; i += 8) Marshal.WriteInt64(rsa, i, unchecked((long)0xDEADBEEFDEADBEEF));
    for (int i = 0; i < gp.Length; i++) Marshal.WriteInt64(rsa, (fixedGp + i) * 8, gp[i]);
    for (int i = 0; i < fp.Length; i++) Marshal.WriteInt64(rsa, 48 + i * 16, BitConverter.DoubleToInt64Bits(fp[i]));
    IntPtr area = Marshal.AllocHGlobal(overflow.Length * 8 + 8);
    for (int i = 0; i < overflow.Length; i++) Marshal.WriteInt64(area, i * 8, overflow[i]);
    IntPtr tag = Marshal.AllocHGlobal(24);
    Marshal.WriteInt32(tag, 0, fixedGp * 8);
    Marshal.WriteInt32(tag, 4, 48);
    Marshal.WriteIntPtr(tag, 8, area);
    Marshal.WriteIntPtr(tag, 16, rsa);
    return tag;
  }
  static long D(double d) { return BitConverter.DoubleToInt64Bits(d); }

  static int Main(string[] args) {
    Console.WriteLine("runtime: " + Environment.Version + " " + (unix ? "unix" : "windows") + " " + IntPtr.Size * 8 + "-bit");
    IntPtr hello = S("hello");
    IntPtr f1 = S("%d|%f|%s|%f");
    IntPtr f2 = S("%f|%f|%d|%f");
    IntPtr f3 = S("%d|%f|%s|%f|%lld|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f");
    long big = 1L << 40;
    const string want1 = "42|2.500000|hello|-1.250000";
    const string want2 = "2.500000|-1.250000|7|0.125000";
    Console.WriteLine("expect1: " + want1);
    Console.WriteLine("expect2: " + want2);

    // A. The CLR vararg calling convention.
    R("A1 vararg  i,d,p,d", () => { if (unix) Lnx.sprintf_va(buf, f1, __arglist(42, 2.5, hello, -1.25)); else Win.sprintf_va(buf, f1, __arglist(42, 2.5, hello, -1.25)); return B(); });
    R("A2 vararg  d,d,i,d", () => { if (unix) Lnx.sprintf_va(buf, f2, __arglist(2.5, -1.25, 7, 0.125)); else Win.sprintf_va(buf, f2, __arglist(2.5, -1.25, 7, 0.125)); return B(); });

    // B. Fixed-arity P/Invokes of the variadic function.
    R("B1 fixed   i,d,p,d", () => { if (unix) Lnx.sprintf_i_d_p_d(buf, f1, 42, 2.5, hello, -1.25); else Win.sprintf_i_d_p_d(buf, f1, 42, 2.5, hello, -1.25); return B(); });
    R("B2 fixed   d,d,i,d", () => { if (unix) Lnx.sprintf_d_d_i_d(buf, f2, 2.5, -1.25, 7, 0.125); else Win.sprintf_d_d_i_d(buf, f2, 2.5, -1.25, 7, 0.125); return B(); });
    if (!unix)
      R("B3 fixed   l,l,i,l (FP bits in GPRs)", () => { Win.sprintf_l_l_i_l(buf, f2, D(2.5), D(-1.25), 7, D(0.125)); return B(); });

    // B4. Linux: repeat B2 and count wrong results (AL is not set by the CLR).
    if (unix) {
      int bad = 0, n = 2000;
      for (int i = 0; i < n; i++) { Lnx.sprintf_d_d_i_d(buf, f2, 2.5, -1.25, 7, 0.125); if (B() != want2) bad++; }
      Console.WriteLine("B4 fixed d,d,i,d x" + n + ": wrong " + bad + " (last: " + B() + ")");
      bad = 0;
      IntPtr ff = S("%f");
      for (int i = 0; i < n; i++) { Lnx.sprintf_d(buf, ff, 2.5); if (B() != "2.500000") bad++; }
      Console.WriteLine("B5 fixed d x" + n + ": wrong " + bad + " (last: " + B() + ")");
    }

    // C. Linux: the value of AL at the native entry of a fixed-arity P/Invoke.
    if (unix) {
      R("C1 AL", () => {
        var seen = new SortedDictionary<int, int>();
        Action<int> add = v => { int c; seen.TryGetValue(v, out c); seen[v] = c + 1; };
        for (int i = 0; i < 300; i++) { add(Lnx.al_d(1.0)); add(Lnx.al_i(1)); add(Lnx.al_ddd(1, 2, 3)); add(Lnx.al_pd(buf, 2.0)); }
        var parts = new List<string>();
        foreach (var kv in seen) parts.Add("0x" + kv.Key.ToString("x2") + "x" + kv.Value);
        return string.Join(" ", parts.ToArray());
      });
      R("C3 AL at al_probe_z (address low byte 0x00)", () => {
        var seen = new SortedDictionary<int, int>();
        for (int i = 0; i < 1200; i++) { int v = Lnx.alz_d(1.0); int c; seen.TryGetValue(v, out c); seen[v] = c + 1; }
        var parts = new List<string>();
        foreach (var kv in seen) parts.Add("0x" + kv.Key.ToString("x2") + "x" + kv.Value);
        return string.Join(" ", parts.ToArray());
      });
      R("C4 first variadic double read like gcc (AL test), x1200", () => {
        int badz = 0, badnz = 0;
        for (int i = 0; i < 1200; i++) { if (Lnx.vaz_d(2.5) != 2.5) badz++; if (Lnx.vanz_d(2.5) != 2.5) badnz++; }
        return "entry at ...00: wrong " + badz + "; entry at ...40: wrong " + badnz;
      });
      R("C2 AL per signature (after warm-up)", () => "d=0x" + Lnx.al_d(1.0).ToString("x2") + " i=0x" + Lnx.al_i(1).ToString("x2") + " ddd=0x" + Lnx.al_ddd(1, 2, 3).ToString("x2") + " pd=0x" + Lnx.al_pd(buf, 2).ToString("x2"));
    }

    // D. v-variants with a native va_list built over an argument buffer.
    long[] s1 = { 42, D(2.5), (long)hello, D(-1.25) };
    R("D1 vsprintf over buffer i,d,p,d", () => { if (unix) Lnx.vsprintf(buf, f1, BuildVaList(s1)); else Win.vsprintf(buf, f1, BuildVaList(s1)); return B(); });
    long[] s3 = { 42, D(2.5), (long)hello, D(-1.25), big, 1, 2, 3, 4, D(1), D(2), D(3), D(4), D(5), D(6), D(7), D(8), D(9) };
    IntPtr f3w = S("%d|%f|%s|%f|%I64d|%d|%d|%d|%d|%f|%f|%f|%f|%f|%f|%f|%f|%f");
    R("D2 vsprintf over buffer, 18 args", () => { if (unix) Lnx.vsprintf(buf, f3, BuildVaList(s3)); else Win.vsprintf(buf, f3w, BuildVaList(s3)); return B(); });
    if (!unix) {
      R("D3 ucrtbase __stdio_common_vsprintf over buffer", () => { Win.stdio_common_vsprintf(0, buf, (IntPtr)512, f3w, IntPtr.Zero, BuildVaList(s3)); return B(); });
    } else {
      // gp: 2 fixed (buf, fmt) + 4 variadic ints in the save area; fp: 3 doubles in the save area.
      IntPtr f4 = S("%d|%d|%d|%d|%f|%f|%f|%d|%f|%s");
      R("D3 vsprintf over a register save area + overflow", () => { Lnx.vsprintf(buf, f4, BuildVaListRegs(new long[] { 1, 2, 3, 4 }, new double[] { 0.5, 1.5, 2.5, 3.5 }, new long[] { 9, (long)hello }, 2)); return B(); });
    }
    return 0;
  }
}
