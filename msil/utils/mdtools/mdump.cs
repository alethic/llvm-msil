//===-- mdump.cs - Raw dumps, opcodes and content ids -----------*- C# -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// mdump is the System.Reflection.Metadata (SRM) oracle of the ECMA-335 library
// (llvm/docs/MSIL/ImplementationPlan.md, 5.7.11). It has three modes:
//
//   mdump [--no-pe] [--no-bodies] <file>...
//       A raw listing for people: PE and CLI headers, the metadata root,
//       streams and table sizes, then the rows with their tokens, blobs and
//       method bodies as SRM reads them. It shows the layout that canonical
//       listing v1 (mdcanon) leaves out. It is never compared by a test; its
//       format may change. With several files each listing is preceded by
//       'File: <path>'.
//
//   mdump --opcodes
//       System.Reflection.Emit.OpCodes in value order, one tab-separated line
//       per opcode: Name, Value (0x and 4 hex digits), OperandType,
//       StackBehaviourPop, StackBehaviourPush, FlowControl. The reserved
//       prefixes (prefix1..prefix7, prefixref) are included; check-cil-defs.py
//       compares the list with CILOpcodes.def.
//
//   mdump --contentid <hex digest>
//       BlobContentId.FromHash of the digest (at least 20 bytes), as Roslyn
//       and msil-ld derive the MVID and the COFF time stamp from a content
//       hash: '<guid, lowercase 8-4-4-4-12> 0x<stamp, 8 hex digits>'.
//
// Run it as 'dotnet exec --runtimeconfig mdump.runtimeconfig.json mdump.dll
// ...'. build-mdtools.py builds it. Exit status: 0, 1 if an input could not
// be read, 2 on a usage error.
//
//===----------------------------------------------------------------------===//

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class MDump
{
    const string Usage =
        "usage: mdump [--no-pe] [--no-bodies] <file>...\n" +
        "       mdump --opcodes\n" +
        "       mdump --contentid <hex digest>\n";

    static int UsageError(string message)
    {
        Console.Error.Write("mdump: error: " + message + "\n" + Usage);
        return 2;
    }

    static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;
        if (args.Length > 0 && args[0] == "--opcodes")
        {
            if (args.Length != 1) return UsageError("--opcodes takes no other arguments");
            return Opcodes();
        }
        if (args.Length > 0 && args[0] == "--contentid")
        {
            if (args.Length != 2) return UsageError("--contentid takes one hex digest");
            return ContentId(args[1]);
        }
        bool noPe = false, noBodies = false;
        var files = new List<string>();
        foreach (var a in args)
        {
            if (a == "--no-pe") noPe = true;
            else if (a == "--no-bodies") noBodies = true;
            else if (a == "-h" || a == "--help") { Console.Out.Write(Usage); return 0; }
            else if (a.StartsWith("-", StringComparison.Ordinal) && a != "-") return UsageError("unknown option '" + a + "'");
            else files.Add(a);
        }
        if (files.Count == 0) return UsageError("no input files");
        var stdout = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { NewLine = "\n" };
        bool failed = false;
        foreach (var f in files)
        {
            if (files.Count > 1) stdout.WriteLine("File: " + f);
            var text = new StringWriter { NewLine = "\n" };
            try
            {
                new Dumper(File.ReadAllBytes(f), text, noPe, noBodies).Run();
            }
            catch (Exception e)
            {
                stdout.Write(text.ToString());
                stdout.Flush();
                Console.Error.WriteLine("mdump: error: '" + f + "': " + e.Message);
                failed = true;
                continue;
            }
            stdout.Write(text.ToString());
        }
        stdout.Flush();
        return failed ? 1 : 0;
    }

    // --opcodes: System.Reflection.Emit.OpCodes in value order.
    static int Opcodes()
    {
        var ops = typeof(System.Reflection.Emit.OpCodes)
            .GetFields(BindingFlags.Public | BindingFlags.Static)
            .Select(f => (System.Reflection.Emit.OpCode)f.GetValue(null))
            .OrderBy(op => (ushort)op.Value);
        var o = new StringBuilder();
        foreach (var op in ops)
            o.Append(op.Name).Append('\t').Append("0x").Append(((ushort)op.Value).ToString("X4")).Append('\t')
             .Append(op.OperandType).Append('\t').Append(op.StackBehaviourPop).Append('\t')
             .Append(op.StackBehaviourPush).Append('\t').Append(op.FlowControl).Append('\n');
        var bytes = Encoding.UTF8.GetBytes(o.ToString());
        using var stdout = Console.OpenStandardOutput();
        stdout.Write(bytes, 0, bytes.Length);
        return 0;
    }

    // --contentid: BlobContentId.FromHash, printed as '<guid> 0x<stamp>'.
    static int ContentId(string hex)
    {
        byte[] hash;
        try
        {
            hash = Convert.FromHexString(hex);
        }
        catch (FormatException)
        {
            return UsageError("'" + hex + "' is not a hex digest");
        }
        BlobContentId id;
        try
        {
            id = BlobContentId.FromHash(hash);
        }
        catch (ArgumentException e)
        {
            Console.Error.WriteLine("mdump: error: " + e.Message);
            return 1;
        }
        Console.Out.Write(id.Guid.ToString("D") + " 0x" + id.Stamp.ToString("X8") + "\n");
        return 0;
    }
}

