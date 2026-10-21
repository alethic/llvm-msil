// E6b: a C# library that C code might want to call.
namespace Contoso.Managed
{
    public static unsafe class Api
    {
        public static long Add(long a, long b) => a + b;
        public static int Sum(int* p, int n) { int s = 0; for (int i = 0; i < n; i++) s += p[i]; return s; }
        public static nint Twice(nint x) => x * 2;
        public static int Len(string s) => s.Length;     // not expressible in C
    }
}
