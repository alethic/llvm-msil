using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
static class P {
  static UInt128 seed = new UInt128(0x9E3779B97F4A7C15ul, 0xD1B54A32D192ED03ul);
  static UInt128 Next() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
  [MethodImpl(MethodImplOptions.NoInlining)] static UInt128 Div(UInt128 a, UInt128 b) => a / b;
  static void Main() {
    var sw = Stopwatch.StartNew();
    UInt128 acc = 0;
    for (int i = 0; i < 3000000; i++) { UInt128 a = Next(), b = Next() >> (i % 100); if (b == 0) b = 7; acc += Div(a, b); }
    Console.WriteLine($"{acc:x32} {sw.ElapsedMilliseconds} ms");
  }
}
