// bclresolve: resolves BCL type and member references of the form
//   [<ret>] Namespace.Type[/Nested]::Member[``N](<params>)     (a method)
//   [<type>] Namespace.Type[/Nested]::Field                      (a field)
//   Namespace.Type[/Nested]                                       (a type)
// against a set of reference assemblies, with System.Reflection.Metadata only.
//
// For every query and every reference set it prints the assembly that defines
// the type (name, version, public key token), the references that only
// forward the type there (TypeForwardedTo, ExportedType rows), whether the
// member exists with exactly that signature (return type included), and the
// AssemblyRef row a linker would emit.
//
// Modes:
//   bclresolve info  <set>...                        corlib detection per set
//   bclresolve resolve --queries F <set>...          detail per query and set
//   bclresolve matrix  --queries F <set>...          markdown table
//   bclresolve trace   --queries F --from ASM --dir D [--dir D2...]
//       follow the runtime binder's path: start at assembly ASM (simple
//       name) in the directories D (implementation assemblies, e.g. the
//       shared framework), follow ExportedType forwarders until a TypeDef
//       is found, then check the member there.
//   <set> is NAME=PATH[;PATH...]; a PATH that is a directory adds every *.dll
//   in it (not recursive).
//
// Query files: one query per line; '#' starts a comment line; an optional
// 'label | ' prefix names the query.

using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text;

