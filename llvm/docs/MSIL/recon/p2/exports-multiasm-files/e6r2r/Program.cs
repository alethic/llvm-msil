// E6r2r: a C# app published ReadyToRun (crossgen2 also compiles the referenced C library zl.dll).
using System;
using Contoso.Zl;
unsafe static class Program
{
    static int Main()
    {
        Console.WriteLine("C#: main entered");
        int v = *(int*)Native.counter;          // the library's constructor sets counter = 100
        Console.WriteLine($"C#: counter = {v} (100 means the constructor ran first), twice(21) = {Native.twice(21)}");
        return v == 100 ? 0 : 1;
    }
}
