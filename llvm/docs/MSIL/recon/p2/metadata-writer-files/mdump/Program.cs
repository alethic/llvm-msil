// mdump: normalized dump of an ECMA-335 PE image (headers, streams, tables,
// method bodies) with System.Reflection.Metadata, so that two assemblies
// (ilasm's and a hand-written one) can be diffed line by line.
// Usage: mdump <file> [--no-pe] [--no-bodies]
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class P
{
    static MetadataReader md = null!;
    static PEReader pe = null!;

    static string Hex(IEnumerable<byte> b) => string.Join(" ", b.Select(x => x.ToString("X2")));
    static string Str(StringHandle h) => h.IsNil ? "<nil>" : md.GetString(h);
    static string Tok(EntityHandle h) => h.IsNil ? "nil" : $"{MetadataTokens.GetToken(h):X8}";

    static string TypeName(EntityHandle h)
    {
        if (h.IsNil) return "<nil>";
        switch (h.Kind)
        {
            case HandleKind.TypeDefinition:
                {
                    var t = md.GetTypeDefinition((TypeDefinitionHandle)h);
                    var ns = Str(t.Namespace);
                    return (ns.Length > 0 ? ns + "." : "") + Str(t.Name);
                }
            case HandleKind.TypeReference:
                {
                    var t = md.GetTypeReference((TypeReferenceHandle)h);
                    var ns = Str(t.Namespace);
                    string scope = "";
                    if (!t.ResolutionScope.IsNil && t.ResolutionScope.Kind == HandleKind.AssemblyReference)
                        scope = "[" + Str(md.GetAssemblyReference((AssemblyReferenceHandle)t.ResolutionScope).Name) + "]";
                    return scope + (ns.Length > 0 ? ns + "." : "") + Str(t.Name);
                }
            case HandleKind.TypeSpecification:
                {
                    var ts = md.GetTypeSpecification((TypeSpecificationHandle)h);
                    return "spec:" + ts.DecodeSignature(new Prov(), null);
                }
        }
        return h.Kind.ToString();
    }

    sealed class Prov : ISignatureTypeProvider<string, object?>
    {
        public string GetArrayType(string e, ArrayShape s) => e + "[" + new string(',', s.Rank - 1) + "]";
        public string GetByReferenceType(string e) => e + "&";
        public string GetFunctionPointerType(MethodSignature<string> s) =>
            "method " + CallConv(s.Header) + s.ReturnType + "*(" + string.Join(", ", s.ParameterTypes) + ")";
        public string GetGenericInstantiation(string g, ImmutableArray<string> a) => g + "<" + string.Join(",", a) + ">";
        public string GetGenericMethodParameter(object? c, int i) => "!!" + i;
        public string GetGenericTypeParameter(object? c, int i) => "!" + i;
        public string GetModifiedType(string m, string u, bool req) => u + (req ? " modreq(" : " modopt(") + m + ")";
        public string GetPinnedType(string e) => e + " pinned";
        public string GetPointerType(string e) => e + "*";
        public string GetPrimitiveType(PrimitiveTypeCode c) => c switch
        {
            PrimitiveTypeCode.Int32 => "int32", PrimitiveTypeCode.IntPtr => "native int",
            PrimitiveTypeCode.UIntPtr => "native uint", PrimitiveTypeCode.Int64 => "int64",
            PrimitiveTypeCode.Single => "float32", PrimitiveTypeCode.Double => "float64",
            PrimitiveTypeCode.Void => "void", PrimitiveTypeCode.String => "string",
            PrimitiveTypeCode.Object => "object", PrimitiveTypeCode.Boolean => "bool",
            PrimitiveTypeCode.Byte => "uint8", PrimitiveTypeCode.SByte => "int8",
            PrimitiveTypeCode.Int16 => "int16", PrimitiveTypeCode.UInt16 => "uint16",
            PrimitiveTypeCode.UInt32 => "uint32", PrimitiveTypeCode.UInt64 => "uint64",
            PrimitiveTypeCode.Char => "char", _ => c.ToString()
        };
        public string GetSZArrayType(string e) => e + "[]";
        public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k) => (k == 0x11 ? "valuetype " : "class ") + TypeName(h);
        public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k) => (k == 0x11 ? "valuetype " : "class ") + TypeName(h);
        public string GetTypeFromSpecification(MetadataReader r, object? c, TypeSpecificationHandle h, byte k) => TypeName(h);
    }

    static string CallConv(SignatureHeader h)
    {
        var s = new StringBuilder();
        if (h.IsInstance) s.Append("instance ");
        switch (h.CallingConvention)
        {
            case SignatureCallingConvention.Default: break;
            case SignatureCallingConvention.CDecl: s.Append("unmanaged cdecl "); break;
            case SignatureCallingConvention.StdCall: s.Append("unmanaged stdcall "); break;
            case SignatureCallingConvention.ThisCall: s.Append("unmanaged thiscall "); break;
            case SignatureCallingConvention.FastCall: s.Append("unmanaged fastcall "); break;
            case SignatureCallingConvention.VarArgs: s.Append("vararg "); break;
            case SignatureCallingConvention.Unmanaged: s.Append("unmanaged "); break;
        }
        return s.ToString();
    }

    static string MethodSig(BlobHandle b)
    {
        try
        {
            var br = md.GetBlobReader(b);
            var hdr = br.ReadSignatureHeader();
            br.Reset();
            if (hdr.Kind == SignatureKind.Field)
                return "field " + new SignatureDecoder<string, object?>(new Prov(), md, null).DecodeFieldSignature(ref br);
            if (hdr.Kind == SignatureKind.LocalVariables)
                return "locals (" + string.Join(", ", new SignatureDecoder<string, object?>(new Prov(), md, null).DecodeLocalSignature(ref br)) + ")";
            var s = new SignatureDecoder<string, object?>(new Prov(), md, null).DecodeMethodSignature(ref br);
            return CallConv(s.Header) + s.ReturnType + "(" + string.Join(", ", s.ParameterTypes) + ")";
        }
        catch (Exception e) { return "<bad signature: " + e.Message + ">"; }
    }

    static string BlobHex(BlobHandle b) => b.IsNil ? "<nil>" : Hex(md.GetBlobBytes(b));

    static void Main(string[] args)
    {
        if (args[0] == "--opcodes")
        {
            // System.Reflection.Emit.OpCodes: an independent opcode table.
            foreach (var f in typeof(System.Reflection.Emit.OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
            {
                var oc = (System.Reflection.Emit.OpCode)f.GetValue(null)!;
                Console.WriteLine($"{oc.Name}\t0x{(ushort)oc.Value:X4}\t{oc.OperandType}\t{oc.Size}\t{oc.StackBehaviourPop}\t{oc.StackBehaviourPush}\t{oc.FlowControl}\t{oc.OpCodeType}");
            }
            foreach (var v in Enum.GetValues(typeof(ILOpCode)))
                Console.WriteLine($"ILOpCode\t{v}\t0x{(ushort)(ILOpCode)v:X4}");
            return;
        }
        if (args[0] == "--srm-fields")
        {
            // Roslyn's writer (System.Reflection.Metadata MetadataBuilder +
            // ManagedPEBuilder) with N static fields on <Module> followed by
            // a type with an empty field list: how wide is TypeDef.FieldList
            // when the list end is rows+1 = 65536?
            int n = int.Parse(args[1]);
            var mb = new MetadataBuilder();
            mb.AddModule(0, mb.GetOrAddString(Path.GetFileName(args[2])), mb.GetOrAddGuid(Guid.NewGuid()), default, default);
            mb.AddAssembly(mb.GetOrAddString("srm"), new Version(0, 0, 0, 0), default, default, 0, AssemblyHashAlgorithm.None);
            var corlib = mb.AddAssemblyReference(mb.GetOrAddString("mscorlib"), new Version(4, 0, 0, 0), default,
                mb.GetOrAddBlob(new byte[] { 0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89 }), 0, default);
            var obj = mb.AddTypeReference(corlib, mb.GetOrAddString("System"), mb.GetOrAddString("Object"));
            var sb = new BlobBuilder();
            new BlobEncoder(sb).FieldSignature().IntPtr();
            var fsig = mb.GetOrAddBlob(sb);
            var il = new BlobBuilder();
            var enc = new MethodBodyStreamEncoder(il);
            var code = new InstructionEncoder(new BlobBuilder());
            code.LoadConstantI4(42); code.OpCode(ILOpCode.Ret);
            int off = enc.AddMethodBody(code);
            var msig = new BlobBuilder();
            new BlobEncoder(msig).MethodSignature().Parameters(0, r => r.Type().Int32(), p => { });
            mb.AddTypeDefinition(0, default, mb.GetOrAddString("<Module>"), default, MetadataTokens.FieldDefinitionHandle(1), MetadataTokens.MethodDefinitionHandle(1));
            for (int i = 0; i < n; i++)
                mb.AddFieldDefinition(FieldAttributes.Static | FieldAttributes.Assembly, mb.GetOrAddString("f" + i), fsig);
            var main = mb.AddMethodDefinition(MethodAttributes.Static | MethodAttributes.Public, MethodImplAttributes.IL,
                mb.GetOrAddString("main"), mb.GetOrAddBlob(msig), off, MetadataTokens.ParameterHandle(1));
            mb.AddTypeDefinition(TypeAttributes.Class | TypeAttributes.Public | TypeAttributes.Abstract | TypeAttributes.Sealed,
                mb.GetOrAddString("N"), mb.GetOrAddString("Empty"), obj,
                MetadataTokens.FieldDefinitionHandle(n + 1), MetadataTokens.MethodDefinitionHandle(2));
            var peb = new ManagedPEBuilder(new PEHeaderBuilder(Machine.Amd64, imageCharacteristics: Characteristics.ExecutableImage),
                new MetadataRootBuilder(mb), il, entryPoint: main, deterministicIdProvider: content => BlobContentId.FromHash(System.Security.Cryptography.SHA256.HashData(content.SelectMany(b => b.GetBytes().ToArray()).ToArray())));
            var outb = new BlobBuilder();
            peb.Serialize(outb);
            File.WriteAllBytes(args[2], outb.ToArray());
            Console.WriteLine($"wrote {args[2]}");
            return;
        }
        if (args[0] == "--contentid")
        {
            // BlobContentId.FromHash of a hex hash (Roslyn's deterministic MVID).
            var hash = Convert.FromHexString(args[1]);
            var id = BlobContentId.FromHash(hash);
            Console.WriteLine($"guid {id.Guid} bytes {Hex(id.Guid.ToByteArray())} stamp 0x{id.Stamp:X8}");
            return;
        }
        var file = args[0];
        bool noPe = args.Contains("--no-pe");
        bool noBodies = args.Contains("--no-bodies");
        using var fs = File.OpenRead(file);
        pe = new PEReader(fs);
        md = pe.GetMetadataReader();
        var o = Console.Out;

        if (!noPe)
        {
            var h = pe.PEHeaders;
            o.WriteLine($"PE.Machine {h.CoffHeader.Machine} 0x{(int)h.CoffHeader.Machine:X4}");
            o.WriteLine($"PE.Characteristics 0x{(int)h.CoffHeader.Characteristics:X4} {h.CoffHeader.Characteristics}");
            o.WriteLine($"PE.TimeDateStamp 0x{h.CoffHeader.TimeDateStamp:X8}");
            var oh = h.PEHeader!;
            o.WriteLine($"PE.Magic {oh.Magic} Linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}");
            o.WriteLine($"PE.ImageBase 0x{oh.ImageBase:X} SectionAlign 0x{oh.SectionAlignment:X} FileAlign 0x{oh.FileAlignment:X}");
            o.WriteLine($"PE.OS {oh.MajorOperatingSystemVersion}.{oh.MinorOperatingSystemVersion} Subsys {oh.Subsystem} {oh.MajorSubsystemVersion}.{oh.MinorSubsystemVersion}");
            o.WriteLine($"PE.DllCharacteristics 0x{(int)oh.DllCharacteristics:X4} {oh.DllCharacteristics}");
            o.WriteLine($"PE.SizeOfImage 0x{oh.SizeOfImage:X} SizeOfHeaders 0x{oh.SizeOfHeaders:X} SizeOfCode 0x{oh.SizeOfCode:X} InitData 0x{oh.SizeOfInitializedData:X}");
            o.WriteLine($"PE.Stack 0x{oh.SizeOfStackReserve:X}/0x{oh.SizeOfStackCommit:X} Heap 0x{oh.SizeOfHeapReserve:X}/0x{oh.SizeOfHeapCommit:X}");
            o.WriteLine($"PE.EntryPointRVA 0x{oh.AddressOfEntryPoint:X} BaseOfCode 0x{oh.BaseOfCode:X} CheckSum 0x{oh.CheckSum:X}");
            o.WriteLine($"PE.Dir.Import 0x{oh.ImportTableDirectory.RelativeVirtualAddress:X}/0x{oh.ImportTableDirectory.Size:X} IAT 0x{oh.ImportAddressTableDirectory.RelativeVirtualAddress:X}/0x{oh.ImportAddressTableDirectory.Size:X} Reloc 0x{oh.BaseRelocationTableDirectory.RelativeVirtualAddress:X}/0x{oh.BaseRelocationTableDirectory.Size:X} Debug 0x{oh.DebugTableDirectory.RelativeVirtualAddress:X}/0x{oh.DebugTableDirectory.Size:X} Resource 0x{oh.ResourceTableDirectory.RelativeVirtualAddress:X}/0x{oh.ResourceTableDirectory.Size:X} CLR 0x{oh.CorHeaderTableDirectory.RelativeVirtualAddress:X}/0x{oh.CorHeaderTableDirectory.Size:X}");
            foreach (var s in h.SectionHeaders)
                o.WriteLine($"PE.Section {s.Name} VA 0x{s.VirtualAddress:X} VSize 0x{s.VirtualSize:X} Raw 0x{s.PointerToRawData:X}/0x{s.SizeOfRawData:X} Chars 0x{(uint)s.SectionCharacteristics:X8}");
            var c = h.CorHeader!;
            o.WriteLine($"COR.Runtime {c.MajorRuntimeVersion}.{c.MinorRuntimeVersion} Flags 0x{(int)c.Flags:X} {c.Flags} EntryPoint 0x{c.EntryPointTokenOrRelativeVirtualAddress:X8}");
            o.WriteLine($"COR.Metadata 0x{c.MetadataDirectory.RelativeVirtualAddress:X}/0x{c.MetadataDirectory.Size:X} Resources 0x{c.ResourcesDirectory.RelativeVirtualAddress:X}/0x{c.ResourcesDirectory.Size:X} StrongName 0x{c.StrongNameSignatureDirectory.RelativeVirtualAddress:X}/0x{c.StrongNameSignatureDirectory.Size:X}");
        }

        // Metadata root and the #~ header, parsed by hand.
        var mdb = pe.GetMetadata();
        var raw = mdb.GetContent().ToArray();
        uint sig = BitConverter.ToUInt32(raw, 0);
        int verLen = BitConverter.ToInt32(raw, 12);
        string ver = Encoding.UTF8.GetString(raw, 16, verLen);
        int p = 16 + verLen;
        ushort flags = BitConverter.ToUInt16(raw, p);
        ushort nstreams = BitConverter.ToUInt16(raw, p + 2);
        o.WriteLine($"MD.Root sig 0x{sig:X8} v{raw[4] | raw[5] << 8}.{raw[6] | raw[7] << 8} version '{ver.TrimEnd('\0')}' (len {verLen}) flags {flags} streams {nstreams} size 0x{raw.Length:X}");
        p += 4;
        int tildeOff = -1;
        for (int i = 0; i < nstreams; i++)
        {
            int off = BitConverter.ToInt32(raw, p);
            int size = BitConverter.ToInt32(raw, p + 4);
            int q = p + 8;
            int e = q; while (raw[e] != 0) e++;
            string name = Encoding.ASCII.GetString(raw, q, e - q);
            o.WriteLine($"MD.Stream {name} off 0x{off:X} size 0x{size:X}");
            if (name == "#~" || name == "#-") tildeOff = off;
            p = q + ((e - q + 1 + 3) & ~3);
        }
        o.WriteLine($"MD.StreamHeadersEnd 0x{p:X}");
        if (tildeOff >= 0)
        {
            ulong valid = BitConverter.ToUInt64(raw, tildeOff + 8);
            ulong sorted = BitConverter.ToUInt64(raw, tildeOff + 16);
            o.WriteLine($"MD.Tables v{raw[tildeOff + 4]}.{raw[tildeOff + 5]} HeapSizes 0x{raw[tildeOff + 6]:X2} Reserved2 {raw[tildeOff + 7]} Valid 0x{valid:X16} Sorted 0x{sorted:X16}");
        }
        foreach (HeapIndex hi in Enum.GetValues(typeof(HeapIndex)))
            o.WriteLine($"MD.Heap {hi} size 0x{md.GetHeapSize(hi):X}");
        foreach (TableIndex ti in Enum.GetValues(typeof(TableIndex)))
        {
            int n = md.GetTableRowCount(ti);
            if (n > 0) o.WriteLine($"MD.Table {ti} (0x{(int)ti:X2}) rows {n} rowsize {md.GetTableRowSize(ti)} offset 0x{md.GetTableMetadataOffset(ti):X}");
        }

        // Rows.
        var mod = md.GetModuleDefinition();
        o.WriteLine($"Module name '{Str(mod.Name)}' gen {mod.Generation} mvid {md.GetGuid(mod.Mvid)}");
        if (md.IsAssembly)
        {
            var a = md.GetAssemblyDefinition();
            o.WriteLine($"Assembly '{Str(a.Name)}' ver {a.Version} flags 0x{(int)a.Flags:X} hash 0x{(int)a.HashAlgorithm:X} pubkey [{BlobHex(a.PublicKey)}] culture '{Str(a.Culture)}' CAs {a.GetCustomAttributes().Count}");
        }
        foreach (var h in md.AssemblyReferences)
        {
            var a = md.GetAssemblyReference(h);
            o.WriteLine($"AssemblyRef {Tok(h)} '{Str(a.Name)}' ver {a.Version} flags 0x{(int)a.Flags:X} key [{BlobHex(a.PublicKeyOrToken)}] culture '{Str(a.Culture)}' hash [{BlobHex(a.HashValue)}]");
        }
        for (int i = 1; i <= md.GetTableRowCount(TableIndex.ModuleRef); i++)
        {
            var h = MetadataTokens.ModuleReferenceHandle(i);
            o.WriteLine($"ModuleRef {Tok(h)} '{Str(md.GetModuleReference(h).Name)}'");
        }
        foreach (var h in md.TypeReferences)
        {
            var t = md.GetTypeReference(h);
            o.WriteLine($"TypeRef {Tok(h)} scope {Tok(t.ResolutionScope)} '{Str(t.Namespace)}' '{Str(t.Name)}'");
        }
        for (int i = 1; i <= md.GetTableRowCount(TableIndex.TypeSpec); i++)
        {
            var h = MetadataTokens.TypeSpecificationHandle(i);
            o.WriteLine($"TypeSpec {Tok(h)} [{BlobHex(md.GetTypeSpecification(h).Signature)}] {TypeName(h)}");
        }
        foreach (var h in md.TypeDefinitions)
        {
            var t = md.GetTypeDefinition(h);
            var lay = t.GetLayout();
            o.WriteLine($"TypeDef {Tok(h)} flags 0x{(int)t.Attributes:X8} '{Str(t.Namespace)}' '{Str(t.Name)}' extends {TypeName(t.BaseType)} layout pack {lay.PackingSize} size {lay.Size} fields {t.GetFields().Count} methods {t.GetMethods().Count} nested-in {Tok(t.GetDeclaringType())}");
            foreach (var fh in t.GetFields())
            {
                var f = md.GetFieldDefinition(fh);
                o.WriteLine($"  Field {Tok(fh)} flags 0x{(int)f.Attributes:X4} '{Str(f.Name)}' sig [{BlobHex(f.Signature)}] {MethodSig(f.Signature)} rva 0x{f.GetRelativeVirtualAddress():X} offset {f.GetOffset()} CAs {f.GetCustomAttributes().Count}");
            }
            foreach (var mh in t.GetMethods())
            {
                var m = md.GetMethodDefinition(mh);
                o.WriteLine($"  Method {Tok(mh)} flags 0x{(int)m.Attributes:X4} impl 0x{(int)m.ImplAttributes:X4} '{Str(m.Name)}' sig [{BlobHex(m.Signature)}] {MethodSig(m.Signature)} rva 0x{m.RelativeVirtualAddress:X} params {m.GetParameters().Count}");
                foreach (var ph in m.GetParameters())
                {
                    var pp = md.GetParameter(ph);
                    o.WriteLine($"    Param {Tok(ph)} seq {pp.SequenceNumber} flags 0x{(int)pp.Attributes:X} '{Str(pp.Name)}'");
                }
                var imp = m.GetImport();
                if (!imp.Module.IsNil)
                    o.WriteLine($"    ImplMap flags 0x{(int)imp.Attributes:X4} entry '{Str(imp.Name)}' module {Tok(imp.Module)}");
                if (!noBodies && m.RelativeVirtualAddress != 0)
                {
                    var sd = pe.GetSectionData(m.RelativeVirtualAddress);
                    var b0 = sd.GetContent(0, 1)[0];
                    var body = pe.GetMethodBody(m.RelativeVirtualAddress);
                    bool tiny = (b0 & 3) == 2;
                    o.WriteLine($"    Body {(tiny ? "tiny" : "fat")} hdr0 0x{b0:X2} align4 {(m.RelativeVirtualAddress % 4 == 0)} size {body.Size} maxstack {body.MaxStack} locals {Tok(body.LocalSignature)} init {body.LocalVariablesInitialized} eh {body.ExceptionRegions.Length}");
                    if (!tiny)
                    {
                        var hdr = sd.GetContent(0, 12);
                        o.WriteLine($"    FatHeader [{Hex(hdr)}]");
                    }
                    var il = body.GetILBytes()!;
                    o.WriteLine($"    IL {il.Length} [{Hex(il)}]");
                    foreach (var r in body.ExceptionRegions)
                        o.WriteLine($"    EH {r.Kind} try {r.TryOffset}+{r.TryLength} handler {r.HandlerOffset}+{r.HandlerLength} filter {r.FilterOffset} catch {Tok(r.CatchType)}");
                }
            }
        }
        foreach (var h in md.MemberReferences)
        {
            var m = md.GetMemberReference(h);
            string parent = m.Parent.Kind switch
            {
                HandleKind.TypeReference or HandleKind.TypeDefinition or HandleKind.TypeSpecification => TypeName(m.Parent),
                HandleKind.ModuleReference => "module " + Str(md.GetModuleReference((ModuleReferenceHandle)m.Parent).Name),
                _ => Tok(m.Parent)
            };
            o.WriteLine($"MemberRef {Tok(h)} parent {parent} '{Str(m.Name)}' sig [{BlobHex(m.Signature)}] {MethodSig(m.Signature)}");
        }
        for (int i = 1; i <= md.GetTableRowCount(TableIndex.StandAloneSig); i++)
        {
            var h = MetadataTokens.StandaloneSignatureHandle(i);
            var s = md.GetStandaloneSignature(h);
            o.WriteLine($"StandAloneSig {Tok(h)} [{BlobHex(s.Signature)}] {MethodSig(s.Signature)}");
        }
        foreach (var h in md.CustomAttributes)
        {
            var ca = md.GetCustomAttribute(h);
            o.WriteLine($"CustomAttribute {Tok(h)} parent {Tok(ca.Parent)} ctor {Tok(ca.Constructor)} value [{BlobHex(ca.Value)}]");
        }
        for (int i = 1; i <= md.GetTableRowCount(TableIndex.MethodSpec); i++)
        {
            var h = MetadataTokens.MethodSpecificationHandle(i);
            var s = md.GetMethodSpecification(h);
            o.WriteLine($"MethodSpec {Tok(h)} method {Tok(s.Method)} [{BlobHex(s.Signature)}]");
        }
        var us = MetadataTokens.UserStringHandle(1);
        int usSize = md.GetHeapSize(HeapIndex.UserString);
        while (usSize > 1 && !us.IsNil && MetadataTokens.GetHeapOffset(us) < usSize)
        {
            o.WriteLine($"US 0x{MetadataTokens.GetHeapOffset(us):X} \"{md.GetUserString(us)}\"");
            us = md.GetNextHandle(us);
        }
    }
}
