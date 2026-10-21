// Linker.cs: prototype of option 2 ("symbolic objects"): merge CIL objects (ECMA-335 modules whose
// cross-TU references are named metadata references) into one assembly, binding by name and
// renumbering every token. The linker, not the objects, generates <Module>::.cctor (data segment,
// address fields, function-address fields, data relocations, constructors) and '<msil>Main'.
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;
using System.Text.Json;

class LSym { public string Name = "", Kind = "", Bind = "", Comdat, Visibility = "default"; public long Offset, Size; public int Align = 1; public bool MergeStrings; }
class LReloc { public long Site; public string Kind = "", Target = ""; public long Addend; public int Unaligned; }

class LinkInfo
{
    public string Object = ""; public byte[] Image = Array.Empty<byte>(); public long InitSize;
    public List<LSym> Symbols = new(); public List<LReloc> Relocs = new();
    public List<(string Func, int Prio)> Ctors = new(); public Dictionary<string, string> Aliases = new();
    public HashSet<string> WeakUndef = new();

    public static LinkInfo Parse(byte[] json)
    {
        var d = JsonDocument.Parse(json).RootElement;
        var li = new LinkInfo { Object = d.GetProperty("object").GetString()!, Image = Convert.FromHexString(d.GetProperty("image").GetString()!), InitSize = d.GetProperty("init_size").GetInt64() };
        foreach (var s in d.GetProperty("symbols").EnumerateArray())
        {
            var y = new LSym { Name = s.GetProperty("name").GetString()!, Kind = s.GetProperty("kind").GetString()!, Bind = s.GetProperty("bind").GetString()! };
            if (s.TryGetProperty("comdat", out var c) && c.ValueKind == JsonValueKind.String) y.Comdat = c.GetString();
            if (s.TryGetProperty("visibility", out var v)) y.Visibility = v.GetString()!;
            if (y.Kind == "data") { y.Offset = s.GetProperty("offset").GetInt64(); y.Size = s.GetProperty("size").GetInt64(); y.Align = s.GetProperty("align").GetInt32(); y.MergeStrings = s.GetProperty("merge_strings").GetBoolean(); }
            li.Symbols.Add(y);
        }
        foreach (var r in d.GetProperty("relocs").EnumerateArray())
            li.Relocs.Add(new LReloc { Site = r.GetProperty("site").GetInt64(), Kind = r.GetProperty("kind").GetString()!, Target = r.GetProperty("target").GetString()!, Addend = r.GetProperty("addend").GetInt64(), Unaligned = r.TryGetProperty("unaligned", out var u) ? u.GetInt32() : 0 });
        foreach (var c in d.GetProperty("ctors").EnumerateArray())
            li.Ctors.Add((c.GetProperty("func").GetString()!, c.GetProperty("priority").GetInt32()));
        foreach (var a in d.GetProperty("aliases").EnumerateObject()) li.Aliases[a.Name] = a.Value.GetString()!;
        foreach (var w in d.GetProperty("weak_undef").EnumerateArray()) li.WeakUndef.Add(w.GetString()!);
        return li;
    }
}

class Obj
{
    public string Name = ""; public int Index; public bool Live; public PEReader Pe = null!; public MetadataReader Md = null!; public LinkInfo L = null!;
    public Dictionary<MethodDefinitionHandle, string> Defs = new();
    public Dictionary<MethodDefinitionHandle, (string Name, string Module, string Entry)> Stubs = new();
    public Dictionary<FieldDefinitionHandle, string> DataFields = new();
    public Dictionary<MemberReferenceHandle, (string Kind, string Name)> Placeholders = new();
    public Dictionary<string, LSym> Sym = new();      // every symbol of the link info by name
    public Dictionary<string, MethodDefinitionHandle> DefByName = new();
    public long ObjectBytes;

    public bool IsLocal(string name) => Sym.TryGetValue(name, out var s) && s.Bind == "local";

    public static Obj Load(string name, byte[] bytes)
    {
        var o = new Obj { Name = name, ObjectBytes = bytes.Length, Pe = new PEReader(ImmutableArray.Create(bytes)) };
        o.Md = o.Pe.GetMetadataReader();
        var md = o.Md;
        foreach (var rh in md.ManifestResources)
        {
            var r = md.GetManifestResource(rh);
            if (md.GetString(r.Name) != "msil.link") continue;
            var rd = o.Pe.GetSectionData(o.Pe.PEHeaders.CorHeader!.ResourcesDirectory.RelativeVirtualAddress).GetReader();
            rd.Offset = (int)r.Offset; int len = rd.ReadInt32();
            o.L = LinkInfo.Parse(rd.ReadBytes(len));
        }
        if (o.L == null) throw new Exception($"{name}: no msil.link resource");
        foreach (var s in o.L.Symbols) o.Sym[s.Name] = s;
        foreach (var mh in md.MethodDefinitions)
        {
            var m = md.GetMethodDefinition(mh);
            string n = md.GetString(m.Name);
            var imp = m.GetImport();
            if (!imp.Module.IsNil)
            {
                string mod = md.GetString(md.GetModuleReference(imp.Module).Name);
                o.Stubs[mh] = (n, mod, md.GetString(imp.Name));
            }
            else { o.Defs[mh] = n; o.DefByName[n] = mh; }
        }
        foreach (var fh in md.FieldDefinitions) o.DataFields[fh] = md.GetString(md.GetFieldDefinition(fh).Name);
        foreach (var rh in md.MemberReferences)
        {
            var mr = md.GetMemberReference(rh);
            if (mr.Parent.Kind != HandleKind.TypeReference) continue;
            var tr = md.GetTypeReference((TypeReferenceHandle)mr.Parent);
            if (tr.ResolutionScope.Kind != HandleKind.AssemblyReference) continue;
            string asm = md.GetString(md.GetAssemblyReference((AssemblyReferenceHandle)tr.ResolutionScope).Name);
            if (asm != "msil.extern") continue;
            string tn = md.GetString(tr.Name), n = md.GetString(mr.Name);
            string kind = tn == "<msil>FnAddr" ? "fnaddr" : mr.GetKind() == MemberReferenceKind.Method ? "func" : "data";
            o.Placeholders[rh] = (kind, n);
        }
        return o;
    }