static class Program
{
    static int Main(string[] args)
    {
        if (args.Length == 0) { Usage(); return 2; }
        string mode = args[0];
        string? queries = null, from = null;
        var dirs = new List<string>();
        var sets = new List<RefSet>();
        for (int i = 1; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--queries": queries = args[++i]; break;
                case "--from": from = args[++i]; break;
                case "--dir": dirs.Add(args[++i]); break;
                default:
                    int eq = args[i].IndexOf('=');
                    if (eq <= 0) { Usage(); return 2; }
                    sets.Add(RefSet.Load(args[i][..eq], args[i][(eq + 1)..]));
                    break;
            }
        }
        var qs = queries is null ? new List<Query>() : Query.ReadFile(queries);
        switch (mode)
        {
            case "info": foreach (var s in sets) s.PrintInfo(); return 0;
            case "fwdflag":
                // How many ExportedType rows with an AssemblyRef implementation have the forwarder flag?
                foreach (var s in sets)
                    foreach (var a in s.Asms)
                    {
                        int total = 0, flagged = 0, nestedRows = 0, nestedFlagged = 0;
                        foreach (var h in a.R.ExportedTypes)
                        {
                            var et = a.R.GetExportedType(h);
                            var impl = et.Implementation;
                            while (impl.Kind == HandleKind.ExportedType) impl = a.R.GetExportedType((ExportedTypeHandle)impl).Implementation;
                            if (impl.Kind != HandleKind.AssemblyReference) continue;
                            total++;
                            bool nested = et.Implementation.Kind == HandleKind.ExportedType;
                            bool fl = ((int)et.Attributes & 0x00200000) != 0;
                            if (fl) flagged++;
                            if (nested) nestedRows++;
                            if (nested && fl) nestedFlagged++;
                        }
                        if (total > 0) Console.WriteLine($"{a.Id}: {total} ExportedType rows into AssemblyRefs, {flagged} with flag 0x00200000; nested rows {nestedRows}, of which flagged {nestedFlagged}");
                    }
                return 0;
            case "refasm":
                // Does the assembly carry [assembly: ReferenceAssembly]?
                foreach (var s in sets)
                    foreach (var a in s.Asms)
                    {
                        var r = a.R;
                        bool isRef = false;
                        foreach (var ch in r.GetAssemblyDefinition().GetCustomAttributes())
                        {
                            var ca = r.GetCustomAttribute(ch);
                            string? tn = null;
                            if (ca.Constructor.Kind == HandleKind.MemberReference)
                            {
                                var p = r.GetMemberReference((MemberReferenceHandle)ca.Constructor).Parent;
                                if (p.Kind == HandleKind.TypeReference) tn = Asm.FullName(r, (TypeReferenceHandle)p);
                                else if (p.Kind == HandleKind.TypeDefinition) tn = Asm.FullName(r, (TypeDefinitionHandle)p);
                            }
                            else if (ca.Constructor.Kind == HandleKind.MethodDefinition)
                                tn = Asm.FullName(r, r.GetMethodDefinition((MethodDefinitionHandle)ca.Constructor).GetDeclaringType());
                            if (tn == "System.Runtime.CompilerServices.ReferenceAssemblyAttribute") isRef = true;
                        }
                        Console.WriteLine($"{s.Name}: {a.Id} ReferenceAssembly={isRef}");
                    }
                return 0;
            case "modscan":
                // Public static methods of public types whose signature
                // carries a custom modifier (modreq/modopt).
                foreach (var s in sets)
                    foreach (var a in s.Asms)
                    {
                        int n = 0;
                        var prov = new SigProvider();
                        foreach (var th in a.R.TypeDefinitions)
                        {
                            var td = a.R.GetTypeDefinition(th);
                            if ((td.Attributes & TypeAttributes.VisibilityMask) != TypeAttributes.Public) continue;
                            foreach (var mh in td.GetMethods())
                            {
                                var md = a.R.GetMethodDefinition(mh);
                                var sig = md.DecodeSignature(prov, null);
                                string text = sig.ReturnType + " (" + string.Join(", ", sig.ParameterTypes) + ")";
                                if (!text.Contains("modreq(") && !text.Contains("modopt(")) continue;
                                if (n++ < 8 && (md.Attributes & MethodAttributes.Static) != 0)
                                    Console.WriteLine($"   {a.Name}: {Asm.FullName(a.R, th)}::{a.R.GetString(md.Name)} : {text}");
                            }
                        }
                        if (n > 0) Console.WriteLine($"== {a.Name}: {n} methods with custom modifiers");
                    }
                return 0;
            case "stats":
                // Sizes a linker would read: file bytes, metadata bytes, rows.
                foreach (var s in sets)
                {
                    long file = 0, md = 0; int td = 0, et = 0, mdef = 0, fld = 0, tr = 0, ts = 0, ca = 0, mr = 0;
                    long strings = 0, blob = 0;
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    foreach (var a in s.Asms)
                    {
                        file += new FileInfo(a.Path).Length;
                        var r = a.R;
                        md += r.MetadataLength;
                        td += r.TypeDefinitions.Count; et += r.ExportedTypes.Count;
                        mdef += r.MethodDefinitions.Count; fld += r.FieldDefinitions.Count;
                        tr += r.TypeReferences.Count; ts += r.GetTableRowCount(TableIndex.TypeSpec);
                        ca += r.CustomAttributes.Count; mr += r.MemberReferences.Count;
                        strings += r.GetHeapSize(HeapIndex.String); blob += r.GetHeapSize(HeapIndex.Blob);
                    }
                    Console.WriteLine($"{s.Name}: {s.Asms.Count} assemblies, {file / 1024} KiB files, {md / 1024} KiB metadata (#Strings {strings / 1024} KiB, #Blob {blob / 1024} KiB); rows: TypeDef {td}, ExportedType {et}, MethodDef {mdef}, Field {fld}, TypeRef {tr}, TypeSpec {ts}, MemberRef {mr}, CustomAttribute {ca}");
                }
                return 0;
            case "refs":
                // Print the AssemblyRef rows of compiled assemblies and the
                // scope of every TypeRef (what a compiler actually emitted).
                foreach (var s in sets)
                    foreach (var a in s.Asms)
                    {
                        Console.WriteLine($"== {a.Id} ({System.IO.Path.GetFileName(a.Path)})");
                        var r = a.R;
                        foreach (var h in r.AssemblyReferences)
                        {
                            var ar = r.GetAssemblyReference(h);
                            bool isKey = (ar.Flags & AssemblyFlags.PublicKey) != 0;
                            Console.WriteLine($"   AssemblyRef {MetadataTokens.GetRowNumber(h)}: {r.GetString(ar.Name)} {ar.Version} pkt={Asm.TokenOf(r.GetBlobBytes(ar.PublicKeyOrToken), isKey)}{(isKey ? " (full key)" : "")} flags={ar.Flags}");
                        }
                        foreach (var h in r.TypeReferences)
                        {
                            var tr = r.GetTypeReference(h);
                            string scope = tr.ResolutionScope.Kind switch
                            {
                                HandleKind.AssemblyReference => r.GetString(r.GetAssemblyReference((AssemblyReferenceHandle)tr.ResolutionScope).Name),
                                HandleKind.TypeReference => "nested in " + Asm.FullName(r, (TypeReferenceHandle)tr.ResolutionScope),
                                HandleKind.ModuleReference => "module " + r.GetString(r.GetModuleReference((ModuleReferenceHandle)tr.ResolutionScope).Name),
                                _ => tr.ResolutionScope.Kind.ToString(),
                            };
                            Console.WriteLine($"   TypeRef {Asm.FullName(r, h)} -> [{scope}]");
                        }
                        var prov = new SigProvider();
                        foreach (var h in r.MemberReferences)
                        {
                            var mr = r.GetMemberReference(h);
                            string parent = mr.Parent.Kind switch
                            {
                                HandleKind.TypeReference => Asm.FullName(r, (TypeReferenceHandle)mr.Parent),
                                HandleKind.TypeDefinition => Asm.FullName(r, (TypeDefinitionHandle)mr.Parent),
                                _ => mr.Parent.Kind.ToString(),
                            };
                            string sig = mr.GetKind() == MemberReferenceKind.Method
                                ? (mr.DecodeMethodSignature(prov, null) is var ms ? $"{ms.ReturnType} ({string.Join(", ", ms.ParameterTypes)})" : "")
                                : mr.DecodeFieldSignature(prov, null);
                            Console.WriteLine($"   MemberRef {parent}::{r.GetString(mr.Name)} : {sig}  blob={BitConverter.ToString(r.GetBlobBytes(mr.Signature)).Replace('-', ' ')}");
                        }
                    }
                return 0;
            case "resolve":
                foreach (var q in qs)
                {
                    Console.WriteLine($"== {q.Label}: {q.Text}");
                    foreach (var s in sets)
                        Console.WriteLine($"   [{s.Name}] {s.Resolve(q).Detail()}");
                }
                return 0;
            case "matrix":
                Console.Write("| Query |");
                foreach (var s in sets) Console.Write($" {s.Name} |");
                Console.WriteLine();
                Console.Write("|---|");
                foreach (var _ in sets) Console.Write("---|");
                Console.WriteLine();
                foreach (var q in qs)
                {
                    Console.Write($"| `{q.Text}` |");
                    foreach (var s in sets) Console.Write($" {s.Resolve(q).Cell()} |");
                    Console.WriteLine();
                }
                return 0;
            case "trace":
                if (from is null || dirs.Count == 0) { Usage(); return 2; }
                var rt = RefSet.Load("runtime", string.Join(';', dirs));
                foreach (var q in qs)
                    Console.WriteLine($"{q.Text}\n   {rt.Trace(from, q)}");
                return 0;
        }
        Usage();
        return 2;
    }

    static void Usage() => Console.Error.WriteLine(
        "usage: bclresolve info|resolve|matrix [--queries F] NAME=PATH[;PATH]...\n" +
        "       bclresolve trace --queries F --from ASSEMBLY --dir DIR [--dir DIR]...");
}

