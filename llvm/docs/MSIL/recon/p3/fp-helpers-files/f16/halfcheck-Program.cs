// System.Half -> float for all 65,536 halves against compiler-rt's __extendhfsf2 rule
// (exact; NaN payload shifted left by 13, quiet bit NOT forced), and timing of 2^30 conversions.
using System;
using System.Diagnostics;
static class P {
    static uint CrtExtend(ushort h) {
        uint sign = (uint)(h & 0x8000) << 16, exp = (uint)(h >> 10) & 31, man = (uint)h & 0x3ff;
        if (exp == 31) return sign | 0x7f800000u | (man << 13);
        if (exp == 0) {
            if (man == 0) return sign;
            int e = -1; do { man <<= 1; e++; } while ((man & 0x400) == 0);
            return sign | (uint)(112 - e) << 23 | ((man & 0x3ff) << 13);
        }
        return sign | (exp + 112) << 23 | (man << 13);
    }
    static int Main() {
        int diff = 0, snan = 0, diffNotSnan = 0;
        for (int i = 0; i < 65536; ++i) {
            ushort h = (ushort)i;
            uint net = BitConverter.SingleToUInt32Bits((float)BitConverter.UInt16BitsToHalf(h));
            uint crt = CrtExtend(h);
            bool isSnan = ((h >> 10) & 31) == 31 && (h & 0x3ff) != 0 && (h & 0x200) == 0;
            if (isSnan) snan++;
            if (net != crt) { diff++; if (!isSnan) diffNotSnan++; }
        }
        Console.WriteLine($"{Environment.Version}: half->float differs from compiler-rt on {diff} of 65536 inputs; sNaN inputs {snan}; differing non-sNaN {diffNotSnan}");
        var sw = Stopwatch.StartNew(); uint acc = 0;
        for (uint u = 0; u < (1u << 30); ++u) acc += BitConverter.HalfToUInt16Bits((Half)BitConverter.UInt32BitsToSingle(u * 4 + 1));
        Console.WriteLine($"  2^30 float->Half conversions in C#: {sw.ElapsedMilliseconds} ms (acc {acc})");
        return 0;
    }
}
