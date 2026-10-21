// mdinfo: inspect PE/netmodule/raw ECMA-335 metadata with System.Reflection.Metadata.
// Usage:
//   mdinfo pe  <file>            PE headers, CLI header, metadata streams, tables, IL token density
//   mdinfo raw <file> [off len]  metadata root bytes (e.g. a COFF .cormeta section dump)
//   mdinfo il  <file>...         IL token statistics only (one line per file)
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class P {
  static Dictionary<ushort, OperandType> Ops = new();
  static Dictionary<ushort, string> OpNames = new();

  static int Main(string[] a) {
    foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)) {
      var oc = (OpCode)f.GetValue(null);
      Ops[(ushort)oc.Value] = oc.OperandType;
      OpNames[(ushort)oc.Value] = oc.Name;
    }
    try {
      switch (a[0]) {
        case "pe": return DumpPE(a[1]);
        case "raw": {
          var bytes = File.ReadAllBytes(a[1]);
          int off = a.Length > 2 ? Convert.ToInt32(a[2], 16) : 0;
          int len = a.Length > 3 ? Convert.ToInt32(a[3], 16) : bytes.Length - off;
          return DumpRaw(bytes, off, len);
        }
        case "il": {
          foreach (var f in a.Skip(1)) ILStats(f, true);
          return 0;
        }
      }
    } catch (Exception e) {
      Console.WriteLine($"EXCEPTION {e.GetType().Name}: {e.Message}");
      return 2;
    }
    return 1;
  }

  static unsafe int DumpRaw(byte[] bytes, int off, int len) {
    fixed (byte* p = bytes) {
      Streams(bytes, off, len);
      var md = new MetadataReader(p + off, len);
      Tables(md);
    }
    return 0;
  }

  static void Streams(byte[] b, int off, int len) {
    // ECMA-335 II.24.2.1 metadata root
    uint sig = BitConverter.ToUInt32(b, off);
    int vlen = BitConverter.ToInt32(b, off + 12);
    string ver = Encoding.ASCII.GetString(b, off + 16, vlen).TrimEnd('\0');
    int p = off + 16 + vlen;
    ushort flags = BitConverter.ToUInt16(b, p);
    ushort n = BitConverter.ToUInt16(b, p + 2);
    p += 4;
    Console.WriteLine($"metadata root: sig=0x{sig:X8} version='{ver}' streams={n} size=0x{len:X}");
    for (int i = 0; i < n; i++) {
      uint so = BitConverter.ToUInt32(b, p), ss = BitConverter.ToUInt32(b, p + 4);
      int q = p + 8; var sb = new StringBuilder();
      while (b[q] != 0) sb.Append((char)b[q++]);
      p = q + 1; while (((p - (off)) & 3) != 0) p++;
      Console.WriteLine($"  stream '{sb}' offset=0x{so:X} size=0x{ss:X}");
    }
  }

  static int DumpPE(string path) {
    using var fs = File.OpenRead(path);
    using var pe = new PEReader(fs);
    var h = pe.PEHeaders;
    Console.WriteLine($"file: {Path.GetFileName(path)} size={new FileInfo(path).Length}");
    Console.WriteLine($"machine=0x{(ushort)h.CoffHeader.Machine:X4} characteristics={h.CoffHeader.Characteristics} pe32plus={h.PEHeader?.Magic == PEMagic.PE32Plus} isDll={h.IsDll} isExe={h.IsExe}");
    foreach (var s in h.SectionHeaders)
      Console.WriteLine($"  section {s.Name,-8} va=0x{s.VirtualAddress:X} vsize=0x{s.VirtualSize:X} raw=0x{s.SizeOfRawData:X} ptr=0x{s.PointerToRawData:X} chars={s.SectionCharacteristics}");
    if (h.CorHeader == null) { Console.WriteLine("no CLI header"); return 0; }
    var c = h.CorHeader;
    Console.WriteLine($"CLI: flags={c.Flags} entry=0x{c.EntryPointTokenOrRelativeVirtualAddress:X8} metadata rva=0x{c.MetadataDirectory.RelativeVirtualAddress:X} size=0x{c.MetadataDirectory.Size:X}");
    var mdBytes = pe.GetMetadata().GetContent().ToArray();
    Streams(mdBytes, 0, mdBytes.Length);
    var md = pe.GetMetadataReader();
    Tables(md);
    ILStats(path, false);
    return 0;
  }

  static string Name(MetadataReader md, EntityHandle h) {
    switch (h.Kind) {
      case HandleKind.MethodDefinition: return "MethodDef " + md.GetString(md.GetMethodDefinition((MethodDefinitionHandle)h).Name);
      case HandleKind.TypeReference: { var t = md.GetTypeReference((TypeReferenceHandle)h); return "TypeRef " + md.GetString(t.Namespace) + "." + md.GetString(t.Name); }
      case HandleKind.TypeDefinition: { var t = md.GetTypeDefinition((TypeDefinitionHandle)h); return "TypeDef " + md.GetString(t.Namespace) + "." + md.GetString(t.Name); }
      case HandleKind.ModuleReference: return "ModuleRef " + md.GetString(md.GetModuleReference((ModuleReferenceHandle)h).Name);
      case HandleKind.TypeSpecification: return "TypeSpec";
      default: return h.Kind.ToString();
    }
  }

  static void Tables(MetadataReader md) {
    Console.WriteLine($"kind={md.MetadataKind} isAssembly={md.IsAssembly} module='{md.GetString(md.GetModuleDefinition().Name)}' mvid={md.GetGuid(md.GetModuleDefinition().Mvid)}");
    var counts = new List<string>();
    foreach (TableIndex t in Enum.GetValues(typeof(TableIndex))) {
      int n = md.GetTableRowCount(t);
      if (n > 0) counts.Add($"{t}={n}");
    }
    Console.WriteLine("rows: " + string.Join(" ", counts));
    foreach (var r in md.AssemblyReferences) { var ar = md.GetAssemblyReference(r); Console.WriteLine($"  AssemblyRef {md.GetString(ar.Name)} {ar.Version}"); }
    foreach (var r in md.TypeReferences) { var t = md.GetTypeReference(r); Console.WriteLine($"  TypeRef 0x{MetadataTokens.GetToken(r):X8} {md.GetString(t.Namespace)}.{md.GetString(t.Name)} scope={t.ResolutionScope.Kind}"); }
    for (int i = 1; i <= md.GetTableRowCount(TableIndex.ModuleRef); i++) { var h = MetadataTokens.ModuleReferenceHandle(i); Console.WriteLine($"  ModuleRef 0x{MetadataTokens.GetToken(h):X8} {md.GetString(md.GetModuleReference(h).Name)}"); }
    foreach (var th in md.TypeDefinitions) {
      var t = md.GetTypeDefinition(th);
      Console.WriteLine($"  TypeDef 0x{MetadataTokens.GetToken(th):X8} {md.GetString(t.Namespace)}.{md.GetString(t.Name)} attrs={t.Attributes}");
    }
    foreach (var mh in md.MethodDefinitions) {
      var m = md.GetMethodDefinition(mh);
      var imp = m.GetImport();
      string pinv = imp.Module.IsNil ? "" : $" pinvoke({md.GetString(md.GetModuleReference(imp.Module).Name)}!{md.GetString(imp.Name)})";
      Console.WriteLine($"  MethodDef 0x{MetadataTokens.GetToken(mh):X8} {md.GetString(m.Name)} rva=0x{m.RelativeVirtualAddress:X} attrs={m.Attributes}{pinv}");
    }
    foreach (var fh in md.FieldDefinitions) {
      var f = md.GetFieldDefinition(fh);
      Console.WriteLine($"  Field 0x{MetadataTokens.GetToken(fh):X8} {md.GetString(f.Name)} attrs={f.Attributes} rva=0x{f.GetRelativeVirtualAddress():X}");
    }
    foreach (var rh in md.MemberReferences) {
      var r = md.GetMemberReference(rh);
      Console.WriteLine($"  MemberRef 0x{MetadataTokens.GetToken(rh):X8} {md.GetString(r.Name)} parent={Name(md, r.Parent)}");
    }
    foreach (var ca in md.CustomAttributes) {
      var a = md.GetCustomAttribute(ca);
      Console.WriteLine($"  CustomAttribute parent={a.Parent.Kind} ctor={a.Constructor.Kind}");
    }
    foreach (var fh in md.AssemblyFiles) Console.WriteLine($"  File {md.GetString(md.GetAssemblyFile(fh).Name)}");
    foreach (var eh in md.ExportedTypes) { var e = md.GetExportedType(eh); Console.WriteLine($"  ExportedType {md.GetString(e.Namespace)}.{md.GetString(e.Name)} impl={e.Implementation.Kind}"); }
  }

  // Walk every IL body and count token operands: these are the sites a merger must rewrite.
  static void ILStats(string path, bool oneLine) {
    using var fs = File.OpenRead(path);
    using var pe = new PEReader(fs);
    var md = pe.GetMetadataReader();
    long ilBytes = 0, methods = 0, tokens = 0, calls = 0, fields = 0, types = 0, strings = 0, sigs = 0, insns = 0, bodies = 0, bodyBytes = 0;
    var byTable = new Dictionary<string, long>();
    foreach (var mh in md.MethodDefinitions) {
      var m = md.GetMethodDefinition(mh);
      methods++;
      if (m.RelativeVirtualAddress == 0) continue;
      var body = pe.GetMethodBody(m.RelativeVirtualAddress);
      bodies++; bodyBytes += body.Size;
      var il = body.GetILBytes();
      ilBytes += il.Length;
      int i = 0;
      while (i < il.Length) {
        ushort op = il[i++];
        if (op == 0xFE) op = (ushort)(0xFE00 | il[i++]);
        insns++;
        var ot = Ops[op];
        int sz = ot switch {
          OperandType.InlineNone => 0,
          OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
          OperandType.InlineVar => 2,
          OperandType.InlineI8 or OperandType.InlineR => 8,
          OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(il, i),
          _ => 4,
        };
        if (ot is OperandType.InlineMethod or OperandType.InlineField or OperandType.InlineType or OperandType.InlineTok or OperandType.InlineSig or OperandType.InlineString) {
          tokens++;
          uint tok = BitConverter.ToUInt32(il, i);
          string tab = (tok >> 24) switch { 0x06 => "MethodDef", 0x0A => "MemberRef", 0x04 => "Field", 0x01 => "TypeRef", 0x02 => "TypeDef", 0x1B => "TypeSpec", 0x11 => "StandAloneSig", 0x70 => "UserString", 0x2B => "MethodSpec", _ => $"0x{tok >> 24:X2}" };
          byTable[tab] = byTable.GetValueOrDefault(tab) + 1;
          if (ot == OperandType.InlineMethod) calls++;
          if (ot == OperandType.InlineField) fields++;
          if (ot == OperandType.InlineType) types++;
          if (ot == OperandType.InlineString) strings++;
          if (ot == OperandType.InlineSig) sigs++;
        }
        i += sz;
      }
    }
    string tabs = string.Join(" ", byTable.OrderBy(k => k.Key).Select(k => $"{k.Key}={k.Value}"));
    if (oneLine)
      Console.WriteLine($"{Path.GetFileName(path)}\tmethods={methods}\tbodies={bodies}\tbodyBytes={bodyBytes}\tilBytes={ilBytes}\tinsns={insns}\ttokens={tokens}\t{tabs}");
    else
      Console.WriteLine($"IL: methods={methods} bodies={bodies} bodyBytes={bodyBytes} ilBytes={ilBytes} insns={insns} tokenOperands={tokens} ({tabs})");
  }
}