    // Non-local symbols this object defines (functions, data, aliases).
    public IEnumerable<string> GlobalDefs()
    {
        foreach (var s in L.Symbols) if (s.Bind != "local") yield return s.Name;
        foreach (var a in L.Aliases.Keys) yield return a;
    }

    // Names this object references but does not define (undefined symbols), with weakness.
    public IEnumerable<(string Name, bool Weak)> Undefs()
    {
        var seen = new HashSet<string>();
        foreach (var (n, mod, _) in Stubs.Values) if (mod == "msil.unresolved" && seen.Add(n)) yield return (n, L.WeakUndef.Contains(n));
        foreach (var (k, n) in Placeholders.Values) if (!Sym.ContainsKey(n) && !L.Aliases.ContainsKey(n) && seen.Add(n)) yield return (n, L.WeakUndef.Contains(n));
        foreach (var r in L.Relocs) if (!Sym.ContainsKey(r.Target) && !L.Aliases.ContainsKey(r.Target) && seen.Add(r.Target)) yield return (r.Target, L.WeakUndef.Contains(r.Target));
    }
}

class Def { public Obj Obj = null!; public LSym Sym = null!; public string Name = ""; public string AliasOf; }

class Stats
{
    public Dictionary<string, int> TokenSites = new();       // by table
    public Dictionary<string, int> TokenMapKinds = new();    // by how the token was bound
    public int Methods, IlBytes, Instructions, SigsRewritten, CodedIndices, EhClauses, StandaloneSigs, UserStrings;
    public int DataRelocs, DataRelocsDropped, ComdatDropped, WeakOverridden, CommonMerged, StringsMerged, Extracted, NativeImports, TrapStubs;
    public void Tok(string table) => TokenSites[table] = TokenSites.GetValueOrDefault(table) + 1;
    public void Map(string kind) => TokenMapKinds[kind] = TokenMapKinds.GetValueOrDefault(kind) + 1;
}

class Linker
{
    readonly List<Obj> objs = new();
    readonly Dictionary<string, Def> globals = new();
    readonly Stats st = new();
    readonly List<string> errors = new();
    readonly Dictionary<string, string> nativeImports;  // symbol -> library (msil-ld would read import libs / .so)
    public bool TraceRelocs; public string Entry = "main";

    public Linker(Dictionary<string, string> imports) { nativeImports = imports; }

    // ---- input ---------------------------------------------------------------------------------
    public void AddInput(string path)
    {
        var bytes = File.ReadAllBytes(path);
        if (bytes.Length >= 8 && Encoding.ASCII.GetString(bytes, 0, 8) == "!<arch>\n")
        {
            foreach (var (n, data) in Archive.Members(bytes))
            {
                var o = Obj.Load(Path.GetFileName(path) + "(" + n + ")", data);
                o.Index = objs.Count; o.Live = false; objs.Add(o);
            }
        }
        else { var o = Obj.Load(Path.GetFileName(path), bytes); o.Index = objs.Count; o.Live = true; objs.Add(o); }
    }

    void AddDefs(Obj o)
    {
        foreach (var s in o.L.Symbols)
        {
            if (s.Bind == "local") continue;
            var d = new Def { Obj = o, Sym = s, Name = s.Name };
            if (!globals.TryGetValue(s.Name, out var e)) { globals[s.Name] = d; continue; }
            var es = e.Sym;
            if (s.Kind == "data" && es.Bind == "common" && s.Bind == "common")
            {   // common + common: largest size, strictest alignment
                st.CommonMerged++;
                if (s.Size > es.Size) { s.Align = Math.Max(s.Align, es.Align); globals[s.Name] = d; } else es.Align = Math.Max(es.Align, s.Align);
            }
            else if (es.Bind == "common" && s.Bind != "common") { st.CommonMerged++; globals[s.Name] = d; }  // a definition beats common
            else if (s.Bind == "common") { st.CommonMerged++; }
            else if (es.Comdat != null && s.Comdat == es.Comdat) { st.ComdatDropped++; }                // COMDAT any: first wins
            else if (es.Bind == "weak" && s.Bind == "global") { st.WeakOverridden++; globals[s.Name] = d; } // strong beats weak
            else if (s.Bind == "weak") { st.WeakOverridden++; }
            else errors.Add($"duplicate symbol: {s.Name} in {e.Obj.Name} and {o.Name}");
        }
        foreach (var (a, t) in o.L.Aliases)
        {
            if (globals.TryGetValue(a, out var e) && e.Sym.Bind != "weak") { errors.Add($"duplicate symbol: {a} (alias) in {e.Obj.Name} and {o.Name}"); continue; }
            var target = o.Sym[t];
            globals[a] = new Def { Obj = o, Sym = new LSym { Name = a, Kind = target.Kind, Bind = "global" }, Name = a, AliasOf = t };
        }
    }

    void ResolveArchives()
    {
        foreach (var o in objs.Where(x => x.Live)) AddDefs(o);
        bool changed = true;
        while (changed)
        {
            changed = false;
            var need = objs.Where(x => x.Live).SelectMany(x => x.Undefs()).Where(u => !u.Weak && !globals.ContainsKey(u.Name)).Select(u => u.Name).ToHashSet();
            foreach (var o in objs.Where(x => !x.Live))
            {
                if (!o.GlobalDefs().Any(need.Contains)) continue;
                o.Live = true; st.Extracted++; AddDefs(o); changed = true;
                Console.Error.WriteLine($"msilobj: extracted {o.Name} for {string.Join(",", o.GlobalDefs().Where(need.Contains))}");
                break;   // re-scan: the extracted member may satisfy or add references
            }
        }
    }

    // ---- symbol resolution: a name referenced from object o --------------------------------------
    enum RK { Method, Data, Null, Native }
    record struct Res(RK Kind, Obj Obj, string Name);

