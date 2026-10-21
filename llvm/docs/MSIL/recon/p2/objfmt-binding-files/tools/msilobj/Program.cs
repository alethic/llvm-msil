// msilobj: Phase-2 binding experiments for the MSIL backend (objfmt-binding recon).
//   msilobj link -o <out.dll|out.exe> [--exe] [--trace-relocs] [--import name=lib ...] <objects and archives>
//   msilobj ilstats <assemblies...>
static class Program
{
    static int Main(string[] args)
    {
        try
        {
            switch (args[0])
            {
                case "link": return LinkCmd(args[1..]);
                case "ilstats": return IlStats.Run(args[1..]);
                case "carriers": return Carriers.Run(args[1..]);
                default: Console.Error.WriteLine("unknown command " + args[0]); return 2;
            }
        }
        catch (Exception e) { Console.Error.WriteLine("msilobj: " + e); return 3; }
    }

    static int LinkCmd(string[] a)
    {
        string outp = "a.dll"; bool exe = false, trace = false; var inputs = new List<string>(); RefIndex refs = null;
        var imports = new Dictionary<string, string>();
        string lib = "ucrtbase", libm = "ucrtbase";
        foreach (var x in a) if (x == "--linux") { lib = "libc"; libm = "libm.so.6"; }
        foreach (var n in new[] { "puts", "putchar" }) imports[n] = lib;
        foreach (var n in ("sqrt cbrt floor ceil trunc round rint nearbyint fmod remainder fma fmin fmax fdim sin cos tan asin acos atan atan2 " +
                           "sinh cosh tanh exp exp2 expm1 log log2 log10 log1p pow ldexp frexp modf hypot").Split(' '))
        { imports[n] = libm; imports[n + "f"] = libm; }
        for (int i = 0; i < a.Length; i++)
        {
            if (a[i] == "-o") outp = a[++i];
            else if (a[i] == "--exe") exe = true;
            else if (a[i] == "--linux") { }
            else if (a[i] == "--ref") { refs ??= new RefIndex(); refs.Add(a[++i]); }
            else if (a[i] == "--trace-relocs") trace = true;
            else if (a[i] == "--import") { var kv = a[++i].Split('='); imports[kv[0]] = kv[1]; }
            else inputs.Add(a[i]);
        }
        var l = new Linker(imports) { TraceRelocs = trace, Refs = refs };
        foreach (var f in inputs) l.AddInput(f);
        return l.Link(outp, exe);
    }
}
