// A C# module that calls a C function by the same placeholder convention a
// B object uses for undefined C functions (ImplMap stub on ModuleRef
// msil.unresolved), and offers a public static method for C to call.
using System.Runtime.InteropServices;

public static class CsLib
{
    [DllImport("msil.unresolved", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    static extern int twice(int x);              // defined in C (b.o)

    public static int cs_triple_twice(int x) => 3 * twice(x);
}