    Res Resolve(Obj o, string name, bool weak)
    {
        if (o.Sym.TryGetValue(name, out var ls) && ls.Bind == "local") return new Res(ls.Kind == "func" ? RK.Method : RK.Data, o, name);
        if (globals.TryGetValue(name, out var d))
        {
            if (d.AliasOf != null) return Resolve(d.Obj, d.AliasOf, false);
            return new Res(d.Sym.Kind == "func" ? RK.Method : RK.Data, d.Obj, d.Name);
        }
        if (nativeImports.ContainsKey(name)) return new Res(RK.Native, null!, name);
        if (weak || o.L.WeakUndef.Contains(name)) return new Res(RK.Null, null!, name);
        errors.Add($"undefined symbol: {name} (referenced by {o.Name})");
        return new Res(RK.Null, null!, name);
    }

    // ---- output ------------------------------------------------------------------------------------
    MetadataBuilder mb = new();
    readonly Dictionary<(Obj, MethodDefinitionHandle), int> methodRow = new();   // kept object methods
    readonly Dictionary<(string, string), int> nativeRow = new();                 // (name, sig hex) -> row
    readonly Dictionary<string, int> trapRow = new();
    readonly List<Func<MethodBodyStreamEncoder, int>> methodEmitters = new();
    readonly List<(MethodAttributes A, MethodImplAttributes I, string Name, byte[] Sig)> methodRows = new();
    readonly List<(FieldAttributes A, string Name, byte[] Sig)> fieldRows = new();
    readonly Dictionary<(Obj, string), int> dataField = new();      // kept data symbol -> field row
    readonly Dictionary<(Obj, string), long> dataOffset = new();    // kept data symbol -> segment offset
    readonly Dictionary<int, int> fnField = new();                  // method row (or -trap/-native) -> field row
    readonly Dictionary<string, int> nullFnField = new();
    readonly HashSet<string> fnNames = new();           // unresolved weak function -> never-set field
    readonly Dictionary<string, int> nullDataField = new();
    readonly Dictionary<string, AssemblyReferenceHandle> asmRefs = new();
    readonly Dictionary<(string, string, string), TypeReferenceHandle> typeRefs = new();
    readonly Dictionary<(EntityHandle, string, string), MemberReferenceHandle> memberRefs = new();
    readonly Dictionary<string, StandaloneSignatureHandle> sasigs = new();
    readonly Dictionary<string, ModuleReferenceHandle> modRefs = new();
    readonly List<(int Row, string Module, string Entry)> imports = new();
    readonly List<string> relocTrace = new();

    int AddMethodRow(MethodAttributes a, MethodImplAttributes i, string name, byte[] sig, Func<MethodBodyStreamEncoder, int> body)
    { if (frozen) throw new Exception($"method row added while encoding bodies: {name}"); methodRows.Add((a, i, name, sig)); methodEmitters.Add(body); return methodRows.Count; }
    bool frozen;
    int AddFieldRow(FieldAttributes a, string name, byte[] sig) { if (frozen) throw new Exception($"field row added while encoding bodies: {name}"); fieldRows.Add((a, name, sig)); return fieldRows.Count; }

    static readonly byte[] NativeIntFieldSig = { 0x06, 0x18 };

    AssemblyReferenceHandle AsmRef(MetadataReader md, AssemblyReferenceHandle h)
    {
        var a = md.GetAssemblyReference(h); string n = md.GetString(a.Name);
        if (asmRefs.TryGetValue(n, out var r)) return r;
        r = mb.AddAssemblyReference(mb.GetOrAddString(n), a.Version, a.Culture.IsNil ? default : mb.GetOrAddString(md.GetString(a.Culture)),
            a.PublicKeyOrToken.IsNil ? default : mb.GetOrAddBlob(md.GetBlobBytes(a.PublicKeyOrToken)), a.Flags, default);
        return asmRefs[n] = r;
    }
    AssemblyReferenceHandle Mscorlib()
    {
        if (asmRefs.TryGetValue("mscorlib", out var r)) return r;
        return asmRefs["mscorlib"] = mb.AddAssemblyReference(mb.GetOrAddString("mscorlib"), new Version(4, 0, 0, 0), default,
            mb.GetOrAddBlob(new byte[] { 0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89 }), 0, default);
    }
    public RefIndex Refs;
    TypeReferenceHandle TypeRef(AssemblyReferenceHandle scope, string asm, string ns, string name)
    {
        if (Refs != null)
        {   // P2-B: the object's scope is only a hint; the defining reference assembly decides.
            var def = Refs.Find(ns, name) ?? throw new Exception($"type {ns}.{name} is not defined by any reference assembly");
            asm = def.Name;
            if (!asmRefs.TryGetValue(asm, out scope))
                asmRefs[asm] = scope = mb.AddAssemblyReference(mb.GetOrAddString(def.Name), def.Version, default,
                    def.PublicKeyToken.Length > 0 ? mb.GetOrAddBlob(def.PublicKeyToken) : default, 0, default);
        }
        if (typeRefs.TryGetValue((asm, ns, name), out var t)) return t;
        return typeRefs[(asm, ns, name)] = mb.AddTypeReference(scope, mb.GetOrAddString(ns), mb.GetOrAddString(name));
    }
    EntityHandle MapType(Obj o, EntityHandle h)
    {
        var md = o.Md;
        if (h.Kind == HandleKind.TypeReference)
        {
            var tr = md.GetTypeReference((TypeReferenceHandle)h);
            if (tr.ResolutionScope.Kind != HandleKind.AssemblyReference) throw new NotSupportedException("nested/module TypeRef");
            var ar = (AssemblyReferenceHandle)tr.ResolutionScope;
            return TypeRef(AsmRef(md, ar), md.GetString(md.GetAssemblyReference(ar).Name), md.GetString(tr.Namespace), md.GetString(tr.Name));
        }
        throw new NotSupportedException($"type handle {h.Kind} in {o.Name}");
    }
    byte[] MapSig(Obj o, BlobHandle b)
    {
        int ci = 0;
        var bytes = IL.RewriteSig(o.Md.GetBlobReader(b), h => MapType(o, h), ref ci);
        st.SigsRewritten++; st.CodedIndices += ci;
        return bytes;
    }

    int NativeStub(string name, string module, string entry, byte[] sig)
    {
        var key = (name + "@" + module, Convert.ToHexString(sig));
        if (nativeRow.TryGetValue(key, out var row)) return row;
        row = AddMethodRow(MethodAttributes.Public | MethodAttributes.Static | MethodAttributes.HideBySig | MethodAttributes.PinvokeImpl,
            MethodImplAttributes.PreserveSig, name, sig, null!);
        imports.Add((row, module, entry)); st.NativeImports++;
        return nativeRow[key] = row;
    }