/// One query line.
sealed record Query(string Label, string Text, string? Ret, string Type,
                    string? Member, int Arity, string[]? Params)
{
    public bool IsMethod => Params is not null;

    public static List<Query> ReadFile(string path)
    {
        var list = new List<Query>();
        foreach (var raw in File.ReadAllLines(path))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#')) continue;
            string label = "";
            int bar = line.IndexOf(" | ", StringComparison.Ordinal);
            if (bar >= 0) { label = line[..bar].Trim(); line = line[(bar + 3)..].Trim(); }
            list.Add(Parse(label, line));
        }
        return list;
    }

    public static Query Parse(string label, string text)
    {
        string t = Norm(text);
        int colons = t.IndexOf("::", StringComparison.Ordinal);
        string head = colons < 0 ? t : t[..colons];
        // The type name is the last top-level token of head; anything before
        // it is the return (or field) type.
        int depth = 0, split = -1;
        for (int i = head.Length - 1; i >= 0; i--)
        {
            char c = head[i];
            if (c == '>') depth++;
            else if (c == '<') depth--;
            else if (c == ' ' && depth == 0) { split = i; break; }
        }
        string? ret = split < 0 ? null : head[..split];
        string type = split < 0 ? head : head[(split + 1)..];
        if (colons < 0) return new Query(label, t, null, type, null, 0, null);
        string rest = t[(colons + 2)..];
        int paren = rest.IndexOf('(');
        string member = paren < 0 ? rest : rest[..paren];
        int arity = 0;
        int tick = member.IndexOf("``", StringComparison.Ordinal);
        if (tick >= 0) { arity = int.Parse(member[(tick + 2)..]); member = member[..tick]; }
        string[]? ps = null;
        if (paren >= 0)
        {
            string inner = rest[(paren + 1)..rest.LastIndexOf(')')];
            ps = SplitTop(inner).Select(Norm).Where(p => p.Length > 0).ToArray();
        }
        return new Query(label, t, ret, type, member, arity, ps);
    }

    static IEnumerable<string> SplitTop(string s)
    {
        int depth = 0, start = 0;
        for (int i = 0; i < s.Length; i++)
        {
            if (s[i] == '<' || s[i] == '(') depth++;
            else if (s[i] == '>' || s[i] == ')') depth--;
            else if (s[i] == ',' && depth == 0) { yield return s[start..i]; start = i + 1; }
        }
        yield return s[start..];
    }

    public static string Norm(string s)
    {
        var sb = new StringBuilder();
        bool space = false;
        foreach (char c in s.Trim())
        {
            if (char.IsWhiteSpace(c)) { space = true; continue; }
            if (space && sb.Length > 0 && sb[^1] != ',' && sb[^1] != '<' && sb[^1] != '(' &&
                c != ',' && c != '>' && c != ')' && c != '*' && c != '&' && c != '[')
                sb.Append(' ');
            space = false;
            sb.Append(c);
        }
        return sb.ToString().Replace(", ", ",");
    }
}

