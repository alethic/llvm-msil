// E3 reader: finds the P2-C symbol table of an assembly (assembly-level custom
// attribute '<msil>SymbolTable', manifest resource 'msil.symtab', or metadata
// stream '#MSIL'), checks its triple against the link triple and decodes it, the
// way msil-ld would. Also prints metadata statistics.
//
//   symtab stats <dll>
//   symtab read <dll> <link-triple> <a|b|t>     (a: self-contained, b: row refs, t: text)
//   symtab attrs <dll>                          (Assembly.GetCustomAttributes through reflection)
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class P
{
    static int Main(string[] a)
    {
        switch (a[0])
        {
            case "stats": return Stats(a[1]);
            case "read": return Read(a[1], a[2], a[3]);
            case "attrs": return Attrs(a[1]);
        }
        return 2;
    }

    static int Stats(string path)
    {
        using var fs = File.OpenRead(path);
        using var pe = new PEReader(fs);
        var md = pe.GetMetadata();
        var r = pe.GetMetadataReader();
        Console.WriteLine($"file {new FileInfo(path).Length} bytes, metadata {md.Length} bytes");
        foreach (var (name, off, size) in Streams(md.GetContent().ToArray()))
            Console.WriteLine($"  stream {name,-9} {size,8} bytes");
        foreach (var t in new[] { TableIndex.TypeDef, TableIndex.MethodDef, TableIndex.Field, TableIndex.Property, TableIndex.CustomAttribute, TableIndex.MemberRef, TableIndex.ManifestResource })
            Console.WriteLine($"  table {t,-16} {r.GetTableRowCount(t),6} rows x {r.GetTableRowSize(t)} bytes");
        return 0;
    }

    // Stream headers of a metadata root (ECMA-335 II.24.2.1-2).
    static List<(string, int, int)> Streams(byte[] m)
    {
        var res = new List<(string, int, int)>();
        int verLen = BitConverter.ToInt32(m, 12);
        int p = 16 + verLen + 2;
        int n = BitConverter.ToUInt16(m, p); p += 2;
        for (int i = 0; i < n; i++)
        {
            int off = BitConverter.ToInt32(m, p), size = BitConverter.ToInt32(m, p + 4);
            p += 8;
            int e = Array.IndexOf(m, (byte)0, p);
            string name = Encoding.ASCII.GetString(m, p, e - p);
            p = (e + 4) & ~3;
            res.Add((name, off, size));
        }
        return res;
    }

    static byte[] FindTable(PEReader pe, MetadataReader r, out string where)
    {
        // 1. assembly attribute whose constructor belongs to '<msil>SymbolTable'
        foreach (var h in r.GetAssemblyDefinition().GetCustomAttributes())
        {
            var ca = r.GetCustomAttribute(h);
            if (ca.Constructor.Kind != HandleKind.MethodDefinition) continue;
            var ctor = r.GetMethodDefinition((MethodDefinitionHandle)ca.Constructor);
            var type = r.GetTypeDefinition(ctor.GetDeclaringType());
            if (!r.StringComparer.Equals(type.Name, "<msil>SymbolTable") || !type.Namespace.IsNil) continue;
            var b = r.GetBlobReader(ca.Value);
            if (b.ReadUInt16() != 1) throw new BadImageFormatException("bad prolog");
            int len = b.ReadInt32();
            where = "custom attribute";
            return b.ReadBytes(len);
        }
        // 2. manifest resource 'msil.symtab'
        foreach (var h in r.ManifestResources)
        {
            var res = r.GetManifestResource(h);
            if (!r.StringComparer.Equals(res.Name, "msil.symtab") || !res.Implementation.IsNil) continue;
            var dir = pe.PEHeaders.CorHeader.ResourcesDirectory;
            var block = pe.GetSectionData(dir.RelativeVirtualAddress);
            var br = block.GetReader((int)res.Offset, block.Length - (int)res.Offset);
            int len = br.ReadInt32();
            where = "manifest resource";
            return br.ReadBytes(len);
        }
        // 3. metadata stream '#MSIL'
        var md = pe.GetMetadata().GetContent().ToArray();
        foreach (var (name, off, size) in Streams(md))
            if (name == "#MSIL") { where = "metadata stream"; return md.AsSpan(off, size).ToArray(); }
        where = null;
        return null;
    }

    static uint Cu(byte[] b, ref int p)
    {
        byte x = b[p];
        if ((x & 0x80) == 0) { p += 1; return x; }
        if ((x & 0xC0) == 0x80) { uint v = (uint)((x & 0x3F) << 8 | b[p + 1]); p += 2; return v; }
        uint w = (uint)((x & 0x1F) << 24 | b[p + 1] << 16 | b[p + 2] << 8 | b[p + 3]); p += 4; return w;
    }
    static string Str(byte[] b, ref int p) { int n = (int)Cu(b, ref p); var s = Encoding.UTF8.GetString(b, p, n); p += n; return s; }

    // arch-vendor-os-env[-objfmt]; vendor and the environment's version suffix are ignored.
    static string Key(string triple)
    {
        var c = triple.Split('-');
        if (c.Length < 4) return triple;
        string env = new string(c[3].TakeWhile(char.IsLetter).ToArray());
        string fmt = c.Length > 4 ? c[4] : "";
        return $"{c[0]}-{c[2]}-{env}-{fmt}";
    }

    static int Read(string path, string linkTriple, string fmt)
    {
        using var fs = File.OpenRead(path);
        using var pe = new PEReader(fs);
        var r = pe.GetMetadataReader();
        var t = FindTable(pe, r, out var where);
        if (t == null) { Console.WriteLine("error: no symbol table: not built by this toolchain"); return 1; }
        Console.WriteLine($"table: {t.Length} bytes in the {where}");
        string triple;
        int p;
        if (fmt == "t")
        {
            var text = Encoding.UTF8.GetString(t);
            var first = text.Substring(0, text.IndexOf('\n'));
            triple = first.Split(' ').First(x => x.StartsWith("triple=")).Substring(7);
        }
        else
        {
            if (Encoding.ASCII.GetString(t, 0, 4) != "MSYM") { Console.WriteLine("error: bad magic"); return 1; }
            p = 8;
            triple = Str(t, ref p);
        }
        if (Key(triple) != Key(linkTriple))
        {
            Console.WriteLine($"error: {Path.GetFileName(path)} was built for {triple}, which cannot be linked into {linkTriple}");
            return 1;
        }
        var sw = Stopwatch.StartNew();
        int iters = 200, count = 0;
        List<string> sample = null;
        for (int it = 0; it < iters; it++)
        {
            sample = new List<string>();
            count = 0;
            if (fmt == "n")
            {
                // format 1 by name: export classes, data entries, flagged functions; the functions
                // themselves are the public static methods of the export classes.
                p = 8; Str(t, ref p);
                var classes = new List<string>();
                for (uint c = Cu(t, ref p); c > 0; c--) classes.Add(Str(t, ref p));
                var data = new Dictionary<string, (uint, int, byte)>();
                for (uint d = Cu(t, ref p); d > 0; d--) { string dn = Str(t, ref p); uint sz = Cu(t, ref p); int al = 1 << t[p++]; byte fl = t[p++]; data[dn] = (sz, al, fl); }
                var flagged = new Dictionary<string, byte>();
                for (uint f = Cu(t, ref p); f > 0; f--) { string fname = Str(t, ref p); flagged[fname] = t[p++]; }
                foreach (var th in r.TypeDefinitions)
                {
                    var td = r.GetTypeDefinition(th);
                    string full = td.Namespace.IsNil ? r.GetString(td.Name) : r.GetString(td.Namespace) + "." + r.GetString(td.Name);
                    if (!classes.Contains(full)) continue;
                    foreach (var mh in td.GetMethods())
                    {
                        var m = r.GetMethodDefinition(mh);
                        string name = r.GetString(m.Name);
                        bool pubStatic = (m.Attributes & (MethodAttributes.MemberAccessMask | MethodAttributes.Static)) == (MethodAttributes.Public | MethodAttributes.Static);
                        if (!pubStatic || name.StartsWith("<msil>")) continue;
                        count++;
                        if (sample.Count < 4) sample.Add($"function {full}::{name} sig[{r.GetBlobBytes(m.Signature).Length}]" + (flagged.TryGetValue(name, out var fl) ? $" flags {fl}" : ""));
                    }
                    foreach (var ph in td.GetProperties())
                    {
                        var pr = r.GetPropertyDefinition(ph);
                        string name = r.GetString(pr.Name);
                        if (!data.TryGetValue(name, out var dd)) continue;
                        count++;
                        if (sample.Count < 8) sample.Add($"data {full}::{name} size {dd.Item1} align {dd.Item2} via {r.GetString(r.GetMethodDefinition(pr.GetAccessors().Getter).Name)}");
                    }
                }
                continue;
            }
            if (fmt == "t")
            {
                foreach (var line in Encoding.UTF8.GetString(t).Split('\n').Skip(1))
                    if (line.Length > 0) { count++; if (sample.Count < 3) sample.Add(line); }
                continue;
            }
            p = 8; Str(t, ref p);
            uint n = Cu(t, ref p);
            for (uint i = 0; i < n; i++)
            {
                byte kind = t[p++];
                string desc;
                if (fmt == "a")
                {
                    string name = Str(t, ref p);
                    if (kind == 0) { int sl = (int)Cu(t, ref p); desc = $"function {name} sig[{sl}]"; p += sl; }
                    else { uint size = Cu(t, ref p); int al = 1 << t[p++]; desc = $"data {name} size {size} align {al}"; }
                }
                else
                {
                    int row = (int)Cu(t, ref p);
                    if (kind == 0)
                    {
                        var m = r.GetMethodDefinition(MetadataTokens.MethodDefinitionHandle(row));
                        var sig = r.GetBlobBytes(m.Signature);
                        desc = $"function {r.GetString(m.Name)} sig[{sig.Length}]";
                    }
                    else
                    {
                        var pr = r.GetPropertyDefinition(MetadataTokens.PropertyDefinitionHandle(row));
                        uint size = Cu(t, ref p); int al = 1 << t[p++];
                        desc = $"data {r.GetString(pr.Name)} size {size} align {al} via {r.GetString(r.GetMethodDefinition(pr.GetAccessors().Getter).Name)}";
                    }
                }
                count++;
                if (sample.Count < 3 || (kind == 1 && sample.Count < 4)) sample.Add(desc);
            }
        }
        sw.Stop();
        Console.WriteLine($"triple {triple} matches {linkTriple}; {count} symbols; decode {sw.Elapsed.TotalMilliseconds * 1000 / iters:F0} us per table");
        foreach (var s in sample) Console.WriteLine("  " + s);
        return 0;
    }

    static int Attrs(string path)
    {
        var asm = Assembly.LoadFrom(path);
        foreach (var o in asm.GetCustomAttributes(false))
            Console.WriteLine($"reflection: {o.GetType().FullName}");
        foreach (var d in asm.GetCustomAttributesData())
            Console.WriteLine($"reflection data: {d.AttributeType.Name}, {d.ConstructorArguments.Count} argument(s)");
        return 0;
    }
}