    int TrapStub(string name, byte[] sig)
    {
        if (trapRow.TryGetValue(name, out var row)) return row;
        st.TrapStubs++;
        return trapRow[name] = AddMethodRow(MethodAttributes.PrivateScope | MethodAttributes.Static, 0, name, sig, enc =>
        {
            var il = new InstructionEncoder(new BlobBuilder());
            il.OpCode(ILOpCode.Ldnull); il.OpCode(ILOpCode.Throw);   // calling an unresolved weak function traps
            return enc.AddMethodBody(il, 1, default, MethodBodyAttributes.None);
        });
    }

    // The output method row that a call to `name` (from object o, with signature sig) binds to.
    int MethodFor(Obj o, string name, bool weak, byte[] sig)
    {
        var r = Resolve(o, name, weak);
        switch (r.Kind)
        {
            case RK.Method: return methodRow[(r.Obj, r.Obj.DefByName[r.Name])];
            case RK.Native: return NativeStub(name, nativeImports[name], name, sig);
            case RK.Null: return TrapStub(name, sig);
            default: errors.Add($"{name}: referenced as a function by {o.Name} but is data"); return 1;
        }
    }

    int FnAddrField(Obj o, string name)
    {
        var r = Resolve(o, name, true);
        if (r.Kind == RK.Null)
        {
            if (!nullFnField.TryGetValue(name, out var nf)) nullFnField[name] = nf = AddFieldRow((fnNames.Add(name) ? FieldAttributes.Private : FieldAttributes.PrivateScope) | FieldAttributes.Static, "<msil>fn_" + name, NativeIntFieldSig);
            return nf;
        }
        int key = r.Kind == RK.Native ? -NativeStub(name, nativeImports[name], name, SigOfUndef(o, name)) : methodRow[(r.Obj, r.Obj.DefByName[r.Name])];
        if (!fnField.TryGetValue(key, out var f)) fnField[key] = f = AddFieldRow((fnNames.Add(name) ? FieldAttributes.Private : FieldAttributes.PrivateScope) | FieldAttributes.Static | FieldAttributes.InitOnly, "<msil>fn_" + name, NativeIntFieldSig);
        return f;
    }
    byte[] SigOfUndef(Obj o, string name)
    {
        foreach (var (h, s) in o.Stubs) if (s.Name == name) return MapSig(o, o.Md.GetMethodDefinition(h).Signature);
        foreach (var (h, p) in o.Placeholders) if (p.Name == name && p.Kind == "func") return MapSig(o, o.Md.GetMemberReference(h).Signature);
        throw new Exception($"no signature known for {name}");
    }

    int DataField(Obj o, string name, bool weak)
    {
        var r = Resolve(o, name, weak);
        if (r.Kind == RK.Data) return dataField[(r.Obj, r.Name)];
        if (r.Kind == RK.Null)
        {
            if (!nullDataField.TryGetValue(name, out var f)) nullDataField[name] = f = AddFieldRow(FieldAttributes.PrivateScope | FieldAttributes.Static, name, NativeIntFieldSig);
            return f;
        }
        errors.Add($"{name}: referenced as data by {o.Name} but is a function"); return 1;
    }

    // ---- the link ------------------------------------------------------------------------------------
    public int Link(string outPath, bool exe)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        ResolveArchives();
        var live = objs.Where(o => o.Live).ToList();

        // 1. Methods kept from objects: locals, and the chosen definition of each global.
        foreach (var o in live)
            foreach (var (h, n) in o.Defs)
            {
                bool keep = o.IsLocal(n) || (globals.TryGetValue(n, out var d) && d.Obj == o && d.AliasOf == null);
                if (!keep) continue;
                var m = o.Md.GetMethodDefinition(h);
                var attrs = m.Attributes;
                if (o.IsLocal(n)) attrs = (attrs & ~MethodAttributes.MemberAccessMask) | MethodAttributes.PrivateScope;
                var oo = o; var hh = h;
                methodRow[(o, h)] = AddMethodRow(attrs, m.ImplAttributes, n, MapSig(o, m.Signature), enc => EmitObjectBody(enc, oo, hh));
            }

        // 2. Data layout: one segment, initialized symbols first, then zero-initialized, then commons.
        long seg = 0; int maxAlign = 16; var image = new MemoryStream();
        var strings = new Dictionary<string, (Obj, string)>();
        var keptData = new List<(Obj O, LSym S)>();
        foreach (var o in live)
            foreach (var s in o.L.Symbols.Where(s => s.Kind == "data").OrderBy(s => s.Offset))
            {
                bool keep = s.Bind == "local" || (globals.TryGetValue(s.Name, out var d) && d.Obj == o && d.Sym == s);
                if (keep) keptData.Add((o, s));
            }
        foreach (var pass in new[] { 0, 1 })   // 0: initialized, 1: zero-initialized
            foreach (var (o, s) in keptData)
            {
                bool init = s.Offset < o.L.InitSize;
                if (init != (pass == 0)) continue;
                if (s.MergeStrings)
                {
                    string key = Convert.ToHexString(o.L.Image, (int)s.Offset, (int)s.Size);
                    if (strings.TryGetValue(key, out var first)) { dataOffset[(o, s.Name)] = dataOffset[first]; st.StringsMerged++; continue; }
                    strings[key] = (o, s.Name);
                }
                seg = (seg + s.Align - 1) / s.Align * s.Align; maxAlign = Math.Max(maxAlign, s.Align);
                dataOffset[(o, s.Name)] = seg;
                if (init) { image.SetLength(seg + s.Size); image.Position = seg; image.Write(o.L.Image, (int)s.Offset, (int)s.Size); }
                seg += s.Size;
            }
        long initSize = image.Length, totalSize = Math.Max(seg, 1);
        foreach (var (o, s) in keptData)
            dataField[(o, s.Name)] = AddFieldRow((s.Bind == "local" ? FieldAttributes.PrivateScope : FieldAttributes.Public) | FieldAttributes.Static | FieldAttributes.InitOnly, s.Name, NativeIntFieldSig);