/// One reference assembly.
sealed class Asm
{
    public required string Path;
    public required MetadataReader R;
    public required string Name;
    public required Version Version;
    public required string Pkt;      // "" when not strong-named
    public required string Culture;
    public readonly List<string> Refs = new();
    public readonly Dictionary<string, TypeDefinitionHandle> Types = new(StringComparer.Ordinal);
    // forwarded full type name -> destination assembly simple name
    public readonly Dictionary<string, string> Forwarders = new(StringComparer.Ordinal);
    public bool DefinesObject => Types.ContainsKey("System.Object");
    public bool IsCorLibCandidate => DefinesObject && Refs.Count == 0;
    public string Id => $"{Name} {Version} [{(Pkt.Length == 0 ? "null" : Pkt)}]";

    public static Asm? Open(string path)
    {
        PEReader pe;
        try { pe = new PEReader(File.OpenRead(path)); } catch { return null; }
        try { if (!pe.HasMetadata) return null; } catch { return null; }
        var r = pe.GetMetadataReader();
        if (!r.IsAssembly) return null;
        var def = r.GetAssemblyDefinition();
        var a = new Asm
        {
            Path = path, R = r, Name = r.GetString(def.Name), Version = def.Version,
            Pkt = TokenOf(r.GetBlobBytes(def.PublicKey), true),
            Culture = r.GetString(def.Culture),
        };
        foreach (var h in r.AssemblyReferences)
        {
            var ar = r.GetAssemblyReference(h);
            a.Refs.Add(r.GetString(ar.Name));
        }
        foreach (var h in r.TypeDefinitions)
            a.Types[FullName(r, h)] = h;
        foreach (var h in r.ExportedTypes)
        {
            var et = r.GetExportedType(h);
            string? dest = Destination(r, et);
            if (dest is not null) a.Forwarders[ExportedName(r, h)] = dest;
        }
        return a;
    }

