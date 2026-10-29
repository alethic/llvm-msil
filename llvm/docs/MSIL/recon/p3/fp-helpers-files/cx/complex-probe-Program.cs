// System.Numerics.Complex on the cases and matrix of cx.c (double only), same output format.
using System;
using System.Numerics;
using System.Text;

static class P
{
    static string H(double d) => BitConverter.DoubleToInt64Bits(d).ToString("x16");
    static double[] V;
    static ulong Hs = 0xcbf29ce484222325UL;
    static void Hadd(ulong v) { Hs = (Hs ^ v) * 0x100000001b3UL; Hs ^= Hs >> 29; }

    static int Main(string[] args)
    {
        double zero = 0.0;
        double inf = 1.0 / zero, nan = zero / zero;
        V = new double[] { 0.0, -0.0, 1.0, -1.0, 1.5, -2.25, 1e-310, 1e300, -1e300, inf, -inf, nan, 3.0, 1e-20 };
        string m = args.Length > 0 ? args[0] : "named";
        var sb = new StringBuilder();
        if (m[0] == 'n')
        {
            var cs = new (string n, double a, double b, double c, double d)[] {
                ("ordinary", 1.5, -2.25, -2.25, 1.5),
                ("(inf+nan i)*(1+1i)", inf, nan, 1, 1),
                ("(nan+inf i)*(2+0i)", nan, inf, 2, 0),
                ("(inf+0i)*(0+0i)", inf, 0, 0, 0),
                ("(1e300+1e300i)^2", 1e300, 1e300, 1e300, 1e300),
                ("(0+inf i)*(0+inf i)", 0, inf, 0, inf),
                ("(inf+inf i)*(nan+nan i)", inf, inf, nan, nan),
                ("(1+1i)/(0+0i)", 1, 1, 0, 0),
                ("(1+1i)/(-0+0i)", 1, 1, -0.0, 0),
                ("(1+1i)/(inf+nan i)", 1, 1, inf, nan),
                ("(1+1i)/(inf+inf i)", 1, 1, inf, inf),
                ("(inf+inf i)/(1+1i)", inf, inf, 1, 1),
                ("(inf+nan i)/(1+1i)", inf, nan, 1, 1),
                ("(1e300+1e300i)/(1e300+1i)", 1e300, 1e300, 1e300, 1),
                ("(1e-300+1e-300i)/(1e-300+1e-300i)", 1e-300, 1e-300, 1e-300, 1e-300),
                ("(1+1i)/(1e-310+1e-310i)", 1, 1, 1e-310, 1e-310),
                ("(3+4i)/(1e308+1e308i)", 3, 4, 1e308, 1e308),
                ("(1e308+1e308i)/(1e-308+1e-308i)", 1e308, 1e308, 1e-308, 1e-308),
                ("(nan+0i)/(0+0i)", nan, 0, 0, 0),
                ("(0+0i)/(0+0i)", 0, 0, 0, 0),
            };
            foreach (var c in cs)
            {
                var x = new Complex(c.a, c.b); var y = new Complex(c.c, c.d);
                var r = c.n.Contains("/") ? x / y : x * y;
                sb.Append(c.n).Append(" = ").Append(H(r.Real)).Append(' ').Append(H(r.Imaginary)).Append('\n');
            }
        }
        else
        {
            string[] ops = { "dmul", "ddiv" };
            for (int op = 0; op < 2; ++op)
            {
                Hs = 0xcbf29ce484222325UL;
                for (int a = 0; a < 14; ++a) for (int b = 0; b < 14; ++b) for (int c = 0; c < 14; ++c) for (int d = 0; d < 14; ++d)
                {
                    var x = new Complex(V[a], V[b]); var y = new Complex(V[c], V[d]);
                    var r = op == 0 ? x * y : x / y;
                    ulong r1 = (ulong)BitConverter.DoubleToInt64Bits(r.Real), r2 = (ulong)BitConverter.DoubleToInt64Bits(r.Imaginary);
                    Hadd(r1); Hadd(r2);
                    if (m[0] == 'm')
                        sb.Append(ops[op]).Append(' ').Append((((a * 14 + b) * 14 + c) * 14 + d).ToString("x5")).Append(' ').Append(r1.ToString("x16")).Append(' ').Append(r2.ToString("x16")).Append('\n');
                }
                if (m[0] != 'm') sb.Append(ops[op]).Append(" hash ").Append(Hs.ToString("x16")).Append('\n');
            }
        }
        Console.Out.Write(sb.ToString());
        return 0;
    }
}