        // 3. Data relocations (site in a kept symbol) and constructors, all bound by name.
        var relocs = new List<(long Site, string Kind, int Field, long Addend, int Unaligned)>();
        foreach (var o in live)
            foreach (var r in o.L.Relocs)
            {
                var owner = o.L.Symbols.FirstOrDefault(s => s.Kind == "data" && s.Offset <= r.Site && r.Site < s.Offset + s.Size);
                if (owner == null || !dataOffset.ContainsKey((o, owner.Name))) { st.DataRelocsDropped++; continue; }
                long site = dataOffset[(o, owner.Name)] + (r.Site - owner.Offset);
                int field = r.Kind == "fnaddr64" ? FnAddrField(o, r.Target) : DataField(o, r.Target, false);
                relocs.Add((site, r.Kind, field, r.Addend, (site % (r.Kind == "addr32" ? 4 : 8)) == 0 ? 0 : (int)(site & -site))); st.DataRelocs++;
            }
        var ctors = live.SelectMany(o => o.L.Ctors.Select((c, i) => (c.Prio, o.Index, i, Row: methodRow[(o, o.DefByName[c.Func])]))).OrderBy(c => c.Prio).ThenBy(c => c.Index).ThenBy(c => c.i).ToList();

        // 4. Pre-bind every placeholder and stub so that rows exist before bodies are encoded.
        foreach (var o in live)
        {
            foreach (var (h, p) in o.Placeholders)
            {
                if (p.Kind == "fnaddr") FnAddrField(o, p.Name);
                else if (p.Kind == "data") DataField(o, p.Name, false);
                else MethodFor(o, p.Name, false, MapSig(o, o.Md.GetMemberReference(h).Signature));
            }
            foreach (var (h, s) in o.Stubs)
            {
                var sig = MapSig(o, o.Md.GetMethodDefinition(h).Signature);
                if (s.Module == "msil.unresolved") MethodFor(o, s.Name, false, sig); else NativeStub(s.Name, s.Module, s.Entry, sig);
            }
        }
        if (!globals.TryGetValue(Entry, out var entryDef) || entryDef.Sym.Kind != "func") errors.Add($"undefined symbol: {Entry} (entry point)");
        if (errors.Count > 0) { foreach (var e in errors.Distinct()) Console.Error.WriteLine("msilobj: error: " + e); return 1; }