    /// The simple name of the assembly an exported type is forwarded to
    /// (following nested exported types to their outermost one), or null for
    /// a type exported from another module of a multi-module assembly.
    static string? Destination(MetadataReader r, ExportedType et)
    {
        var impl = et.Implementation;
        while (impl.Kind == HandleKind.ExportedType)
            impl = r.GetExportedType((ExportedTypeHandle)impl).Implementation;
        if (impl.Kind != HandleKind.AssemblyReference) return null;
        return r.GetString(r.GetAssemblyReference((AssemblyReferenceHandle)impl).Name);
    }

    static string ExportedName(MetadataReader r, ExportedTypeHandle h)
    {
        var et = r.GetExportedType(h);
        string name = r.GetString(et.Name);
        if (et.Implementation.Kind == HandleKind.ExportedType)
            return ExportedName(r, (ExportedTypeHandle)et.Implementation) + "/" + name;
        string ns = r.GetString(et.Namespace);
        return ns.Length == 0 ? name : ns + "." + name;
    }

    public static string FullName(MetadataReader r, TypeDefinitionHandle h)
    {
        var td = r.GetTypeDefinition(h);
        string name = r.GetString(td.Name);
        var outer = td.GetDeclaringType();
        if (!outer.IsNil) return FullName(r, outer) + "/" + name;
        string ns = r.GetString(td.Namespace);
        return ns.Length == 0 ? name : ns + "." + name;
    }

    public static string FullName(MetadataReader r, TypeReferenceHandle h)
    {
        var tr = r.GetTypeReference(h);
        string name = r.GetString(tr.Name);
        if (tr.ResolutionScope.Kind == HandleKind.TypeReference)
            return FullName(r, (TypeReferenceHandle)tr.ResolutionScope) + "/" + name;
        string ns = r.GetString(tr.Namespace);
        return ns.Length == 0 ? name : ns + "." + name;
    }

    public static string TokenOf(byte[] keyOrToken, bool isKey)
    {
        if (keyOrToken.Length == 0) return "";
        byte[] tok = keyOrToken;
        if (isKey)
        {
            var hash = SHA1.HashData(keyOrToken);
            tok = hash[^8..];
            Array.Reverse(tok);
        }
        return Convert.ToHexString(tok).ToLowerInvariant();
    }
}

/// A named set of reference assemblies.
sealed class RefSet
{
    public required string Name;
    public readonly List<Asm> Asms = new();
    public readonly List<string> Duplicates = new();

    public static RefSet Load(string name, string paths)
    {
        var s = new RefSet { Name = name };
        var seen = new Dictionary<string, Asm>(StringComparer.OrdinalIgnoreCase);
        foreach (var p in paths.Split(';', StringSplitOptions.RemoveEmptyEntries))
        {
            IEnumerable<string> files = Directory.Exists(p)
                ? Directory.GetFiles(p, "*.dll").OrderBy(f => f, StringComparer.OrdinalIgnoreCase)
                : new[] { p };
            foreach (var f in files)
            {
                var a = Asm.Open(f);
                if (a is null) continue;
                if (seen.TryGetValue(a.Name, out var prev))
                {
                    s.Duplicates.Add($"{a.Name}: {prev.Path} and {a.Path} (first kept)");
                    continue;
                }
                seen[a.Name] = a;
                s.Asms.Add(a);
            }
        }
        return s;
    }

    public Asm? ByName(string simple) =>
        Asms.FirstOrDefault(a => string.Equals(a.Name, simple, StringComparison.OrdinalIgnoreCase));

    /// Roslyn's rule: the core library is the reference that defines
    /// System.Object and references no other assembly.
    public Asm? CorLib => Asms.FirstOrDefault(a => a.IsCorLibCandidate);

    public void PrintInfo()
    {
        int defs = Asms.Count(a => a.Types.Count > 1), fwdOnly = Asms.Count(a => a.Types.Count <= 1 && a.Forwarders.Count > 0);
        Console.WriteLine($"== set {Name}: {Asms.Count} assemblies ({defs} define types, {fwdOnly} forward only); corlib (Roslyn rule) = {CorLib?.Id ?? "none"}");
        foreach (var a in Asms.Where(a => a.DefinesObject || a.Forwarders.ContainsKey("System.Object")))
            Console.WriteLine($"   System.Object: {a.Id} {(a.DefinesObject ? "defines" : "forwards to " + a.Forwarders["System.Object"])}, {a.Refs.Count} AssemblyRefs{(a.Refs.Count > 0 ? " (" + string.Join(", ", a.Refs.Take(6)) + (a.Refs.Count > 6 ? ", ..." : "") + ")" : "")}, {a.Types.Count} TypeDefs, {a.Forwarders.Count} forwarders");
        foreach (var d in Duplicates) Console.WriteLine($"   duplicate: {d}");
    }

