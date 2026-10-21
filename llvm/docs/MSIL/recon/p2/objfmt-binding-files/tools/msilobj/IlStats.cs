// IlStats.cs: token density of IL bodies in assemblies (what a wasm-style object would need as relocations).
using System.Collections.Immutable;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

static class IlStats
{
    public static int Run(string[] files)
    {
        long il = 0, methods = 0, tokens = 0, instrs = 0, estUleb = 0;
        var byTable = new Dictionary<string, long>();
        foreach (var f in files)
        {
            using var pe = new PEReader(File.OpenRead(f));
            var md = pe.GetMetadataReader();
            foreach (var h in md.MethodDefinitions)
            {
                var m = md.GetMethodDefinition(h);
                if (m.RelativeVirtualAddress == 0) continue;
                var b = pe.GetMethodBody(m.RelativeVirtualAddress).GetILBytes()!;
                methods++; il += b.Length;
                instrs += IL.Walk(b, (at, op, tok) =>
                {
                    tokens++;
                    string t = (tok >> 24) == 0x70 ? "UserString" : ((TableIndex)(tok >> 24)).ToString();
                    byTable[t] = byTable.GetValueOrDefault(t) + 1;
                    // wasm-style relocation record: type (1) + ULEB offset + ULEB symbol index
                    estUleb += 1 + Uleb(at) + Uleb(tok & 0xffffff);
                });
            }
        }
        Console.WriteLine($"files {files.Length}, methods {methods}, IL bytes {il}, instructions {instrs}, token sites {tokens} ({(double)tokens / Math.Max(1, il) * 100:F1} per 100 IL bytes, {(double)tokens * 4 / Math.Max(1, il) * 100:F1}% of IL bytes are tokens)");
        Console.WriteLine("  by table: " + string.Join(", ", byTable.OrderByDescending(k => k.Value).Select(k => $"{k.Key} {k.Value}")));
        Console.WriteLine($"  estimated wasm-style relocation records: {estUleb} bytes ({(double)estUleb / Math.Max(1, il) * 100:F1}% of IL size)");
        return 0;
    }
    static int Uleb(long v) { int n = 1; while (v >= 0x80) { v >>= 7; n++; } return n; }
}