        // 5. Synthesized members.
        int dataBase = AddFieldRow(FieldAttributes.Private | FieldAttributes.Static | FieldAttributes.InitOnly, "<msil>data", NativeIntFieldSig);
        int imageField = 0;
        var mscorlib = Refs != null ? default : Mscorlib();
        var valueType = TypeRef(mscorlib, "mscorlib", "System", "ValueType");
        // <msil>image_t is TypeDef row 2 (after <Module>).
        if (initSize > 0)
        {
            var sig = new BlobBuilder(); sig.WriteByte(0x06); sig.WriteByte(0x11); sig.WriteCompressedInteger(CodedIndex.TypeDefOrRefOrSpec(MetadataTokens.TypeDefinitionHandle(2)));
            imageField = AddFieldRow(FieldAttributes.Private | FieldAttributes.Static | FieldAttributes.HasFieldRVA, "<msil>image", sig.ToArray());
        }
        var marshal = TypeRef(mscorlib, "mscorlib", "System.Runtime.InteropServices", "Marshal");
        var allocSig = new BlobBuilder(); new BlobEncoder(allocSig).MethodSignature().Parameters(1, r => r.Type().IntPtr(), p => p.AddParameter().Type().IntPtr());
        var alloc = mb.AddMemberReference(marshal, mb.GetOrAddString("AllocHGlobal"), mb.GetOrAddBlob(allocSig));
        int mainRow = methodRow[(entryDef!.Obj, entryDef.Obj.DefByName[Entry])];
        var cctorSig = new BlobBuilder(); new BlobEncoder(cctorSig).MethodSignature().Parameters(0, r => r.Void(), p => { });
        var fnInits = fnField.Where(kv => true).ToList();
        AddMethodRow(MethodAttributes.Private | MethodAttributes.Static | MethodAttributes.HideBySig | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName,
            0, ".cctor", cctorSig.ToArray(), enc =>
            {
                var il = new InstructionEncoder(new BlobBuilder());
                il.LoadConstantI4((int)(totalSize + maxAlign - 1)); il.OpCode(ILOpCode.Conv_i); il.Call(alloc);
                il.LoadConstantI4(maxAlign - 1); il.OpCode(ILOpCode.Conv_i); il.OpCode(ILOpCode.Add);
                il.LoadConstantI4(-maxAlign); il.OpCode(ILOpCode.Conv_i); il.OpCode(ILOpCode.And);
                il.OpCode(ILOpCode.Stsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataBase));
                if (initSize > 0)
                {
                    il.OpCode(ILOpCode.Ldsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataBase));
                    il.OpCode(ILOpCode.Ldsflda); il.Token(MetadataTokens.FieldDefinitionHandle(imageField));
                    il.LoadConstantI4((int)initSize); il.OpCode(ILOpCode.Cpblk);
                }
                if (totalSize > initSize)
                {
                    il.OpCode(ILOpCode.Ldsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataBase));
                    il.LoadConstantI4((int)initSize); il.OpCode(ILOpCode.Add); il.LoadConstantI4(0);
                    il.LoadConstantI4((int)(totalSize - initSize)); il.OpCode(ILOpCode.Initblk);
                }
                foreach (var (o, s) in keptData)
                {
                    il.OpCode(ILOpCode.Ldsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataBase));
                    long off = dataOffset[(o, s.Name)];
                    if (off != 0) { il.LoadConstantI4((int)off); il.OpCode(ILOpCode.Add); }
                    il.OpCode(ILOpCode.Stsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataField[(o, s.Name)]));
                }
                foreach (var (key, f) in fnInits)
                {
                    il.OpCode(ILOpCode.Ldftn); il.Token(MetadataTokens.MethodDefinitionHandle(Math.Abs(key)));
                    il.OpCode(ILOpCode.Stsfld); il.Token(MetadataTokens.FieldDefinitionHandle(f));
                }
                foreach (var (site, kind, field, addend, unal) in relocs)
                {
                    il.OpCode(ILOpCode.Ldsfld); il.Token(MetadataTokens.FieldDefinitionHandle(dataBase));
                    if (site != 0) { il.LoadConstantI4((int)site); il.OpCode(ILOpCode.Add); }
                    il.OpCode(ILOpCode.Ldsfld); il.Token(MetadataTokens.FieldDefinitionHandle(field));
                    if (addend != 0) { il.LoadConstantI4((int)addend); il.OpCode(ILOpCode.Add); }
                    if (unal != 0 && (maxAlign % 8) == 0) { il.OpCode(ILOpCode.Unaligned); il.CodeBuilder.WriteByte((byte)Math.Min(unal, 4)); }
                    il.OpCode(kind == "addr32" ? ILOpCode.Stind_i4 : ILOpCode.Stind_i);
                }
                foreach (var c in ctors)
                {
                    il.Call(MetadataTokens.MethodDefinitionHandle(c.Row));
                    var sr = new BlobReader(); var sigb = methodRows[c.Row - 1].Sig;
                    if (sigb[2] != 0x01) il.OpCode(ILOpCode.Pop);   // a constructor that returns a value
                }
                il.OpCode(ILOpCode.Ret);
                return enc.AddMethodBody(il, 4, default, MethodBodyAttributes.None);
            });
        var mainSig = new BlobBuilder(); new BlobEncoder(mainSig).MethodSignature().Parameters(1, r => r.Type().Int32(), p => p.AddParameter().Type().SZArray().String());
        // D27: main(void) is called directly; main(int, char **) gets a NULL-terminated argv built from
        // Environment.GetCommandLineArgs() exactly as the Phase-1 AsmPrinter builds it.
        var mainDefSig = entryDef.Obj.Md.GetBlobReader(entryDef.Obj.Md.GetMethodDefinition(entryDef.Obj.DefByName[Entry]).Signature);
        mainDefSig.ReadByte(); int mainParams = mainDefSig.ReadCompressedInteger();
        StandaloneSignatureHandle argvLocals = default; MemberReferenceHandle getArgs = default, toAnsi = default;
        if (mainParams == 2)
        {
            var env = TypeRef(mscorlib, "mscorlib", "System", "Environment");
            var gs = new BlobBuilder(); new BlobEncoder(gs).MethodSignature().Parameters(0, r => r.Type().SZArray().String(), p => { });
            getArgs = mb.AddMemberReference(env, mb.GetOrAddString("GetCommandLineArgs"), mb.GetOrAddBlob(gs));
            var ts = new BlobBuilder(); new BlobEncoder(ts).MethodSignature().Parameters(1, r => r.Type().IntPtr(), p => p.AddParameter().Type().String());
            toAnsi = mb.AddMemberReference(marshal, mb.GetOrAddString("StringToHGlobalAnsi"), mb.GetOrAddBlob(ts));
            var ls = new BlobBuilder(); var le = new BlobEncoder(ls).LocalVariableSignature(4);
            le.AddVariable().Type().SZArray().String(); le.AddVariable().Type().Int32(); le.AddVariable().Type().IntPtr(); le.AddVariable().Type().Int32();
            argvLocals = mb.AddStandaloneSignature(mb.GetOrAddBlob(ls));
        }
        else if (mainParams != 0) errors.Add("unsupported signature of main");
        int entryRow = AddMethodRow(MethodAttributes.Private | MethodAttributes.Static | MethodAttributes.HideBySig, 0, "<msil>Main", mainSig.ToArray(), enc =>
        {
            var il = new InstructionEncoder(new BlobBuilder(), new ControlFlowBuilder());
            if (mainParams == 2)
            {
                var loop = il.DefineLabel(); var cond = il.DefineLabel();
                il.Call(getArgs); il.StoreLocal(0); il.LoadLocal(0); il.OpCode(ILOpCode.Ldlen); il.OpCode(ILOpCode.Conv_i4); il.StoreLocal(1);
                il.LoadLocal(1); il.LoadConstantI4(1); il.OpCode(ILOpCode.Add); il.OpCode(ILOpCode.Conv_i); il.LoadConstantI4(8); il.OpCode(ILOpCode.Mul);
                il.Call(alloc); il.StoreLocal(2); il.LoadConstantI4(0); il.StoreLocal(3); il.Branch(ILOpCode.Br, cond);
                il.MarkLabel(loop);
                il.LoadLocal(2); il.LoadLocal(3); il.OpCode(ILOpCode.Conv_i); il.LoadConstantI4(8); il.OpCode(ILOpCode.Mul); il.OpCode(ILOpCode.Add);
                il.LoadLocal(0); il.LoadLocal(3); il.OpCode(ILOpCode.Ldelem_ref); il.Call(toAnsi); il.OpCode(ILOpCode.Stind_i);
                il.LoadLocal(3); il.LoadConstantI4(1); il.OpCode(ILOpCode.Add); il.StoreLocal(3);
                il.MarkLabel(cond);
                il.LoadLocal(3); il.LoadLocal(1); il.Branch(ILOpCode.Blt, loop);
                il.LoadLocal(2); il.LoadLocal(1); il.OpCode(ILOpCode.Conv_i); il.LoadConstantI4(8); il.OpCode(ILOpCode.Mul); il.OpCode(ILOpCode.Add);
                il.LoadConstantI4(0); il.OpCode(ILOpCode.Conv_i); il.OpCode(ILOpCode.Stind_i);
                il.LoadLocal(1); il.LoadLocal(2);
            }
            il.Call(MetadataTokens.MethodDefinitionHandle(mainRow)); il.OpCode(ILOpCode.Ret);
            return enc.AddMethodBody(il, 3, argvLocals, MethodBodyAttributes.None);
        });

        // 6. Emit rows in order: module, assembly, <Module>, <msil>image_t, fields, methods.
        string asmName = Path.GetFileNameWithoutExtension(outPath);
        mb.AddModule(0, mb.GetOrAddString(Path.GetFileName(outPath)), mb.GetOrAddGuid(Guid.NewGuid()), default, default);
        mb.AddAssembly(mb.GetOrAddString(asmName), new Version(0, 0, 0, 0), default, default, 0, AssemblyHashAlgorithm.Sha1);
        mb.AddTypeDefinition(0, default, mb.GetOrAddString("<Module>"), default, MetadataTokens.FieldDefinitionHandle(1), MetadataTokens.MethodDefinitionHandle(1));
        if (initSize > 0)
        {
            var it = mb.AddTypeDefinition(TypeAttributes.NotPublic | TypeAttributes.ExplicitLayout | TypeAttributes.Sealed | TypeAttributes.AnsiClass,
                default, mb.GetOrAddString("<msil>image_t"), valueType, MetadataTokens.FieldDefinitionHandle(fieldRows.Count + 1), MetadataTokens.MethodDefinitionHandle(methodRows.Count + 1));
            mb.AddTypeLayout(it, 1, (uint)initSize);
        }
        foreach (var (a, n, s) in fieldRows) mb.AddFieldDefinition(a, mb.GetOrAddString(n), mb.GetOrAddBlob(s));
        var mapped = new BlobBuilder();
        if (initSize > 0) { mb.AddFieldRelativeVirtualAddress(MetadataTokens.FieldDefinitionHandle(imageField), 0); mapped.WriteBytes(image.ToArray()); mapped.Align(8); }

        var ilStream = new BlobBuilder();
        var bodies = new MethodBodyStreamEncoder(ilStream);
        var offsets = new int[methodRows.Count];
        int rowsBefore = methodRows.Count, fieldsBefore = fieldRows.Count; frozen = true;
        for (int k = 0; k < methodRows.Count; k++)
            offsets[k] = methodEmitters[k] == null ? -1 : methodEmitters[k](bodies);
        if (methodRows.Count != rowsBefore || fieldRows.Count != fieldsBefore) throw new Exception("rows added while encoding bodies");
        for (int k = 0; k < methodRows.Count; k++)
        {
            var (a, i, n, s) = methodRows[k];
            mb.AddMethodDefinition(a, i, mb.GetOrAddString(n), mb.GetOrAddBlob(s), offsets[k], MetadataTokens.ParameterHandle(1));
        }
        foreach (var (row, module, entry) in imports)
        {
            if (!modRefs.TryGetValue(module, out var mr)) modRefs[module] = mr = mb.AddModuleReference(mb.GetOrAddString(module));
            mb.AddMethodImport(MetadataTokens.MethodDefinitionHandle(row), MethodImportAttributes.CallingConventionCDecl, mb.GetOrAddString(entry), mr);
        }

        var header = new PEHeaderBuilder(machine: Machine.Amd64,
            imageCharacteristics: Characteristics.ExecutableImage | Characteristics.LargeAddressAware | (exe ? 0 : Characteristics.Dll),
            subsystem: Subsystem.WindowsCui);
        var pe = new ManagedPEBuilder(header, new MetadataRootBuilder(mb), ilStream, mappedFieldData: mapped,
            entryPoint: MetadataTokens.MethodDefinitionHandle(entryRow), flags: CorFlags.ILOnly);
        var blob = new BlobBuilder(); pe.Serialize(blob);
        using (var fs = File.Create(outPath)) blob.WriteContentTo(fs);
        sw.Stop();

        // ---- report ----
        Console.WriteLine($"msilobj: linked {live.Count} objects ({objs.Count - live.Count} archive members not extracted) -> {outPath} ({blob.Count} bytes) in {sw.Elapsed.TotalMilliseconds:F1} ms");
        Console.WriteLine($"  methods {methodRows.Count} (from objects {st.Methods}, native imports {st.NativeImports}, trap stubs {st.TrapStubs}), fields {fieldRows.Count}, data segment {totalSize} bytes ({initSize} initialized), align {maxAlign}");
        Console.WriteLine($"  IL bytes rewritten {st.IlBytes}, instructions {st.Instructions}, token sites {st.TokenSites.Values.Sum()}: " + string.Join(", ", st.TokenSites.OrderBy(k => k.Key).Select(k => $"{k.Key} {k.Value}")));
        Console.WriteLine($"  token bindings: " + string.Join(", ", st.TokenMapKinds.OrderBy(k => k.Key).Select(k => $"{k.Key} {k.Value}")));
        Console.WriteLine($"  signature blobs re-encoded {st.SigsRewritten} (TypeDefOrRef coded indices inside: {st.CodedIndices}), standalone sigs {st.StandaloneSigs}, EH clauses {st.EhClauses}, user strings {st.UserStrings}");
        Console.WriteLine($"  data relocations {st.DataRelocs} (dropped with discarded COMDAT copies: {st.DataRelocsDropped}), COMDAT copies dropped {st.ComdatDropped}, weak definitions overridden {st.WeakOverridden}, common merges {st.CommonMerged}, strings merged {st.StringsMerged}, archive members extracted {st.Extracted}");
        Console.WriteLine($"  constructors in order: " + string.Join(" ", ctors.Select(c => $"{methodRows[c.Row - 1].Name}({c.Prio})")));
        if (TraceRelocs) File.WriteAllLines(outPath + ".relocs.txt", relocTrace);
        return 0;
    }

    int EmitObjectBody(MethodBodyStreamEncoder enc, Obj o, MethodDefinitionHandle h)
    {
        var md = o.Md; var m = md.GetMethodDefinition(h);
        var body = o.Pe.GetMethodBody(m.RelativeVirtualAddress);
        var il = body.GetILBytes()!;
        string mname = md.GetString(m.Name);
        st.Methods++; st.IlBytes += il.Length;
        bool localloc = false;
        st.Instructions += IL.Walk(il, (at, op, tok) =>
        {
            var eh = MetadataTokens.EntityHandle(tok);
            int table = tok >> 24;
            int newTok;
            string tname = ((TableIndex)table).ToString();
            if (table == 0x70) { tname = "UserString"; st.UserStrings++; newTok = MetadataTokens.GetToken(mb.GetOrAddUserString(md.GetUserString(MetadataTokens.UserStringHandle(tok & 0xffffff)))); st.Map("user string"); }
            else newTok = MapToken(o, eh, op);
            st.Tok(tname);
            if (TraceRelocs) relocTrace.Add($"{o.Name}\t{mname}\t+{at}\t{op}\t{tok:X8}\t->\t{newTok:X8}");
            BitConverter.TryWriteBytes(il.AsSpan(at, 4), newTok);
        }, name => { if (name == "localloc") localloc = true; });
        var localSig = default(StandaloneSignatureHandle);
        if (!body.LocalSignature.IsNil) localSig = MapStandalone(o, body.LocalSignature);
        var regions = body.ExceptionRegions;
        st.EhClauses += regions.Length;
        var mbody = enc.AddMethodBody(il.Length, body.MaxStack, regions.Length, false, localSig,
            body.LocalVariablesInitialized ? MethodBodyAttributes.InitLocals : MethodBodyAttributes.None, localloc);
        new BlobWriter(mbody.Instructions).WriteBytes(il);
        foreach (var r in regions)
            mbody.ExceptionRegions.Add(r.Kind, r.TryOffset, r.TryLength, r.HandlerOffset, r.HandlerLength,
                r.CatchType.IsNil ? default : (EntityHandle)MetadataTokens.EntityHandle(MapToken(o, r.CatchType, "catch")), r.FilterOffset);
        return mbody.Offset;
    }

    StandaloneSignatureHandle MapStandalone(Obj o, StandaloneSignatureHandle h)
    {
        var sig = MapSig(o, o.Md.GetStandaloneSignature(h).Signature);
        string key = Convert.ToHexString(sig);
        if (!sasigs.TryGetValue(key, out var r)) { sasigs[key] = r = mb.AddStandaloneSignature(mb.GetOrAddBlob(sig)); st.StandaloneSigs++; }
        return r;
    }

    int MapToken(Obj o, EntityHandle h, string op)
    {
        var md = o.Md;
        switch (h.Kind)
        {
            case HandleKind.MethodDefinition:
                {
                    var mh = (MethodDefinitionHandle)h;
                    if (o.Defs.TryGetValue(mh, out var n))
                    {
                        if (methodRow.TryGetValue((o, mh), out var row)) { st.Map("MethodDef -> own definition"); return 0x06000000 | row; }
                        st.Map("MethodDef -> other object's definition (weak/COMDAT loser)");
                        return 0x06000000 | MethodFor(o, n, false, MapSig(o, md.GetMethodDefinition(mh).Signature));
                    }
                    var s = o.Stubs[mh];
                    var sig = MapSig(o, md.GetMethodDefinition(mh).Signature);
                    if (s.Module != "msil.unresolved") { st.Map("MethodDef stub -> native import (explicit module)"); return 0x06000000 | NativeStub(s.Name, s.Module, s.Entry, sig); }
                    var r = Resolve(o, s.Name, false);
                    st.Map(r.Kind == RK.Method ? "ImplMap stub -> definition" : r.Kind == RK.Native ? "ImplMap stub -> native import" : "ImplMap stub -> trap (weak undefined)");
                    return 0x06000000 | MethodFor(o, s.Name, false, sig);
                }
            case HandleKind.FieldDefinition:
                {
                    var name = o.DataFields[(FieldDefinitionHandle)h];
                    bool own = dataField.ContainsKey((o, name));
                    st.Map(own ? "FieldDef -> own address field" : "FieldDef -> other object's address field (COMDAT/weak/common)");
                    return 0x04000000 | DataField(o, name, false);
                }
            case HandleKind.MemberReference:
                {
                    var mh = (MemberReferenceHandle)h;
                    if (o.Placeholders.TryGetValue(mh, out var p))
                    {
                        if (p.Kind == "fnaddr") { st.Map("MemberRef <msil>FnAddr -> function address field"); return 0x04000000 | FnAddrField(o, p.Name); }
                        if (p.Kind == "data") { st.Map("MemberRef <msil>Symbols (data) -> address field"); return 0x04000000 | DataField(o, p.Name, false); }
                        st.Map("MemberRef <msil>Symbols (func) -> MethodDef");
                        return 0x06000000 | MethodFor(o, p.Name, false, MapSig(o, md.GetMemberReference(mh).Signature));
                    }
                    var mr = md.GetMemberReference(mh);
                    var parent = MapType(o, mr.Parent);
                    var msig = MapSig(o, mr.Signature);
                    var key = (parent, md.GetString(mr.Name), Convert.ToHexString(msig));
                    if (!memberRefs.TryGetValue(key, out var nr)) memberRefs[key] = nr = mb.AddMemberReference(parent, mb.GetOrAddString(key.Item2), mb.GetOrAddBlob(msig));
                    st.Map("MemberRef (BCL) -> MemberRef");
                    return MetadataTokens.GetToken(nr);
                }
            case HandleKind.StandaloneSignature:
                st.Map("StandAloneSig -> StandAloneSig");
                return MetadataTokens.GetToken(MapStandalone(o, (StandaloneSignatureHandle)h));
            case HandleKind.TypeReference:
                st.Map("TypeRef -> TypeRef");
                return MetadataTokens.GetToken(MapType(o, h));
            default:
                throw new NotSupportedException($"{o.Name}: token kind {h.Kind} in {op}");
        }
    }
}

static class Archive
{
    // Minimal GNU/COFF 'ar' reader (llvm-ar output): yields (member name, bytes).
    public static IEnumerable<(string, byte[])> Members(byte[] a)
    {
        int p = 8; string longNames = "";
        while (p + 60 <= a.Length)
        {
            string name = Encoding.ASCII.GetString(a, p, 16).TrimEnd();
            long size = long.Parse(Encoding.ASCII.GetString(a, p + 48, 10).Trim());
            int data = p + 60;
            if (name == "//") longNames = Encoding.ASCII.GetString(a, data, (int)size);
            else if (name != "/" && name != "/SYM64/" && name != "__.SYMDEF")
            {
                if (name.StartsWith("/")) { int off = int.Parse(name[1..]); int end = longNames.IndexOf('\n', off); name = longNames[off..end].TrimEnd('/'); }
                else name = name.TrimEnd('/');
                yield return (name, a[data..(data + (int)size)]);
            }
            p = data + (int)size; if ((p & 1) != 0) p++;
        }
    }
}
