// netmerge: an ILMerge-style merger for Phase-1-shaped netmodules (recon option C/B prototype).
//
//   netmerge <out.dll|out.exe> <in1.netmodule> [<in2.netmodule> ...]
//
// Every input is a netmodule whose code lives in global methods and fields of <Module>
// (exactly what the Phase-1 MSIL backend emits). Undefined C symbols are expressed with
// existing ECMA-335 rows:
//   - a function: a body-less MethodDef with an ImplMap row naming the ModuleRef
//     'msil.unresolved' (the P/Invoke shape), or a MemberRef whose parent is that ModuleRef;
//   - a variable (its address field): a field MemberRef whose parent is that ModuleRef.
// The merger maps every row of every input to a row of one output module, resolves the
// placeholders by name (checking the signatures), renames colliding non-public names,
// turns each input's .cctor into '<msil>cctor$N' and emits one .cctor calling them in
// input order, rewrites every token in every IL body and every TypeDefOrRef in every
// signature blob, copies RVA data, and writes a deterministic PE with ManagedPEBuilder.
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;

sealed class Mod {
  public int Index;
  public string Path;
  public PEReader PE;
  public MetadataReader MD;
}

sealed class MergeError : Exception { public MergeError(string m) : base(m) { } }

static class NetMerge {
  const string Unresolved = "msil.unresolved";
  static readonly Dictionary<ushort, OperandType> Ops = new();
  static readonly MetadataBuilder MB = new();
  static readonly List<Mod> Mods = new();
  // Planned output rows.
  static readonly Dictionary<(int, int), int> MethodRow = new(), FieldRow = new(), TypeDefRow = new();
  static readonly Dictionary<(int, int), EntityHandle> MemberRefMap = new(), TypeRefMap = new(), SigMap = new();
  static readonly Dictionary<string, EntityHandle> TypeRefByKey = new(), MemberRefByKey = new();
  static readonly Dictionary<string, AssemblyReferenceHandle> AsmRefByKey = new();
  static readonly Dictionary<string, ModuleReferenceHandle> ModRefByName = new();
  static readonly Dictionary<string, (int m, int row)> GlobalMethods = new(), GlobalFields = new();
  static int Stats_Tokens, Stats_SigTypeRefs, Stats_Resolved, Stats_Renamed;