sealed class Dumper
{
    readonly byte[] image;
    readonly TextWriter o;
    readonly bool noPe, noBodies;
    PEReader pe;
    MetadataReader md;

    public Dumper(byte[] image, TextWriter o, bool noPe, bool noBodies)
    {
        this.image = image;
        this.o = o;
        this.noPe = noPe;
        this.noBodies = noBodies;
    }

    static string Hex(IEnumerable<byte> b) => string.Join(" ", b.Select(x => x.ToString("X2")));

    string Str(StringHandle h) => h.IsNil ? "<nil>" : md.GetString(h);

    static string Tok(EntityHandle h) => h.IsNil ? "nil" : MetadataTokens.GetToken(h).ToString("X8");

    string BlobHex(BlobHandle b) => b.IsNil ? "<nil>" : Hex(md.GetBlobBytes(b));

    string TypeName(EntityHandle h)
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
                try
                {
                    return "spec:" + ts.DecodeSignature(new Provider(this), null);
                }
                catch (Exception e)
                {
                    return "<bad typespec: " + e.Message + ">";
                }
            }
        }
        return h.Kind.ToString();
    }

    sealed class Provider : ISignatureTypeProvider<string, object>
    {
        readonly Dumper d;
        public Provider(Dumper d) { this.d = d; }
        public string GetArrayType(string e, ArrayShape s) => e + "[" + new string(',', Math.Max(0, s.Rank - 1)) + "]";
        public string GetByReferenceType(string e) => e + "&";
        public string GetFunctionPointerType(MethodSignature<string> s) =>
            "method " + CallConv(s.Header) + s.ReturnType + "*(" + string.Join(", ", s.ParameterTypes) + ")";
        public string GetGenericInstantiation(string g, ImmutableArray<string> a) => g + "<" + string.Join(",", a) + ">";
        public string GetGenericMethodParameter(object c, int i) => "!!" + i;
        public string GetGenericTypeParameter(object c, int i) => "!" + i;
        public string GetModifiedType(string m, string u, bool req) => u + (req ? " modreq(" : " modopt(") + m + ")";
        public string GetPinnedType(string e) => e + " pinned";
        public string GetPointerType(string e) => e + "*";
        public string GetPrimitiveType(PrimitiveTypeCode c) => c switch
        {
            PrimitiveTypeCode.Void => "void", PrimitiveTypeCode.Boolean => "bool", PrimitiveTypeCode.Char => "char",
            PrimitiveTypeCode.SByte => "int8", PrimitiveTypeCode.Byte => "uint8", PrimitiveTypeCode.Int16 => "int16",
            PrimitiveTypeCode.UInt16 => "uint16", PrimitiveTypeCode.Int32 => "int32", PrimitiveTypeCode.UInt32 => "uint32",
            PrimitiveTypeCode.Int64 => "int64", PrimitiveTypeCode.UInt64 => "uint64", PrimitiveTypeCode.Single => "float32",
            PrimitiveTypeCode.Double => "float64", PrimitiveTypeCode.IntPtr => "native int",
            PrimitiveTypeCode.UIntPtr => "native uint", PrimitiveTypeCode.String => "string",
            PrimitiveTypeCode.Object => "object", PrimitiveTypeCode.TypedReference => "typedref",
            _ => c.ToString(),
        };
        public string GetSZArrayType(string e) => e + "[]";
        public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k) =>
            (k == 0x11 ? "valuetype " : "class ") + d.TypeName(h);
        public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k) =>
            (k == 0x11 ? "valuetype " : "class ") + d.TypeName(h);
        public string GetTypeFromSpecification(MetadataReader r, object c, TypeSpecificationHandle h, byte k) => d.TypeName(h);
    }

    static string CallConv(SignatureHeader h)
    {
        var s = new StringBuilder();
        if (h.IsInstance) s.Append("instance ");
        if (h.HasExplicitThis) s.Append("explicit ");
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

    string SigText(BlobHandle b)
    {
        try
        {
            var br = md.GetBlobReader(b);
            var hdr = br.ReadSignatureHeader();
            br.Reset();
            var decoder = new SignatureDecoder<string, object>(new Provider(this), md, null);
            switch (hdr.Kind)
            {
                case SignatureKind.Field:
                    return "field " + decoder.DecodeFieldSignature(ref br);
                case SignatureKind.LocalVariables:
                    return "locals (" + string.Join(", ", decoder.DecodeLocalSignature(ref br)) + ")";
                case SignatureKind.MethodSpecification:
                    return "methodspec <" + string.Join(", ", decoder.DecodeMethodSpecificationSignature(ref br)) + ">";
                default:
                {
                    var s = decoder.DecodeMethodSignature(ref br);
                    return (hdr.Kind == SignatureKind.Property ? "property " : "") + CallConv(s.Header) + s.ReturnType +
                           "(" + string.Join(", ", s.ParameterTypes) + ")";
                }
            }
        }
        catch (Exception e)
        {
            return "<bad signature: " + e.Message + ">";
        }
    }

    public void Run()
    {
        pe = new PEReader(new MemoryStream(image, writable: false));
        if (!pe.HasMetadata) throw new BadImageFormatException("no CLI metadata");
        md = pe.GetMetadataReader();
        if (!noPe) Headers();
        Root();
        Rows();
    }

    void Headers()
    {
        var h = pe.PEHeaders;
        o.WriteLine($"PE.Machine {h.CoffHeader.Machine} 0x{(int)h.CoffHeader.Machine:X4}");
        o.WriteLine($"PE.Characteristics 0x{(int)h.CoffHeader.Characteristics:X4} {h.CoffHeader.Characteristics}");
        o.WriteLine($"PE.TimeDateStamp 0x{h.CoffHeader.TimeDateStamp:X8} Symbols 0x{h.CoffHeader.PointerToSymbolTable:X}/{h.CoffHeader.NumberOfSymbols}");
        var oh = h.PEHeader;
        if (oh != null)
        {
            o.WriteLine($"PE.Magic {oh.Magic} Linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}");
            o.WriteLine($"PE.ImageBase 0x{oh.ImageBase:X} SectionAlign 0x{oh.SectionAlignment:X} FileAlign 0x{oh.FileAlignment:X}");
            o.WriteLine($"PE.OS {oh.MajorOperatingSystemVersion}.{oh.MinorOperatingSystemVersion} Subsys {oh.Subsystem} {oh.MajorSubsystemVersion}.{oh.MinorSubsystemVersion}");
            o.WriteLine($"PE.DllCharacteristics 0x{(int)oh.DllCharacteristics:X4} {oh.DllCharacteristics}");
            o.WriteLine($"PE.SizeOfImage 0x{oh.SizeOfImage:X} SizeOfHeaders 0x{oh.SizeOfHeaders:X} SizeOfCode 0x{oh.SizeOfCode:X} InitData 0x{oh.SizeOfInitializedData:X}");
            o.WriteLine($"PE.Stack 0x{oh.SizeOfStackReserve:X}/0x{oh.SizeOfStackCommit:X} Heap 0x{oh.SizeOfHeapReserve:X}/0x{oh.SizeOfHeapCommit:X}");
            o.WriteLine($"PE.EntryPointRVA 0x{oh.AddressOfEntryPoint:X} BaseOfCode 0x{oh.BaseOfCode:X} CheckSum 0x{oh.CheckSum:X}");
            o.WriteLine($"PE.Dir.Import 0x{oh.ImportTableDirectory.RelativeVirtualAddress:X}/0x{oh.ImportTableDirectory.Size:X} IAT 0x{oh.ImportAddressTableDirectory.RelativeVirtualAddress:X}/0x{oh.ImportAddressTableDirectory.Size:X} Reloc 0x{oh.BaseRelocationTableDirectory.RelativeVirtualAddress:X}/0x{oh.BaseRelocationTableDirectory.Size:X} Debug 0x{oh.DebugTableDirectory.RelativeVirtualAddress:X}/0x{oh.DebugTableDirectory.Size:X} Resource 0x{oh.ResourceTableDirectory.RelativeVirtualAddress:X}/0x{oh.ResourceTableDirectory.Size:X} CLR 0x{oh.CorHeaderTableDirectory.RelativeVirtualAddress:X}/0x{oh.CorHeaderTableDirectory.Size:X}");
        }
        foreach (var s in h.SectionHeaders)
            o.WriteLine($"PE.Section {s.Name} VA 0x{s.VirtualAddress:X} VSize 0x{s.VirtualSize:X} Raw 0x{s.PointerToRawData:X}/0x{s.SizeOfRawData:X} Chars 0x{(uint)s.SectionCharacteristics:X8}");
        var c = h.CorHeader;
        o.WriteLine($"COR.Runtime {c.MajorRuntimeVersion}.{c.MinorRuntimeVersion} Flags 0x{(int)c.Flags:X} {c.Flags} EntryPoint 0x{c.EntryPointTokenOrRelativeVirtualAddress:X8}");
        o.WriteLine($"COR.Metadata 0x{c.MetadataDirectory.RelativeVirtualAddress:X}/0x{c.MetadataDirectory.Size:X} Resources 0x{c.ResourcesDirectory.RelativeVirtualAddress:X}/0x{c.ResourcesDirectory.Size:X} StrongName 0x{c.StrongNameSignatureDirectory.RelativeVirtualAddress:X}/0x{c.StrongNameSignatureDirectory.Size:X}");
    }

    // The metadata root and the #~ header, parsed by hand.
    void Root()
    {
        var raw = pe.GetMetadata().GetContent().ToArray();
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
            int e = q;
            while (raw[e] != 0) e++;
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
            if (n > 0)
                o.WriteLine($"MD.Table {ti} (0x{(int)ti:X2}) rows {n} rowsize {md.GetTableRowSize(ti)} offset 0x{md.GetTableMetadataOffset(ti):X}");
        }
    }

    void Rows()
    {
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
                o.WriteLine($"  Field {Tok(fh)} flags 0x{(int)f.Attributes:X4} '{Str(f.Name)}' sig [{BlobHex(f.Signature)}] {SigText(f.Signature)} rva 0x{f.GetRelativeVirtualAddress():X} offset {f.GetOffset()} CAs {f.GetCustomAttributes().Count}");
            }
            foreach (var ph in t.GetProperties())
            {
                var p = md.GetPropertyDefinition(ph);
                var acc = p.GetAccessors();
                o.WriteLine($"  Property {Tok(ph)} flags 0x{(int)p.Attributes:X4} '{Str(p.Name)}' sig [{BlobHex(p.Signature)}] {SigText(p.Signature)} get {Tok(acc.Getter)} set {Tok(acc.Setter)}");
            }
            foreach (var mh in t.GetMethods()) Method(mh);
        }
        foreach (var h in md.MemberReferences)
        {
            var m = md.GetMemberReference(h);
            string parent = m.Parent.Kind switch
            {
                HandleKind.TypeReference or HandleKind.TypeDefinition or HandleKind.TypeSpecification => TypeName(m.Parent),
                HandleKind.ModuleReference => "module " + Str(md.GetModuleReference((ModuleReferenceHandle)m.Parent).Name),
                _ => Tok(m.Parent),
            };
            o.WriteLine($"MemberRef {Tok(h)} parent {parent} '{Str(m.Name)}' sig [{BlobHex(m.Signature)}] {SigText(m.Signature)}");
        }
        for (int i = 1; i <= md.GetTableRowCount(TableIndex.StandAloneSig); i++)
        {
            var h = MetadataTokens.StandaloneSignatureHandle(i);
            var s = md.GetStandaloneSignature(h);
            o.WriteLine($"StandAloneSig {Tok(h)} [{BlobHex(s.Signature)}] {SigText(s.Signature)}");
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
            o.WriteLine($"MethodSpec {Tok(h)} method {Tok(s.Method)} [{BlobHex(s.Signature)}] {SigText(s.Signature)}");
        }
        var us = MetadataTokens.UserStringHandle(1);
        int usSize = md.GetHeapSize(HeapIndex.UserString);
        while (usSize > 1 && !us.IsNil && MetadataTokens.GetHeapOffset(us) < usSize)
        {
            o.WriteLine($"US 0x{MetadataTokens.GetHeapOffset(us):X} \"{md.GetUserString(us)}\"");
            us = md.GetNextHandle(us);
        }
    }

    void Method(MethodDefinitionHandle mh)
    {
        var m = md.GetMethodDefinition(mh);
        o.WriteLine($"  Method {Tok(mh)} flags 0x{(int)m.Attributes:X4} impl 0x{(int)m.ImplAttributes:X4} '{Str(m.Name)}' sig [{BlobHex(m.Signature)}] {SigText(m.Signature)} rva 0x{m.RelativeVirtualAddress:X} params {m.GetParameters().Count}");
        foreach (var ph in m.GetParameters())
        {
            var pp = md.GetParameter(ph);
            o.WriteLine($"    Param {Tok(ph)} seq {pp.SequenceNumber} flags 0x{(int)pp.Attributes:X} '{Str(pp.Name)}'");
        }
        var imp = m.GetImport();
        if (!imp.Module.IsNil)
            o.WriteLine($"    ImplMap flags 0x{(int)imp.Attributes:X4} entry '{Str(imp.Name)}' module {Tok(imp.Module)}");
        if (noBodies || m.RelativeVirtualAddress == 0) return;
        var sd = pe.GetSectionData(m.RelativeVirtualAddress);
        var b0 = sd.GetContent(0, 1)[0];
        var body = pe.GetMethodBody(m.RelativeVirtualAddress);
        bool tiny = (b0 & 3) == 2;
        o.WriteLine($"    Body {(tiny ? "tiny" : "fat")} hdr0 0x{b0:X2} align4 {m.RelativeVirtualAddress % 4 == 0} size {body.Size} maxstack {body.MaxStack} locals {Tok(body.LocalSignature)} init {body.LocalVariablesInitialized} eh {body.ExceptionRegions.Length}");
        if (!tiny) o.WriteLine($"    FatHeader [{Hex(sd.GetContent(0, 12))}]");
        var il = body.GetILBytes();
        o.WriteLine($"    IL {il.Length} [{Hex(il)}]");
        foreach (var r in body.ExceptionRegions)
            o.WriteLine($"    EH {r.Kind} try {r.TryOffset}+{r.TryLength} handler {r.HandlerOffset}+{r.HandlerLength} filter {r.FilterOffset} catch {Tok(r.CatchType)}");
    }
}