    public Result Resolve(Query q)
    {
        var res = new Result { Query = q };
        var defs = Asms.Where(a => a.Types.ContainsKey(q.Type)).ToList();
        res.ForwardedBy = Asms.Where(a => a.Forwarders.ContainsKey(q.Type))
                              .Select(a => $"{a.Name}->{a.Forwarders[q.Type]}").ToList();
        if (defs.Count == 0)
        {
            if (res.ForwardedBy.Count == 0) { res.Status = "NO-TYPE"; return res; }
            // Follow the forwarders: a closed reference set always contains
            // the destination; an open one does not (Roslyn CS1070).
            var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var start = Asms.First(a => a.Forwarders.ContainsKey(q.Type));
            Asm cur = start;
            var chain = new List<string> { cur.Name };
            while (true)
            {
                if (!visited.Add(cur.Name)) { res.Status = "FORWARD-CYCLE"; return res; }
                string dest = cur.Forwarders[q.Type];
                var next = ByName(dest);
                if (next is null) { res.Status = $"FORWARDED-TO-MISSING({dest})"; res.Chain = chain; return res; }
                chain.Add(next.Name);
                if (next.Types.ContainsKey(q.Type)) { defs.Add(next); break; }
                if (!next.Forwarders.ContainsKey(q.Type)) { res.Status = "FORWARD-DANGLING"; res.Chain = chain; return res; }
                cur = next;
            }
            res.Chain = chain;
        }
        if (defs.Count > 1)
        {
            res.Ambiguous = defs.Select(d => d.Id).ToList();
            var cl = CorLib;
            if (cl is not null && defs.Contains(cl)) defs = new List<Asm> { cl };
            else { res.Status = "AMBIGUOUS"; return res; }
        }
        var asm = defs[0];
        res.Asm = asm;
        res.Type = asm.Types[q.Type];
        if (q.Member is null) { res.Status = "OK"; return res; }
        FindMember(res, asm, asm.Types[q.Type], q, 0);
        return res;
    }

