// C# consumer of the ILAsm-built e09_modinit.dll: calls the export class directly.
using System;
using System.Runtime.InteropServices;
static class Program
{
    static int Main()
    {
        int c = Exports.get_counter();            // must trigger <Module>::.cctor of e09_modinit
        int f = Exports.call_fp(5);
        string s = Marshal.PtrToStringAnsi(Exports.msg());
        Console.WriteLine($"C#: counter={c} call_fp(5)={f} msg={s}");
        Console.WriteLine($"C#: typeof(Exports).Assembly={typeof(Exports).Assembly.GetName().Name}");
        return (c == 5 && f == 25 && s == "seg-ok") ? 0 : 1;
    }
}
