using System;
using System.Diagnostics;
using System.Numerics;
static class P {
    static int Main(string[] args) {
        int argc = args.Length + 1;
        var sw = Stopwatch.StartNew();
        Complex z = new Complex(0.5, 0.25), w = new Complex(1.0001, -0.0002 * argc), acc = Complex.Zero;
        for (int i = 0; i < 20000000; ++i) {
            z = z * w;
            acc += z / (w + 1.0);
            if (i % 1024 == 0) z = new Complex(0.5, 0.25);
        }
        Console.WriteLine(BitConverter.DoubleToInt64Bits(acc.Real).ToString("x16"));
        Console.WriteLine(BitConverter.DoubleToInt64Bits(acc.Imaginary).ToString("x16"));
        Console.WriteLine("ms " + sw.ElapsedMilliseconds);
        return 0;
    }
}
