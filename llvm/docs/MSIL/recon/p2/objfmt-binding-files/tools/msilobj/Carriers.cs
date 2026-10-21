// Carriers.cs: where could an ECMA-335-module object keep the link information that metadata
// cannot express (weak, COMDAT, visibility, data layout, data relocations, ctor priorities)?
// Writes one tiny object per carrier and reads the information back with System.Reflection.Metadata:
//   attr     CustomAttribute rows whose constructors are MemberRefs on [msil.link] placeholder types
//   resource an embedded ManifestResource 'msil.link'
//   section  an extra PE section '.msilnk'
//   pdb      CustomDebugInformation rows in an embedded Portable PDB
//   stream   an extra metadata stream '#MSIL' in the metadata root
using System.Collections.Immutable;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class Carriers
{
    static readonly byte[] LinkBlob = Encoding.UTF8.GetBytes("{\"weak\":[\"f\"],\"relocs\":[[0,\"g\",0]]}");
    static readonly Guid CdiKind = new("6d73696c-0000-4c69-6e6b-496e666f0001");   // made-up kind GUID

    class SectionPE : ManagedPEBuilder
    {
        public SectionPE(PEHeaderBuilder h, MetadataRootBuilder m, BlobBuilder il) : base(h, m, il, flags: CorFlags.ILOnly) { }
        protected override ImmutableArray<Section> CreateSections()
        {
            var b = base.CreateSections().ToBuilder();
            b.Add(new Section(".msilnk", SectionCharacteristics.ContainsInitializedData | SectionCharacteristics.MemRead));
            return b.ToImmutable();
        }
        protected override BlobBuilder SerializeSection(string name, SectionLocation location)
        {
            if (name != ".msilnk") return base.SerializeSection(name, location);
            var bb = new BlobBuilder(); bb.WriteBytes(LinkBlob); return bb;
        }
    }

    public static int Run(string[] args)
    {
        string dir = args.Length > 0 ? args[0] : ".";
        foreach (var kind in new[] { "attr", "resource", "section", "pdb", "stream", "enc" })
        {
            string path = Path.Combine(dir, $"carrier-{kind}.o");
            File.WriteAllBytes(path, Build(kind));
            Console.WriteLine($"{kind,-8} {new FileInfo(path).Length,6} bytes  read back: {ReadBack(kind, path)}");
        }
        return 0;
    }

    static byte[] Build(string kind)
    {
        var mb = new MetadataBuilder();
        mb.AddModule(0, mb.GetOrAddString($"carrier-{kind}.o"), mb.GetOrAddGuid(Guid.NewGuid()), default, default);
        mb.AddAssembly(mb.GetOrAddString($"carrier_{kind}"), new Version(0, 0, 0, 0), default, default, 0, AssemblyHashAlgorithm.Sha1);   // so runtimes can load it
        var mscorlib = mb.AddAssemblyReference(mb.GetOrAddString("mscorlib"), new Version(4, 0, 0, 0), default, mb.GetOrAddBlob(new byte[] { 0xB7, 0x7A, 0x5C, 0x56, 0x19, 0x34, 0xE0, 0x89 }), 0, default);
        mb.AddTypeDefinition(0, default, mb.GetOrAddString("<Module>"), default, MetadataTokens.FieldDefinitionHandle(1), MetadataTokens.MethodDefinitionHandle(1));
        var fsig = new BlobBuilder(); new BlobEncoder(fsig).FieldSignature().IntPtr();
        var g = mb.AddFieldDefinition(FieldAttributes.Public | FieldAttributes.Static | FieldAttributes.InitOnly, mb.GetOrAddString("g"), mb.GetOrAddBlob(fsig));
        var il = new BlobBuilder(); var enc = new MethodBodyStreamEncoder(il);
        var ie = new InstructionEncoder(new BlobBuilder()); ie.LoadConstantI4(1); ie.OpCode(ILOpCode.Ret);
        int off = enc.AddMethodBody(ie);
        var msig = new BlobBuilder(); new BlobEncoder(msig).MethodSignature().Parameters(0, r => r.Type().Int32(), p => { });
        var f = mb.AddMethodDefinition(MethodAttributes.Public | MethodAttributes.Static, 0, mb.GetOrAddString("f"), mb.GetOrAddBlob(msig), off, MetadataTokens.ParameterHandle(1));
        var managedResources = new BlobBuilder();
        DebugDirectoryBuilder? dbg = null;
        int padding = 0;
        switch (kind)
        {
            case "attr":
                {
                    // [msil.link]'<msil>'.Weak on f, and [msil.link]'<msil>'.Reloc(uint8[]) on g
                    var link = mb.AddAssemblyReference(mb.GetOrAddString("msil.link"), new Version(0, 0, 0, 0), default, default, 0, default);
                    var weak = mb.AddTypeReference(link, mb.GetOrAddString("<msil>"), mb.GetOrAddString("Weak"));
                    var rel = mb.AddTypeReference(link, mb.GetOrAddString("<msil>"), mb.GetOrAddString("Relocs"));
                    var c0 = new BlobBuilder(); new BlobEncoder(c0).MethodSignature(isInstanceMethod: true).Parameters(0, r => r.Void(), p => { });
                    var c1 = new BlobBuilder(); new BlobEncoder(c1).MethodSignature(isInstanceMethod: true).Parameters(1, r => r.Void(), p => p.AddParameter().Type().SZArray().Byte());
                    var weakCtor = mb.AddMemberReference(weak, mb.GetOrAddString(".ctor"), mb.GetOrAddBlob(c0));
                    var relCtor = mb.AddMemberReference(rel, mb.GetOrAddString(".ctor"), mb.GetOrAddBlob(c1));
                    mb.AddCustomAttribute(f, weakCtor, mb.GetOrAddBlob(new byte[] { 1, 0, 0, 0 }));
                    var v = new BlobBuilder(); v.WriteUInt16(1); v.WriteInt32(LinkBlob.Length); v.WriteBytes(LinkBlob); v.WriteUInt16(0);
                    mb.AddCustomAttribute(g, relCtor, mb.GetOrAddBlob(v));
                    break;
                }
            case "resource":
                managedResources.WriteInt32(LinkBlob.Length); managedResources.WriteBytes(LinkBlob);
                mb.AddManifestResource(ManifestResourceAttributes.Private, mb.GetOrAddString("msil.link"), default, 0);
                break;
            case "pdb":
                {
                    var pdb = new MetadataBuilder();
                    pdb.AddCustomDebugInformation(f, pdb.GetOrAddGuid(CdiKind), pdb.GetOrAddBlob(Encoding.UTF8.GetBytes("weak")));
                    pdb.AddCustomDebugInformation(g, pdb.GetOrAddGuid(CdiKind), pdb.GetOrAddBlob(LinkBlob));
                    pdb.AddCustomDebugInformation(EntityHandle.ModuleDefinition, pdb.GetOrAddGuid(CdiKind), pdb.GetOrAddBlob(Encoding.UTF8.GetBytes("ctors:")));
                    var rowCounts = mb.GetRowCounts();
                    var ppb = new PortablePdbBuilder(pdb, rowCounts, default);
                    var pdbBlob = new BlobBuilder(); ppb.Serialize(pdbBlob);
                    dbg = new DebugDirectoryBuilder();
                    dbg.AddEmbeddedPortablePdbEntry(pdbBlob, ppb.FormatVersion);
                    break;
                }
            case "enc":
                // Edit-and-Continue tables in a full (non-delta) image: precedent only
                mb.AddEncLogEntry(f, EditAndContinueOperation.Default);
                mb.AddEncLogEntry(g, EditAndContinueOperation.Default);
                mb.AddEncMapEntry(g); mb.AddEncMapEntry(f);
                break;
            case "stream":
                padding = 16 + 8 + ((LinkBlob.Length + 3) & ~3);
                mb.GetOrAddBlob(new byte[padding + 8]);   // tail of #Blob, given to '#MSIL' afterwards
                break;
        }
        var header = new PEHeaderBuilder(machine: Machine.Amd64, imageCharacteristics: Characteristics.ExecutableImage | Characteristics.Dll | Characteristics.LargeAddressAware);
        var root = new MetadataRootBuilder(mb);
        var pe = kind == "section" ? new SectionPE(header, root, il)
                 : new ManagedPEBuilder(header, root, il, managedResources: kind == "resource" ? managedResources : null, debugDirectoryBuilder: dbg, flags: CorFlags.ILOnly);
        var outb = new BlobBuilder(); pe.Serialize(outb);
        var bytes = outb.ToArray();
        if (kind == "stream") AddStream(bytes, "#MSIL", LinkBlob, padding + 8);
        return bytes;
    }

    // Moves the stream data 16 bytes down, adds a '#MSIL' header, shrinks #Blob (whose tail is unused
    // padding) and puts the new stream there. The metadata size and every RVA stay the same.
    static void AddStream(byte[] pe, string name, byte[] data, int spare)
    {
        using var r = new PEReader(ImmutableArray.Create(pe));
        var dir = r.PEHeaders.CorHeader!.MetadataDirectory;
        int mdOff = r.PEHeaders.TryGetDirectoryOffset(dir, out var o) ? o : throw new Exception("no metadata");
        int p = mdOff + 12; int verLen = BitConverter.ToInt32(pe, p); p += 4 + verLen + 2;
        int nStreams = BitConverter.ToUInt16(pe, p); int countAt = p; p += 2;
        var hdrs = new List<(int Off, int Size, string Name)>();
        for (int i = 0; i < nStreams; i++)
        {
            int so = BitConverter.ToInt32(pe, p), ss = BitConverter.ToInt32(pe, p + 4); int q = p + 8; var sb = new StringBuilder();
            while (pe[q] != 0) sb.Append((char)pe[q++]);
            hdrs.Add((so, ss, sb.ToString())); p = (q + 4) & ~3;
        }
        int headersEnd = p;
        int newHdr = 8 + ((name.Length + 4) & ~3);
        int dataStart = mdOff + hdrs.Min(h => h.Off);
        int dataEnd = mdOff + hdrs.Max(h => h.Off + h.Size);
        Buffer.BlockCopy(pe, dataStart, pe, dataStart + newHdr, dataEnd - dataStart - newHdr);   // the last newHdr bytes are padding of #Blob
        var blob = hdrs.Single(h => h.Name == "#Blob");
        int newData = (data.Length + 3) & ~3;
        var w = new MemoryStream(); var bw = new BinaryWriter(w);
        foreach (var h in hdrs)
        {
            int size = h.Name == "#Blob" ? h.Size - newHdr - newData : h.Size;
            bw.Write(h.Off + newHdr); bw.Write(size); bw.Write(Encoding.ASCII.GetBytes(h.Name)); for (int k = h.Name.Length; k < ((h.Name.Length + 4) & ~3); k++) bw.Write((byte)0);
        }
        int streamOff = blob.Off + newHdr + blob.Size - newHdr - newData;
        bw.Write(streamOff); bw.Write(newData); bw.Write(Encoding.ASCII.GetBytes(name)); for (int k = name.Length; k < ((name.Length + 4) & ~3); k++) bw.Write((byte)0);
        var hb = w.ToArray();
        if (countAt + 2 + hb.Length > dataStart + newHdr) throw new Exception("header overflow");
        BitConverter.TryWriteBytes(pe.AsSpan(countAt), (ushort)(nStreams + 1));
        Buffer.BlockCopy(hb, 0, pe, countAt + 2, hb.Length);
        Array.Clear(pe, mdOff + streamOff, newData);
        Buffer.BlockCopy(data, 0, pe, mdOff + streamOff, data.Length);
    }

    static string ReadBack(string kind, string path)
    {
        using var pe = new PEReader(File.OpenRead(path));
        var md = pe.GetMetadataReader();
        switch (kind)
        {
            case "attr":
                return string.Join("; ", md.CustomAttributes.Select(h =>
                {
                    var ca = md.GetCustomAttribute(h); var ctor = md.GetMemberReference((MemberReferenceHandle)ca.Constructor);
                    var t = md.GetTypeReference((TypeReferenceHandle)ctor.Parent);
                    return $"{md.GetString(t.Name)} on 0x{MetadataTokens.GetToken(ca.Parent):X8}, value {md.GetBlobBytes(ca.Value).Length} bytes";
                }));
            case "resource":
                {
                    var r = md.GetManifestResource(md.ManifestResources.First());
                    var rd = pe.GetSectionData(pe.PEHeaders.CorHeader!.ResourcesDirectory.RelativeVirtualAddress).GetReader();
                    rd.Offset = (int)r.Offset; int len = rd.ReadInt32();
                    return $"{md.GetString(r.Name)}: {Encoding.UTF8.GetString(rd.ReadBytes(len))}";
                }
            case "section":
                {
                    var s = pe.PEHeaders.SectionHeaders.Single(h => h.Name == ".msilnk");
                    var bytes = pe.GetSectionData(s.VirtualAddress).GetContent(0, LinkBlob.Length).ToArray();
                    return $"{s.Name} at RVA 0x{s.VirtualAddress:X}: {Encoding.UTF8.GetString(bytes)}";
                }
            case "pdb":
                {
                    var e = pe.ReadDebugDirectory().Single(d => d.Type == DebugDirectoryEntryType.EmbeddedPortablePdb);
                    using var prov = pe.ReadEmbeddedPortablePdbDebugDirectoryData(e);
                    var pmd = prov.GetMetadataReader();
                    return string.Join("; ", pmd.CustomDebugInformation.Select(h =>
                    {
                        var c = pmd.GetCustomDebugInformation(h);
                        return $"0x{MetadataTokens.GetToken(c.Parent):X8}: {Encoding.UTF8.GetString(pmd.GetBlobBytes(c.Value))}";
                    }));
                }
            case "enc":
                {
                    var mdb = pe.GetMetadata().GetContent().ToArray();
                    int verLen = BitConverter.ToInt32(mdb.AsSpan(12)); int p = 16 + verLen + 2; int n = BitConverter.ToUInt16(mdb.AsSpan(p)); p += 2;
                    int q = p + 8; var sb = new StringBuilder(); while (mdb[q] != 0) sb.Append((char)mdb[q++]);
                    return $"first stream {sb}, EncLog rows {md.GetTableRowCount(TableIndex.EncLog)}, EncMap rows {md.GetTableRowCount(TableIndex.EncMap)}";
                }
            case "stream":
                {
                    // SRM ignores unknown streams; find it by hand to prove it is there.
                    var mdb = pe.GetMetadata().GetContent().ToArray();
                    int verLen = BitConverter.ToInt32(mdb.AsSpan(12)); int p = 16 + verLen + 2; int n = BitConverter.ToUInt16(mdb.AsSpan(p)); p += 2;
                    var names = new List<string>(); string found = "";
                    for (int i = 0; i < n; i++)
                    {
                        int so = BitConverter.ToInt32(mdb.AsSpan(p)), ss = BitConverter.ToInt32(mdb.AsSpan(p + 4)); int q = p + 8; var sb = new StringBuilder();
                        while (mdb[q] != 0) sb.Append((char)mdb[q++]);
                        names.Add(sb.ToString()); if (sb.ToString() == "#MSIL") found = Encoding.UTF8.GetString(mdb.AsSpan(so, LinkBlob.Length));
                        p = (q + 4) & ~3;
                    }
                    return $"streams [{string.Join(", ", names)}], SRM methods {md.MethodDefinitions.Count}, #MSIL: {found}";
                }
        }
        return "?";
    }
}