    void FindMember(Result res, Asm asm, TypeDefinitionHandle th, Query q, int depth)
    {
        var r = asm.R;
        var td = r.GetTypeDefinition(th);
        var prov = new SigProvider();
        var near = new List<string>();
        if (q.IsMethod)
        {
            foreach (var mh in td.GetMethods())
            {
                var md = r.GetMethodDefinition(mh);
                if (r.GetString(md.Name) != q.Member) continue;
                var sig = md.DecodeSignature(prov, null);
                string text = Render(sig);
                near.Add(text);
                if (sig.GenericParameterCount != q.Arity) continue;
                if (sig.ParameterTypes.Length != q.Params!.Length) continue;
                bool ok = true;
                for (int i = 0; i < q.Params.Length && ok; i++)
                    ok = Query.Norm(sig.ParameterTypes[i]) == q.Params[i];
                if (ok && q.Ret is not null) ok = Query.Norm(sig.ReturnType) == q.Ret;
                if (!ok) continue;
                res.Status = depth == 0 ? "OK" : "OK(base)";
                res.Found = text;
                res.MemberAccess = (md.Attributes & MethodAttributes.MemberAccessMask).ToString();
                res.DeclaringType = Asm.FullName(r, th);
                // Attributes a binder may care about (on the member or type).
                foreach (var ch in md.GetCustomAttributes().Concat(td.GetCustomAttributes()))
                {
                    var ca = r.GetCustomAttribute(ch);
                    if (ca.Constructor.Kind != HandleKind.MemberReference) continue;
                    var p = r.GetMemberReference((MemberReferenceHandle)ca.Constructor).Parent;
                    if (p.Kind != HandleKind.TypeReference) continue;
                    string an = r.GetTypeReference((TypeReferenceHandle)p).Name is var nh ? r.GetString(nh) : "";
                    if (an is "ObsoleteAttribute" or "RequiresPreviewFeaturesAttribute" or "SupportedOSPlatformAttribute"
                        or "UnsupportedOSPlatformAttribute" or "CLSCompliantAttribute" or "IntrinsicAttribute")
                    {
                        string arg = "";
                        var bl = r.GetBlobReader(ca.Value);
                        if (bl.Length > 2) { bl.ReadUInt16(); try { arg = bl.ReadSerializedString() ?? ""; } catch { } }
                        res.Flags.Add(an.Replace("Attribute", "") + (arg.Length > 0 ? "(" + arg + ")" : ""));
                    }
                }
                return;
            }
        }
        else
        {
            foreach (var fh in td.GetFields())
            {
                var fd = r.GetFieldDefinition(fh);
                if (r.GetString(fd.Name) != q.Member) continue;
                string ft = fd.DecodeSignature(prov, null);
                near.Add(ft);
                if (q.Ret is not null && Query.Norm(ft) != q.Ret) continue;
                res.Status = depth == 0 ? "OK" : "OK(base)";
                res.Found = ft + " " + q.Member;
                res.DeclaringType = Asm.FullName(r, th);
                return;
            }
        }
        res.Near.AddRange(near);
        // Static members are found through a derived type's MemberRef too
        // (the runtime searches the base classes), so look there.
        var bt = td.BaseType;
        if (!bt.IsNil && depth < 16)
        {
            string? bname = bt.Kind switch
            {
                HandleKind.TypeDefinition => Asm.FullName(r, (TypeDefinitionHandle)bt),
                HandleKind.TypeReference => Asm.FullName(r, (TypeReferenceHandle)bt),
                _ => null,
            };
            if (bname is not null && bname != "System.Object" && bname != "System.ValueType")
            {
                var br = Resolve(q with { Type = bname, Member = null });
                if (br.Asm is not null)
                {
                    FindMember(res, br.Asm, br.Asm.Types[bname], q, depth + 1);
                    if (res.Status.StartsWith("OK")) return;
                }
            }
        }
        if (depth == 0) res.Status = "NO-MEMBER";
    }

    static string Render(MethodSignature<string> sig)
    {
        var sb = new StringBuilder();
        sb.Append(sig.ReturnType).Append(' ');
        if (sig.GenericParameterCount > 0) sb.Append("``").Append(sig.GenericParameterCount);
        sb.Append('(').Append(string.Join(", ", sig.ParameterTypes)).Append(')');
        return sb.ToString();
    }

    /// Follows the runtime binder: start at assembly `from`, follow forwarders.
    public string Trace(string from, Query q)
    {
        var cur = ByName(from);
        if (cur is null) return $"assembly {from} not found";
        var chain = new List<string>();
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        while (true)
        {
            chain.Add(cur.Id);
            if (!visited.Add(cur.Name)) return string.Join(" -> ", chain) + "  CYCLE";
            if (cur.Types.ContainsKey(q.Type))
            {
                string tail = "";
                if (q.Member is not null)
                {
                    var res = new Result { Query = q };
                    FindMember(res, cur, cur.Types[q.Type], q, 0);
                    tail = res.Status.StartsWith("OK") ? $"  member OK: {res.Found}" : $"  MEMBER NOT FOUND ({res.Status})";
                }
                return string.Join(" -> ", chain) + "  TYPEDEF" + tail;
            }
            if (!cur.Forwarders.TryGetValue(q.Type, out var dest))
                return string.Join(" -> ", chain) + "  TYPE NOT FOUND (TypeLoadException at run time)";
            var next = ByName(dest);
            if (next is null) return string.Join(" -> ", chain) + $" -> {dest}  NOT IN DIRECTORY";
            cur = next;
        }
    }
}

sealed class Result
{
    public required Query Query;
    public string Status = "?";
    public Asm? Asm;
    public TypeDefinitionHandle Type;
    public List<string> ForwardedBy = new();
    public List<string>? Chain;
    public List<string>? Ambiguous;
    public string? Found, MemberAccess, DeclaringType;
    public List<string> Near = new();
    public List<string> Flags = new();

