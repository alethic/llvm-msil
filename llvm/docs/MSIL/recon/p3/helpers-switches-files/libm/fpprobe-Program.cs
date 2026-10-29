// Prints bit patterns of System.Math results for inputs where C libraries and .NET may differ.
using System;
using System.Globalization;
static class P {
  static string H(double d) => BitConverter.DoubleToInt64Bits(d).ToString("x16");
  static double D(ulong u) => BitConverter.Int64BitsToDouble((long)u);
  static void O(string n, double r) => Console.WriteLine(n + " " + H(r));
  static void Main() {
    double nz = D(0x8000000000000000), qnan = D(0x7ff8000000000000), nnan = D(0xfff8000000000000), pl = D(0x7ff4000000000123);
    double[] rin = { 0.5, 1.5, 2.5, -0.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, 4503599627370497.0, -0.4, nz, 1e300, D(1), qnan, nnan };
    for (int i = 0; i < rin.Length; i++) {
      double x = rin[i];
      O($"round_even[{i}]", Math.Round(x));
      O($"round_away[{i}]", Math.Round(x, MidpointRounding.AwayFromZero));
      O($"floor[{i}]", Math.Floor(x)); O($"ceil[{i}]", Math.Ceiling(x)); O($"trunc[{i}]", Math.Truncate(x));
      O($"abs[{i}]", Math.Abs(x)); O($"sqrt[{i}]", Math.Sqrt(x));
    }
    O("abs_nnan", Math.Abs(nnan)); O("abs_payload", Math.Abs(D(0xfff4000000000123)));
    O("sqrt_neg", Math.Sqrt(-1.0)); O("sqrt_payload", Math.Sqrt(pl));
    // min/max: signed zeros and NaN
    O("min_-0_+0", Math.Min(nz, 0.0)); O("min_+0_-0", Math.Min(0.0, nz));
    O("max_-0_+0", Math.Max(nz, 0.0)); O("max_+0_-0", Math.Max(0.0, nz));
    O("min_nan_1", Math.Min(qnan, 1.0)); O("min_1_nan", Math.Min(1.0, qnan));
    // remainder: CIL rem (C# %) vs IEEERemainder
    double[,] rp = { { 5.1, 3.0 }, { -5.1, 3.0 }, { 1e308, 3.0 }, { 1e308, 1e-308 }, { 7.0, 2.0 }, { -7.0, 2.0 }, { 0.0, 0.0 }, { 1.0, 0.0 }, { double.PositiveInfinity, 2.0 }, { 3.0, double.PositiveInfinity }, { nz, 1.0 }, { 6.0, 4.0 } };
    for (int i = 0; i < rp.GetLength(0); i++) { O($"rem[{i}]", rp[i, 0] % rp[i, 1]); O($"ieeerem[{i}]", Math.IEEERemainder(rp[i, 0], rp[i, 1])); }
    // transcendental: known-hard inputs
    double[] tin = { 1e22, D(0x3ff921fb54442d18), 1.5707963267948966, 3.141592653589793, 0.5, 2.0, 1e-300, 709.782712893384, -745.1332191019411, 100.0, 1e300, 0.7853981633974483, 12345.6789, D(0x3ff921fb54442d19), 0.1 };
    for (int i = 0; i < tin.Length; i++) {
      double x = tin[i];
      O($"sin[{i}]", Math.Sin(x)); O($"cos[{i}]", Math.Cos(x)); O($"tan[{i}]", Math.Tan(x));
      O($"exp[{i}]", Math.Exp(x)); O($"log[{i}]", Math.Log(x)); O($"log10[{i}]", Math.Log10(x));
      O($"atan[{i}]", Math.Atan(x)); O($"sinh[{i}]", Math.Sinh(x)); O($"tanh[{i}]", Math.Tanh(x));
      O($"asin[{i}]", Math.Asin(x / 1e23)); O($"acos[{i}]", Math.Acos(x / 1e23));
      O($"pow[{i}]", Math.Pow(x, 1.0 / 3.0)); O($"pow2[{i}]", Math.Pow(1.0000001, x));
      O($"atan2[{i}]", Math.Atan2(x, 3.0));
    }
    O("pow_m1_inf", Math.Pow(-1.0, double.PositiveInfinity)); O("pow_1_nan", Math.Pow(1.0, qnan)); O("pow_nan_0", Math.Pow(qnan, 0.0));
    O("pow_m8_third", Math.Pow(-8.0, 1.0 / 3.0)); O("log_neg", Math.Log(-1.0)); O("log_0", Math.Log(0.0));
#if NETCOREAPP
    O("cbrt27", Math.Cbrt(27.0)); O("cbrt_m8", Math.Cbrt(-8.0));
    O("fma", Math.FusedMultiplyAdd(0.1, 10.0, -1.0)); O("fma_unfused", 0.1 * 10.0 - 1.0);
    O("copysign", Math.CopySign(3.0, nz)); O("copysign_nan", Math.CopySign(qnan, -1.0));
    O("scaleb", Math.ScaleB(1.5, 1030)); O("log2", Math.Log2(8.0)); O("bitdec", Math.BitDecrement(1.0));
    O("maxnumber_nan", double.MaxNumber(qnan, 1.0)); O("maxnumber_-0+0", double.MaxNumber(nz, 0.0));
    float fx = 1e10f; Console.WriteLine("sinf " + BitConverter.SingleToInt32Bits(MathF.Sin(fx)).ToString("x8") + " via-double " + BitConverter.SingleToInt32Bits((float)Math.Sin(fx)).ToString("x8"));
    Console.WriteLine("expf " + BitConverter.SingleToInt32Bits(MathF.Exp(88.7f)).ToString("x8") + " via-double " + BitConverter.SingleToInt32Bits((float)Math.Exp(88.7f)).ToString("x8"));
    Console.WriteLine("powf " + BitConverter.SingleToInt32Bits(MathF.Pow(1.0000001f, 1e7f)).ToString("x8") + " via-double " + BitConverter.SingleToInt32Bits((float)Math.Pow(1.0000001f, 1e7f)).ToString("x8"));
#endif
    Console.WriteLine("runtime " + Environment.Version);
  }
}
