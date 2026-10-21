// E6: a C# program calling a library built by this toolchain through its export class.
using System;
using System.Runtime.InteropServices;
using Contoso.Zlib;

unsafe static class Program
{
    static int Main()
    {
        Console.WriteLine($"C#: add(2, 3) = {Native.add(2, 3)}");
        int* counter = (int*)Native.counter;          // exported data: property -> address
        *counter = 40;
        Console.WriteLine($"C#: bump() after *counter = 40 -> {Native.bump()}");
        delegate*<int, int> f = (delegate*<int, int>)Native.get_twice();   // C function pointer (managed in Phase 2)
        Console.WriteLine($"C#: get_twice()(21) = {f(21)}");
        Console.WriteLine($"C#: get_twice() == <msil>addr_twice: {Native.get_twice() == (IntPtr)typeof(Native).GetMethod("<msil>addr_twice").Invoke(null, null)}");
        IntPtr s = Marshal.StringToHGlobalAnsi("hello, world");
        Console.WriteLine($"C#: length(\"hello, world\") = {Native.length(s)}");
        Marshal.FreeHGlobal(s);
        return 0;
    }
}