    public string Detail()
    {
        var sb = new StringBuilder(Status);
        if (Asm is not null) sb.Append($"  defined in {Asm.Id}");
        if (DeclaringType is not null && DeclaringType != Query.Type) sb.Append($" (declared on {DeclaringType})");
        if (Found is not null) sb.Append($"  sig: {Found}");
        if (MemberAccess is not null && MemberAccess != "Public") sb.Append($"  access: {MemberAccess}");
        if (Flags.Count > 0) sb.Append($"  [{string.Join(", ", Flags.Distinct())}]");
        if (Chain is not null) sb.Append($"  via forwarders: {string.Join(" -> ", Chain)}");
        if (ForwardedBy.Count > 0) sb.Append($"  forwarded by: {string.Join(", ", ForwardedBy)}");
        if (Ambiguous is not null) sb.Append($"  defined in several: {string.Join("; ", Ambiguous)}");
        if (Status == "NO-MEMBER" && Near.Count > 0) sb.Append($"  overloads present: {string.Join(" | ", Near.Distinct())}");
        return sb.ToString();
    }

    public string Cell()
    {
        if (Status.StartsWith("OK"))
        {
            string s = $"{Asm!.Name} {Asm.Version.Major}.{Asm.Version.Minor}.{Asm.Version.Build}.{Asm.Version.Revision}";
            if (Chain is not null) s += " (fwd " + string.Join("→", Chain) + ")";
            if (Ambiguous is not null) s += " (also defined elsewhere)";
            if (Status == "OK(base)") s += " (on base)";
            return s;
        }
        if (Status == "NO-MEMBER") return Asm is null ? "no member" : $"**no member** (type in {Asm.Name})";
        if (Status == "NO-TYPE") return "—";
        return Status;
    }
}

/// Renders signature types in ILAsm-like syntax, without assembly scopes.
sealed class SigProvider : ISignatureTypeProvider<string, object?>
{
    public string GetPrimitiveType(PrimitiveTypeCode c) => c switch
    {
        PrimitiveTypeCode.Void => "void", PrimitiveTypeCode.Boolean => "bool",
        PrimitiveTypeCode.Char => "char", PrimitiveTypeCode.SByte => "int8",
        PrimitiveTypeCode.Byte => "uint8", PrimitiveTypeCode.Int16 => "int16",
        PrimitiveTypeCode.UInt16 => "uint16", PrimitiveTypeCode.Int32 => "int32",
        PrimitiveTypeCode.UInt32 => "uint32", PrimitiveTypeCode.Int64 => "int64",
        PrimitiveTypeCode.UInt64 => "uint64", PrimitiveTypeCode.Single => "float32",
        PrimitiveTypeCode.Double => "float64", PrimitiveTypeCode.String => "string",
        PrimitiveTypeCode.IntPtr => "native int", PrimitiveTypeCode.UIntPtr => "native uint",
        PrimitiveTypeCode.Object => "object", PrimitiveTypeCode.TypedReference => "typedref",
        _ => c.ToString(),
    };
    public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k) => Asm.FullName(r, h);
    public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k) => Asm.FullName(r, h);
    public string GetTypeFromSpecification(MetadataReader r, object? ctx, TypeSpecificationHandle h, byte k) =>
        r.GetTypeSpecification(h).DecodeSignature(this, ctx);
    public string GetSZArrayType(string e) => e + "[]";
    public string GetArrayType(string e, ArrayShape s) => e + "[" + new string(',', s.Rank - 1) + "]";
    public string GetByReferenceType(string e) => e + "&";
    public string GetPointerType(string e) => e + "*";
    public string GetPinnedType(string e) => e + " pinned";
    public string GetGenericInstantiation(string g, ImmutableArray<string> a) => g + "<" + string.Join(",", a) + ">";
    public string GetGenericTypeParameter(object? ctx, int i) => "!" + i;
    public string GetGenericMethodParameter(object? ctx, int i) => "!!" + i;
    public string GetFunctionPointerType(MethodSignature<string> s) =>
        "method " + s.ReturnType + " *(" + string.Join(",", s.ParameterTypes) + ")";
    public string GetModifiedType(string mod, string un, bool req) => un + (req ? " modreq(" : " modopt(") + mod + ")";
}