  static int Main(string[] args) {
    foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static)) {
      var oc = (OpCode)f.GetValue(null);
      Ops[(ushort)oc.Value] = oc.OperandType;
    }
    try {
      string output = args[0];
      for (int i = 1; i < args.Length; i++) {
        var pe = new PEReader(File.OpenRead(args[i]));
        Mods.Add(new Mod { Index = i - 1, Path = args[i], PE = pe, MD = pe.GetMetadataReader() });
      }
      Merge(output);
      Console.WriteLine($"merged {Mods.Count} modules -> {output}: methods={MethodRow.Count} fields={FieldRow.Count} tokensRewritten={Stats_Tokens} sigTypeRefsRewritten={Stats_SigTypeRefs} placeholdersResolved={Stats_Resolved} renamed={Stats_Renamed}");
      return 0;
    } catch (MergeError e) {
      Console.Error.WriteLine("netmerge: error: " + e.Message);
      return 1;
    }
  }

  static bool IsUnresolvedModule(MetadataReader md, ModuleReferenceHandle h) => md.GetString(md.GetModuleReference(h).Name) == Unresolved;
  static bool IsStub(MetadataReader md, MethodDefinition m) {
    var imp = m.GetImport();
    return !imp.Module.IsNil && IsUnresolvedModule(md, imp.Module);
  }

  static void Merge(string output) {
    // ---- 1. Symbol table: public definitions (C external linkage) by name.
    var pinvokeByKey = new Dictionary<string, (int, int)>();
    var plannedMethods = new List<(int m, int row, string name, MethodAttributes attrs)>();
    var plannedFields = new List<(int m, int row, string name)>();
    var usedMethodNames = new HashSet<string>();
    var usedFieldNames = new HashSet<string>();
    var aliasMethod = new Dictionary<(int, int), (int, int)>();   // stub or duplicate P/Invoke -> definition
    foreach (var mod in Mods) {
      var md = mod.MD;
      foreach (var h in md.MethodDefinitions) {
        var m = md.GetMethodDefinition(h);
        string name = md.GetString(m.Name);
        bool pub = (m.Attributes & MethodAttributes.MemberAccessMask) == MethodAttributes.Public;
        if (IsStub(md, m) || !pub || name == ".cctor") continue;
        if (!m.GetImport().Module.IsNil) continue;   // real P/Invokes are deduplicated below
        if (GlobalMethods.TryGetValue(name, out var prev))
          throw new MergeError($"duplicate symbol '{name}' in {Path.GetFileName(Mods[prev.m].Path)} and {Path.GetFileName(mod.Path)}");
        GlobalMethods[name] = (mod.Index, MetadataTokens.GetRowNumber(h));
      }
      foreach (var h in md.FieldDefinitions) {
        var f = md.GetFieldDefinition(h);
        if ((f.Attributes & FieldAttributes.FieldAccessMask) != FieldAttributes.Public) continue;
        string name = md.GetString(f.Name);
        if (GlobalFields.ContainsKey(name)) throw new MergeError($"duplicate symbol '{name}' (variable)");
        GlobalFields[name] = (mod.Index, MetadataTokens.GetRowNumber(h));
      }
    }
    // ---- 2. Plan MethodDef rows: definitions and real P/Invokes (deduplicated); stubs alias.
    foreach (var mod in Mods) {
      var md = mod.MD;
      foreach (var h in md.MethodDefinitions) {
        var m = md.GetMethodDefinition(h);
        int row = MetadataTokens.GetRowNumber(h);
        string name = md.GetString(m.Name);
        if (IsStub(md, m)) {
          if (!GlobalMethods.TryGetValue(name, out var def)) throw new MergeError($"undefined symbol '{name}' referenced by {Path.GetFileName(mod.Path)}");
          aliasMethod[(mod.Index, row)] = def; Stats_Resolved++;
          continue;
        }
        var imp = m.GetImport();
        if (!imp.Module.IsNil) {
          string key = md.GetString(md.GetModuleReference(imp.Module).Name) + "!" + md.GetString(imp.Name) + "!" + name;
          if (pinvokeByKey.TryGetValue(key, out var first)) { aliasMethod[(mod.Index, row)] = first; continue; }
          pinvokeByKey[key] = (mod.Index, row);
        }
        var attrs = m.Attributes;
        if (name == ".cctor") {
          name = "<msil>cctor$" + mod.Index;
          attrs &= ~(MethodAttributes.SpecialName | MethodAttributes.RTSpecialName);
        }
        bool pub = (attrs & MethodAttributes.MemberAccessMask) == MethodAttributes.Public;
        if (!pub && usedMethodNames.Contains(name)) { name = name + "$" + mod.Index; Stats_Renamed++; }
        if (usedMethodNames.Contains(name) && !pub) throw new MergeError($"cannot rename '{name}'");
        usedMethodNames.Add(name);
        plannedMethods.Add((mod.Index, row, name, attrs));
      }
      foreach (var h in md.FieldDefinitions) {
        var f = md.GetFieldDefinition(h);
        string name = md.GetString(f.Name);
        bool pub = (f.Attributes & FieldAttributes.FieldAccessMask) == FieldAttributes.Public;
        if (!pub && usedFieldNames.Contains(name)) { name = name + "$" + mod.Index; Stats_Renamed++; }
        usedFieldNames.Add(name);
        plannedFields.Add((mod.Index, MetadataTokens.GetRowNumber(h), name));
      }
    }
    for (int i = 0; i < plannedMethods.Count; i++) MethodRow[(plannedMethods[i].m, plannedMethods[i].row)] = i + 1;
    foreach (var kv in aliasMethod) MethodRow[kv.Key] = MethodRow[kv.Value];
    for (int i = 0; i < plannedFields.Count; i++) FieldRow[(plannedFields[i].m, plannedFields[i].row)] = i + 1;
    int cctorRow = plannedMethods.Count + 1;
    // TypeDefs: every <Module> is row 1; other types follow (Phase 1: memberless layout types).
    var plannedTypes = new List<(int m, int row, string name)>();
    var usedTypeNames = new HashSet<string>();
    foreach (var mod in Mods) {
      foreach (var h in mod.MD.TypeDefinitions) {
        int row = MetadataTokens.GetRowNumber(h);
        if (row == 1) { TypeDefRow[(mod.Index, 1)] = 1; continue; }
        var t = mod.MD.GetTypeDefinition(h);
        if (t.GetMethods().Count != 0 || t.GetFields().Count != 0) throw new MergeError("types with members are not handled by this prototype");
        string name = mod.MD.GetString(t.Name);
        if (usedTypeNames.Contains(name)) { name = name + "$" + mod.Index; Stats_Renamed++; }
        usedTypeNames.Add(name);
        plannedTypes.Add((mod.Index, row, name));
        TypeDefRow[(mod.Index, row)] = plannedTypes.Count + 1;
      }
    }

    // ---- 3. Emit. Module and Assembly first.
    byte[] hash = SHA256.HashData(Mods.SelectMany(m => File.ReadAllBytes(m.Path)).ToArray());
    var mvid = new Guid(hash.AsSpan(0, 16));
    string asmName = Path.GetFileNameWithoutExtension(output);
    MB.AddModule(0, MB.GetOrAddString(Path.GetFileName(output)), MB.GetOrAddGuid(mvid), default, default);
    MB.AddAssembly(MB.GetOrAddString(asmName), new Version(0, 0, 0, 0), default, default, 0, AssemblyHashAlgorithm.Sha1);
    // TypeDefs
    int nFields = plannedFields.Count, nMethods = plannedMethods.Count + 1;
    MB.AddTypeDefinition(0, default, MB.GetOrAddString("<Module>"), default, MetadataTokens.FieldDefinitionHandle(1), MetadataTokens.MethodDefinitionHandle(1));
    foreach (var (m, row, name) in plannedTypes) {
      var md = Mods[m].MD;
      var t = md.GetTypeDefinition(MetadataTokens.TypeDefinitionHandle(row));
      var bt = t.BaseType.IsNil ? default : MapType(Mods[m], t.BaseType);
      var nh = MB.AddTypeDefinition(t.Attributes, MB.GetOrAddString(md.GetString(t.Namespace)), MB.GetOrAddString(name), bt,
          MetadataTokens.FieldDefinitionHandle(nFields + 1), MetadataTokens.MethodDefinitionHandle(nMethods + 1));
      var layout = t.GetLayout();
      if (!layout.IsDefault) MB.AddTypeLayout(nh, (ushort)layout.PackingSize, (uint)layout.Size);
    }
    // Fields (+ RVA data)
    var mapped = new BlobBuilder();
    foreach (var (m, row, name) in plannedFields) {
      var mod = Mods[m]; var md = mod.MD;
      var f = md.GetFieldDefinition(MetadataTokens.FieldDefinitionHandle(row));
      var nh = MB.AddFieldDefinition(f.Attributes, MB.GetOrAddString(name), RewriteSig(mod, f.Signature));
      int rva = f.GetRelativeVirtualAddress();
      if (rva != 0) {
        int size = FieldDataSize(mod, f);
        mapped.Align(8);
        MB.AddFieldRelativeVirtualAddress(nh, mapped.Count);
        var data = mod.PE.GetSectionData(rva);
        unsafe { mapped.WriteBytes(data.Pointer, size); }
      }
    }
    // Methods (+ params, P/Invoke rows), bodies rewritten.
    var il = new BlobBuilder();
    var enc = new MethodBodyStreamEncoder(il);
    int nextParam = 1;
    MethodDefinitionHandle entry = default;
    foreach (var (m, row, name, attrs) in plannedMethods) {
      var mod = Mods[m]; var md = mod.MD;
      var h = MetadataTokens.MethodDefinitionHandle(row);
      var mdef = md.GetMethodDefinition(h);
      int bodyOffset = -1;
      if (mdef.RelativeVirtualAddress != 0) bodyOffset = RewriteBody(mod, mdef, enc);
      var ps = mdef.GetParameters().Select(p => md.GetParameter(p)).ToList();
      var nh = MB.AddMethodDefinition(attrs, mdef.ImplAttributes, MB.GetOrAddString(name), RewriteSig(mod, mdef.Signature),
          bodyOffset, MetadataTokens.ParameterHandle(nextParam));
      foreach (var p in ps) { MB.AddParameter(p.Attributes, MB.GetOrAddString(md.GetString(p.Name)), p.SequenceNumber); nextParam++; }
      var imp = mdef.GetImport();
      if (!imp.Module.IsNil)
        MB.AddMethodImport(nh, imp.Attributes, MB.GetOrAddString(md.GetString(imp.Name)), GetModRef(md.GetString(md.GetModuleReference(imp.Module).Name)));
      if (name == "<msil>Main") { if (!entry.IsNil) throw new MergeError("two entry points"); entry = nh; }
    }
    // The merged module initializer: call every input's initializer in input order.
    var cctorIL = new BlobBuilder();
    var ie = new InstructionEncoder(cctorIL);
    foreach (var (m, row, name, attrs) in plannedMethods)
      if (name.StartsWith("<msil>cctor$")) ie.Call(MetadataTokens.MethodDefinitionHandle(MethodRow[(m, row)]));
    ie.OpCode(ILOpCode.Ret);
    var cctorSig = new BlobBuilder();
    new BlobEncoder(cctorSig).MethodSignature().Parameters(0, r => r.Void(), p => { });
    int cctorBody = enc.AddMethodBody(ie, maxStack: 8, localVariablesSignature: default, attributes: MethodBodyAttributes.None);
    MB.AddMethodDefinition(MethodAttributes.Private | MethodAttributes.Static | MethodAttributes.HideBySig | MethodAttributes.SpecialName | MethodAttributes.RTSpecialName,
        MethodImplAttributes.IL, MB.GetOrAddString(".cctor"), MB.GetOrAddBlob(cctorSig), cctorBody, MetadataTokens.ParameterHandle(nextParam));

    // ---- 4. Write the PE (deterministic: MVID from the inputs, fixed timestamp).
    bool exe = output.EndsWith(".exe", StringComparison.OrdinalIgnoreCase);
    var header = new PEHeaderBuilder(machine: Machine.Amd64,
        imageCharacteristics: (exe ? 0 : Characteristics.Dll) | Characteristics.ExecutableImage | Characteristics.LargeAddressAware,
        subsystem: Subsystem.WindowsCui);
    var pe = new ManagedPEBuilder(header, new MetadataRootBuilder(MB), il, mappedFieldData: mapped, entryPoint: entry,
        flags: CorFlags.ILOnly, strongNameSignatureSize: 0,
        deterministicIdProvider: blobs => BlobContentId.FromHash(ImmutableArray.Create(SHA256.HashData(blobs.SelectMany(b => b.GetBytes().ToArray()).ToArray()))));
    var outBlob = new BlobBuilder();
    pe.Serialize(outBlob);
    using var fs = File.Create(output);
    outBlob.WriteContentTo(fs);
  }

  static int FieldDataSize(Mod mod, FieldDefinition f) {
    var r = mod.MD.GetBlobReader(f.Signature);
    if (r.ReadByte() != 0x06) throw new MergeError("bad field sig");
    var code = r.ReadByte();
    if (code == 0x11) {   // valuetype T: size from T's ClassLayout
      var th = r.ReadTypeHandle();
      if (th.Kind == HandleKind.TypeDefinition) return mod.MD.GetTypeDefinition((TypeDefinitionHandle)th).GetLayout().Size;
    }
    throw new MergeError("RVA field of unsupported type");
  }

  static ModuleReferenceHandle GetModRef(string name) {
    if (!ModRefByName.TryGetValue(name, out var h)) ModRefByName[name] = h = MB.AddModuleReference(MB.GetOrAddString(name));
    return h;
  }

  static EntityHandle MapAsmRef(Mod mod, AssemblyReferenceHandle h) {
    var md = mod.MD; var a = md.GetAssemblyReference(h);
    string key = md.GetString(a.Name) + "|" + a.Version + "|" + md.GetString(a.Culture) + "|" + Convert.ToHexString(md.GetBlobBytes(a.PublicKeyOrToken));
    if (!AsmRefByKey.TryGetValue(key, out var nh))
      AsmRefByKey[key] = nh = MB.AddAssemblyReference(MB.GetOrAddString(md.GetString(a.Name)), a.Version, MB.GetOrAddString(md.GetString(a.Culture)),
          MB.GetOrAddBlob(md.GetBlobBytes(a.PublicKeyOrToken)), a.Flags, default);
    return nh;
  }

  static EntityHandle MapType(Mod mod, EntityHandle h) {
    var md = mod.MD;
    switch (h.Kind) {
      case HandleKind.TypeDefinition:
        return MetadataTokens.TypeDefinitionHandle(TypeDefRow[(mod.Index, MetadataTokens.GetRowNumber(h))]);
      case HandleKind.TypeReference: {
        var key0 = (mod.Index, MetadataTokens.GetRowNumber(h));
        if (TypeRefMap.TryGetValue(key0, out var done)) return done;
        var t = md.GetTypeReference((TypeReferenceHandle)h);
        EntityHandle scope = t.ResolutionScope.Kind switch {
          HandleKind.AssemblyReference => MapAsmRef(mod, (AssemblyReferenceHandle)t.ResolutionScope),
          HandleKind.TypeReference => MapType(mod, (EntityHandle)t.ResolutionScope),
          _ => throw new MergeError("TypeRef scope " + t.ResolutionScope.Kind),
        };
        string key = MetadataTokens.GetToken(scope) + "|" + md.GetString(t.Namespace) + "|" + md.GetString(t.Name);
        if (!TypeRefByKey.TryGetValue(key, out var nh))
          TypeRefByKey[key] = nh = MB.AddTypeReference(scope, MB.GetOrAddString(md.GetString(t.Namespace)), MB.GetOrAddString(md.GetString(t.Name)));
        return TypeRefMap[key0] = nh;
      }
      default: throw new MergeError("type handle " + h.Kind);
    }
  }

  // Resolve a MemberRef: placeholders become definitions; BCL references are re-created.
  static EntityHandle MapMemberRef(Mod mod, MemberReferenceHandle h) {
    var key0 = (mod.Index, MetadataTokens.GetRowNumber(h));
    if (MemberRefMap.TryGetValue(key0, out var done)) return done;
    var md = mod.MD; var r = md.GetMemberReference(h);
    string name = md.GetString(r.Name);
    var sig = RewriteSig(mod, r.Signature);
    EntityHandle result;
    bool placeholder = r.Parent.Kind == HandleKind.ModuleReference && IsUnresolvedModule(md, (ModuleReferenceHandle)r.Parent)
        || r.Parent.Kind == HandleKind.TypeDefinition && MetadataTokens.GetRowNumber(r.Parent) == 1;   // MSVC /clr shape
    if (placeholder) {
      bool isField = md.GetBlobReader(r.Signature).ReadByte() == 0x06;
      if (isField) {
        if (!GlobalFields.TryGetValue(name, out var def)) throw new MergeError($"undefined variable '{name}'");
        var dm = Mods[def.m];
        var dsig = RewriteSig(dm, dm.MD.GetFieldDefinition(MetadataTokens.FieldDefinitionHandle(def.row)).Signature);
        if (!dsig.Equals(sig)) throw new MergeError($"type mismatch for '{name}'");
        result = MetadataTokens.FieldDefinitionHandle(FieldRow[def]);
      } else {
        if (!GlobalMethods.TryGetValue(name, out var def)) throw new MergeError($"undefined symbol '{name}'");
        var dm = Mods[def.m];
        var dsig = RewriteSig(dm, dm.MD.GetMethodDefinition(MetadataTokens.MethodDefinitionHandle(def.row)).Signature);
        if (!dsig.Equals(sig)) throw new MergeError($"signature mismatch for '{name}'");
        result = MetadataTokens.MethodDefinitionHandle(MethodRow[def]);
      }
      Stats_Resolved++;
    } else {
      EntityHandle parent = r.Parent.Kind switch {
        HandleKind.TypeReference or HandleKind.TypeDefinition => MapType(mod, r.Parent),
        HandleKind.ModuleReference => GetModRef(md.GetString(md.GetModuleReference((ModuleReferenceHandle)r.Parent).Name)),
        _ => throw new MergeError("MemberRef parent " + r.Parent.Kind),
      };
      string key = MetadataTokens.GetToken(parent) + "|" + name + "|" + MetadataTokens.GetHeapOffset(sig);
      if (!MemberRefByKey.TryGetValue(key, out result))
        MemberRefByKey[key] = result = MB.AddMemberReference(parent, MB.GetOrAddString(name), sig);
    }
    return MemberRefMap[key0] = result;
  }

  static EntityHandle MapSig(Mod mod, StandaloneSignatureHandle h) {
    var key0 = (mod.Index, MetadataTokens.GetRowNumber(h));
    if (!SigMap.TryGetValue(key0, out var nh))
      SigMap[key0] = nh = MB.AddStandaloneSignature(RewriteSig(mod, mod.MD.GetStandaloneSignature(h).Signature));
    return nh;
  }

  static EntityHandle MapToken(Mod mod, int token) {
    var h = MetadataTokens.EntityHandle(token);
    switch (h.Kind) {
      case HandleKind.MethodDefinition: return MetadataTokens.MethodDefinitionHandle(MethodRow[(mod.Index, MetadataTokens.GetRowNumber(h))]);
      case HandleKind.FieldDefinition: return MetadataTokens.FieldDefinitionHandle(FieldRow[(mod.Index, MetadataTokens.GetRowNumber(h))]);
      case HandleKind.MemberReference: return MapMemberRef(mod, (MemberReferenceHandle)h);
      case HandleKind.TypeReference: case HandleKind.TypeDefinition: return MapType(mod, h);
      case HandleKind.StandaloneSignature: return MapSig(mod, (StandaloneSignatureHandle)h);
      default: throw new MergeError("token kind " + h.Kind);
    }
  }

  static int RewriteBody(Mod mod, MethodDefinition m, MethodBodyStreamEncoder enc) {
    var body = mod.PE.GetMethodBody(m.RelativeVirtualAddress);
    if (body.ExceptionRegions.Length != 0) throw new MergeError("exception regions are not handled by this prototype");
    var code = body.GetILBytes();
    bool localloc = false;
    int i = 0;
    while (i < code.Length) {
      ushort op = code[i++];
      if (op == 0xFE) { op = (ushort)(0xFE00 | code[i++]); if (op == 0xFE0F) localloc = true; }
      var ot = Ops[op];
      int sz = ot switch {
        OperandType.InlineNone => 0,
        OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
        OperandType.InlineVar => 2,
        OperandType.InlineI8 or OperandType.InlineR => 8,
        OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(code, i),
        _ => 4,
      };
      if (ot is OperandType.InlineMethod or OperandType.InlineField or OperandType.InlineType or OperandType.InlineTok or OperandType.InlineSig) {
        int tok = BitConverter.ToInt32(code, i);
        int nt = MetadataTokens.GetToken(MapToken(mod, tok));
        BitConverter.TryWriteBytes(code.AsSpan(i, 4), nt);
        Stats_Tokens++;
      } else if (ot == OperandType.InlineString) {
        int tok = BitConverter.ToInt32(code, i);
        var us = mod.MD.GetUserString(MetadataTokens.UserStringHandle(tok & 0xFFFFFF));
        BitConverter.TryWriteBytes(code.AsSpan(i, 4), MetadataTokens.GetToken(MB.GetOrAddUserString(us)));
        Stats_Tokens++;
      }
      i += sz;
    }
    var locals = body.LocalSignature.IsNil ? default : (StandaloneSignatureHandle)MapSig(mod, body.LocalSignature);
    var mb = enc.AddMethodBody(code.Length, body.MaxStack, 0, true, locals,
        body.LocalVariablesInitialized ? MethodBodyAttributes.InitLocals : MethodBodyAttributes.None, localloc);
    new BlobWriter(mb.Instructions).WriteBytes(code);
    return mb.Offset;
  }

  // ---- Signature blobs (ECMA-335 II.23.2): copy, rewriting every TypeDefOrRefOrSpec.
  static BlobHandle RewriteSig(Mod mod, BlobHandle h) {
    var r = mod.MD.GetBlobReader(h);
    var w = new BlobBuilder();
    byte head = r.ReadByte(); w.WriteByte(head);
    switch (head & 0x0F) {
      case 0x06: Type(mod, ref r, w); break;                                     // FIELD
      case 0x07: { int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n); for (int k = 0; k < n; k++) Type(mod, ref r, w); break; }   // LOCAL_SIG
      default: MethodTail(mod, head, ref r, w); break;                           // method / calli signatures
    }
    return MB.GetOrAddBlob(w);
  }

  static void MethodTail(Mod mod, byte head, ref BlobReader r, BlobBuilder w) {
    if ((head & 0x10) != 0) w.WriteCompressedInteger(r.ReadCompressedInteger());   // generic param count
    int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
    Type(mod, ref r, w);                                                            // return type
    for (int k = 0; k < n; k++) Type(mod, ref r, w);
  }

  static void Type(Mod mod, ref BlobReader r, BlobBuilder w) {
    byte b = r.ReadByte(); w.WriteByte(b);
    switch (b) {
      case 0x0F: case 0x10: case 0x1D: case 0x45: Type(mod, ref r, w); break;        // PTR BYREF SZARRAY PINNED
      case 0x41: Type(mod, ref r, w); break;                                          // SENTINEL then type
      case 0x11: case 0x12: TypeRefOperand(mod, ref r, w); break;                     // VALUETYPE CLASS
      case 0x1F: case 0x20: TypeRefOperand(mod, ref r, w); Type(mod, ref r, w); break; // CMOD_REQD/OPT
      case 0x13: case 0x1E: w.WriteCompressedInteger(r.ReadCompressedInteger()); break;  // VAR MVAR
      case 0x1B: { byte h2 = r.ReadByte(); w.WriteByte(h2); MethodTail(mod, h2, ref r, w); break; }   // FNPTR
      case 0x14: {                                                                    // ARRAY
        Type(mod, ref r, w);
        w.WriteCompressedInteger(r.ReadCompressedInteger());
        int ns = r.ReadCompressedInteger(); w.WriteCompressedInteger(ns);
        for (int k = 0; k < ns; k++) w.WriteCompressedInteger(r.ReadCompressedInteger());
        int nl = r.ReadCompressedInteger(); w.WriteCompressedInteger(nl);
        for (int k = 0; k < nl; k++) w.WriteCompressedSignedInteger(r.ReadCompressedSignedInteger());
        break;
      }
      case 0x15: {                                                                    // GENERICINST
        w.WriteByte(r.ReadByte()); TypeRefOperand(mod, ref r, w);
        int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
        for (int k = 0; k < n; k++) Type(mod, ref r, w);
        break;
      }
      default: break;                                                                 // primitive
    }
  }

  static void TypeRefOperand(Mod mod, ref BlobReader r, BlobBuilder w) {
    var th = r.ReadTypeHandle();
    w.WriteCompressedInteger(CodedIndex.TypeDefOrRefOrSpec(MapType(mod, th)));
    Stats_SigTypeRefs++;
  }
}
