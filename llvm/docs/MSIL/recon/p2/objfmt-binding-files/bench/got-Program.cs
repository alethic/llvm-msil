// Option 5 (GOT-like table filled at startup) vs direct calls: the cost of turning every
// cross-object call into 'calli' through a table slot.
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;

static unsafe class P
{
    static int Twice(int x) => (x << 1) + 1;
    [MethodImpl(MethodImplOptions.NoInlining)] static int TwiceNoInline(int x) => (x << 1) + 1;
    static delegate*<int, int> got;          // the table slot (one per imported function)

    static int Direct(int n) { int acc = 0; for (int i = 0; i < n; i++) acc = Twice(acc ^ i); return acc; }
    static int DirectNoInline(int n) { int acc = 0; for (int i = 0; i < n; i++) acc = TwiceNoInline(acc ^ i); return acc; }
    static int ViaTable(int n) { int acc = 0; for (int i = 0; i < n; i++) acc = got(acc ^ i); return acc; }

    static void Main()
    {
        got = &Twice;                         // filled at startup (ldftn)
        const int N = 400_000_000;
        for (int round = 0; round < 3; round++)
        {
            var sw = Stopwatch.StartNew(); int a = Direct(N); long t1 = sw.ElapsedMilliseconds;
            sw.Restart(); int b = DirectNoInline(N); long t2 = sw.ElapsedMilliseconds;
            sw.Restart(); int c = ViaTable(N); long t3 = sw.ElapsedMilliseconds;
            Console.WriteLine($"round {round}: call (inlined) {t1} ms, call (not inlined) {t2} ms, calli via table {t3} ms  [{a} {b} {c}]");
        }
    }
}
