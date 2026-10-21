using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
static class P
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static int F() => 0;
    static int Main() => 42;
}
