// mdcanon.cs - tests-p2 recon prototype: a canonical, layout-independent listing
// of an IL-only assembly, so that two writers (ilasm vs a direct PE writer, or
// Framework ilasm vs .NET ilasm) can be compared semantically.
//
//   dotnet exec --runtimeconfig mdcanon.runtimeconfig.json mdcanon.dll [options] file.dll
//     --scopes=keep|strip   print [assembly] resolution scopes (default keep)
//     --order=sorted|decl   order of types and members (default sorted)
//     --forms=exact|canon   canon: short/long branch and ldc/ldarg/ldloc forms print alike
//     --stats               also print table row counts and heap sizes (layout facts)
//
// Everything that depends only on physical layout is left out or normalized:
// metadata tokens (operands print the entity they name), heap offsets, RVAs
// (RVA fields print their bytes), branch offsets (targets print as instruction
// labels), MVID, timestamps and image base. Uses only System.Reflection.Metadata
// from the shared framework.
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
using System.Text;

static class MdCanon
{
    static MetadataReader md;
    static PEReader pe;
    static bool stripScopes, sorted = true, canonForms, stats;
    static readonly Dictionary<ushort, OpCode> ops = new();

    static int Main(string[] args)
    {
        string file = null;
        foreach (var a in args)
        {
            if (a == "--scopes=strip") stripScopes = true;
            else if (a == "--scopes=keep") stripScopes = false;
            else if (a == "--order=decl") sorted = false;
            else if (a == "--order=sorted") sorted = true;
            else if (a == "--forms=canon") canonForms = true;
            else if (a == "--forms=exact") canonForms = false;
            else if (a == "--stats") stats = true;
            else if (a.StartsWith("-")) { Console.Error.WriteLine("mdcanon: unknown option " + a); return 2; }
            else file = a;
        }
        if (file == null) { Console.Error.WriteLine("usage: mdcanon [options] file.dll"); return 2; }
        foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
        {
            var op = (OpCode)f.GetValue(null);
            ops[(ushort)op.Value] = op;
        }
        using var stream = File.OpenRead(file);
        pe = new PEReader(stream);
        if (!pe.HasMetadata) { Console.Error.WriteLine("mdcanon: no CLI metadata"); return 1; }
        md = pe.GetMetadataReader();
        var o = new StringBuilder();
        Header(o);
        foreach (var t in Order(md.TypeDefinitions.Select(h => (h, TypeDefName(h)))))
            TypeDef(o, t);
        CustomAttributes(o, EntityHandle.AssemblyDefinition, "assembly");
        CustomAttributes(o, EntityHandle.ModuleDefinition, "module");
        if (stats) Stats(o);
        var text = o.ToString().Replace("\r\n", "\n");
        var outp = Console.OpenStandardOutput();
        var bytes = new UTF8Encoding(false).GetBytes(text);
        outp.Write(bytes, 0, bytes.Length);
        return 0;
    }

    static IEnumerable<T> Order<T>(IEnumerable<(T h, string key)> items) =>
        sorted ? items.OrderBy(x => x.key, StringComparer.Ordinal).Select(x => x.h) : items.Select(x => x.h);

    // ---------------------------------------------------------------- header
    static void Header(StringBuilder o)
    {
        var h = pe.PEHeaders;
        o.Append("pe ").Append(h.PEHeader.Magic).Append(' ').Append(h.CoffHeader.Machine)
         .Append(h.IsDll ? " dll" : " exe").Append(" subsystem=").Append(h.PEHeader.Subsystem).Append('\n');
        var cor = h.CorHeader;
        o.Append("cor flags=").Append(cor.Flags).Append(" runtime=").Append(cor.MajorRuntimeVersion).Append('.')
         .Append(cor.MinorRuntimeVersion).Append(" metadata-version=").Append(md.MetadataVersion).Append('\n');
        if (cor.EntryPointTokenOrRelativeVirtualAddress != 0)
        {
            var ep = MetadataTokens.EntityHandle(cor.EntryPointTokenOrRelativeVirtualAddress);
            o.Append("entrypoint ").Append(MethodRefText(ep)).Append('\n');
        }
        var asm = md.GetAssemblyDefinition();
        o.Append("assembly '").Append(md.GetString(asm.Name)).Append("' ver ").Append(asm.Version)
         .Append(" flags=").Append(asm.Flags).Append(" hash=").Append(asm.HashAlgorithm)
         .Append(" culture='").Append(md.GetString(asm.Culture)).Append("' key=").Append(Hex(md.GetBlobBytes(asm.PublicKey))).Append('\n');
        o.Append("module '").Append(md.GetString(md.GetModuleDefinition().Name)).Append("'\n");
        foreach (var r in Order(md.AssemblyReferences.Select(x => (x, md.GetString(md.GetAssemblyReference(x).Name)))))
        {
            var a = md.GetAssemblyReference(r);
            o.Append("assemblyref '").Append(md.GetString(a.Name)).Append("' ver ").Append(a.Version)
             .Append(" flags=").Append(a.Flags).Append(" key=").Append(Hex(md.GetBlobBytes(a.PublicKeyOrToken)))
             .Append(" culture='").Append(md.GetString(a.Culture)).Append("'\n");
        }
        foreach (var r in Order(md.ModuleReferences().Select(x => (x, md.GetString(md.GetModuleReference(x).Name)))))
            o.Append("moduleref '").Append(md.GetString(md.GetModuleReference(r).Name)).Append("'\n");
        foreach (var f in md.AssemblyFiles) o.Append("file '").Append(md.GetString(md.GetAssemblyFile(f).Name)).Append("'\n");
        foreach (var e in md.ExportedTypes)
        {
            var x = md.GetExportedType(e);
            o.Append("exportedtype ").Append(md.GetString(x.Namespace)).Append('.').Append(md.GetString(x.Name)).Append('\n');
        }
        foreach (var r in md.ManifestResources) o.Append("resource '").Append(md.GetString(md.GetManifestResource(r).Name)).Append("'\n");
    }

    // ---------------------------------------------------------------- types
    static string TypeDefName(TypeDefinitionHandle h)
    {
        var t = md.GetTypeDefinition(h);
        var name = md.GetString(t.Name);
        var ns = md.GetString(t.Namespace);
        var full = ns.Length > 0 ? ns + "." + name : name;
        var outer = t.GetDeclaringType();
        return outer.IsNil ? full : TypeDefName(outer) + "/" + full;
    }

    static void TypeDef(StringBuilder o, TypeDefinitionHandle h)
    {
        var t = md.GetTypeDefinition(h);
        o.Append("\n.class '").Append(TypeDefName(h)).Append("' ").Append(t.Attributes);
        if (!t.BaseType.IsNil) o.Append(" extends ").Append(TypeText(t.BaseType));
        o.Append('\n');
        var layout = t.GetLayout();
        if (!layout.IsDefault) o.Append("  .layout pack ").Append(layout.PackingSize).Append(" size ").Append(layout.Size).Append('\n');
        foreach (var i in t.GetInterfaceImplementations()) o.Append("  .implements ").Append(TypeText(md.GetInterfaceImplementation(i).Interface)).Append('\n');
        CustomAttributes(o, h, "  ");
        foreach (var f in Order(t.GetFields().Select(x => (x, md.GetString(md.GetFieldDefinition(x).Name) + " " + FieldSig(md.GetFieldDefinition(x).Signature)))))
            Field(o, f);
        foreach (var m in Order(t.GetMethods().Select(x => (x, md.GetString(md.GetMethodDefinition(x).Name) + " " + MethodSigText(md.GetMethodDefinition(x).Signature)))))
            Method(o, m);
    }

    static void Field(StringBuilder o, FieldDefinitionHandle h)
    {
        var f = md.GetFieldDefinition(h);
        o.Append("  .field '").Append(md.GetString(f.Name)).Append("' ").Append(FieldSig(f.Signature)).Append(' ').Append(f.Attributes);
        if (f.GetOffset() >= 0) o.Append(" offset ").Append(f.GetOffset());
        if (!f.GetDefaultValue().IsNil) o.Append(" const ").Append(Hex(md.GetBlobBytes(md.GetConstant(f.GetDefaultValue()).Value)));
        if (!f.GetMarshallingDescriptor().IsNil) o.Append(" marshal ").Append(Hex(md.GetBlobBytes(f.GetMarshallingDescriptor())));
        o.Append('\n');
        CustomAttributes(o, h, "    ");
        int rva = f.GetRelativeVirtualAddress();
        if (rva != 0)
        {
            int size = FieldDataSize(f);
            var data = pe.GetSectionData(rva);
            o.Append("    .data ").Append(size).Append(" bytes");
            if (size < 0 || data.Length < size) { o.Append(" (unknown size)\n"); return; }
            o.Append('\n');
            var bytes = data.GetContent(0, size);
            for (int i = 0; i < size; i += 32)
                o.Append("      ").Append(Hex(bytes.Skip(i).Take(32).ToArray())).Append('\n');
        }
    }

    static int FieldDataSize(FieldDefinition f)
    {
        var blob = md.GetBlobReader(f.Signature);
        if (blob.ReadSignatureHeader().Kind != SignatureKind.Field) return -1;
        var code = blob.ReadSignatureTypeCode();
        switch (code)
        {
            case SignatureTypeCode.Boolean: case SignatureTypeCode.SByte: case SignatureTypeCode.Byte: return 1;
            case SignatureTypeCode.Char: case SignatureTypeCode.Int16: case SignatureTypeCode.UInt16: return 2;
            case SignatureTypeCode.Int32: case SignatureTypeCode.UInt32: case SignatureTypeCode.Single: return 4;
            case SignatureTypeCode.Int64: case SignatureTypeCode.UInt64: case SignatureTypeCode.Double: return 8;
            case SignatureTypeCode.TypeHandle:
                var th = blob.ReadTypeHandle();
                if (th.Kind == HandleKind.TypeDefinition)
                {
                    var l = md.GetTypeDefinition((TypeDefinitionHandle)th).GetLayout();
                    return l.IsDefault ? -1 : l.Size;
                }
                return -1;
            default: return -1;
        }
    }

    static void Method(StringBuilder o, MethodDefinitionHandle h)
    {
        var m = md.GetMethodDefinition(h);
        o.Append("  .method '").Append(md.GetString(m.Name)).Append("' ").Append(MethodSigText(m.Signature))
         .Append(' ').Append(m.Attributes).Append(" impl ").Append(m.ImplAttributes).Append('\n');
        var imp = m.GetImport();
        if (!imp.Module.IsNil)
            o.Append("    .pinvokeimpl '").Append(md.GetString(md.GetModuleReference(imp.Module).Name)).Append("' entry '")
             .Append(md.GetString(imp.Name)).Append("' ").Append(imp.Attributes).Append('\n');
        foreach (var p in m.GetParameters())
        {
            var par = md.GetParameter(p);
            o.Append("    .param ").Append(par.SequenceNumber).Append(" '").Append(md.GetString(par.Name)).Append("' ").Append(par.Attributes);
            if (!par.GetMarshallingDescriptor().IsNil) o.Append(" marshal ").Append(Hex(md.GetBlobBytes(par.GetMarshallingDescriptor())));
            if (!par.GetDefaultValue().IsNil) o.Append(" const ").Append(Hex(md.GetBlobBytes(md.GetConstant(par.GetDefaultValue()).Value)));
            o.Append('\n');
        }
        CustomAttributes(o, h, "    ");
        if (m.RelativeVirtualAddress == 0) return;
        var body = pe.GetMethodBody(m.RelativeVirtualAddress);
        o.Append("    .maxstack ").Append(body.MaxStack).Append(body.LocalVariablesInitialized ? " initlocals" : "").Append('\n');
        if (!body.LocalSignature.IsNil)
        {
            var sig = md.GetStandaloneSignature(body.LocalSignature);
            var locals = sig.DecodeLocalSignature(Prov.I, null);
            o.Append("    .locals (").Append(string.Join(", ", locals)).Append(")\n");
        }
        Body(o, body);
    }

    // ---------------------------------------------------------------- IL
    struct Inst { public int Offset; public OpCode Op; public string Operand; public int[] Targets; }

    static void Body(StringBuilder o, MethodBodyBlock body)
    {
        var il = body.GetILBytes();
        var insts = new List<Inst>();
        int pc = 0;
        while (pc < il.Length)
        {
            int start = pc;
            ushort v = il[pc++];
            if (v == 0xFE) v = (ushort)(0xFE00 | il[pc++]);
            if (!ops.TryGetValue(v, out var op)) { insts.Add(new Inst { Offset = start, Operand = "<bad opcode " + v.ToString("X") + ">" }); break; }
            var inst = new Inst { Offset = start, Op = op };
            switch (op.OperandType)
            {
                case OperandType.InlineNone: break;
                case OperandType.ShortInlineBrTarget: { int d = (sbyte)il[pc]; pc += 1; inst.Targets = new[] { pc + d }; break; }
                case OperandType.InlineBrTarget: { int d = BitConverter.ToInt32(il, pc); pc += 4; inst.Targets = new[] { pc + d }; break; }
                case OperandType.ShortInlineI: inst.Operand = (op.Value == OpCodes.Ldc_I4_S.Value ? (int)(sbyte)il[pc] : (int)il[pc]).ToString(); pc += 1; break;
                case OperandType.ShortInlineVar: inst.Operand = il[pc].ToString(); pc += 1; break;
                case OperandType.InlineVar: inst.Operand = BitConverter.ToUInt16(il, pc).ToString(); pc += 2; break;
                case OperandType.InlineI: inst.Operand = BitConverter.ToInt32(il, pc).ToString(); pc += 4; break;
                case OperandType.InlineI8: inst.Operand = BitConverter.ToInt64(il, pc).ToString(); pc += 8; break;
                case OperandType.ShortInlineR: inst.Operand = "float32(0x" + BitConverter.ToUInt32(il, pc).ToString("X8") + ")"; pc += 4; break;
                case OperandType.InlineR: inst.Operand = "float64(0x" + BitConverter.ToUInt64(il, pc).ToString("X16") + ")"; pc += 8; break;
                case OperandType.InlineSwitch:
                {
                    int n = BitConverter.ToInt32(il, pc); pc += 4;
                    int bas = pc + 4 * n;
                    inst.Targets = new int[n];
                    for (int i = 0; i < n; i++) inst.Targets[i] = bas + BitConverter.ToInt32(il, pc + 4 * i);
                    pc = bas;
                    break;
                }
                case OperandType.InlineString:
                {
                    int tok = BitConverter.ToInt32(il, pc); pc += 4;
                    inst.Operand = Quote(md.GetUserString((UserStringHandle)MetadataTokens.Handle(tok)));
                    break;
                }
                case OperandType.InlineSig:
                {
                    int tok = BitConverter.ToInt32(il, pc); pc += 4;
                    var sig = md.GetStandaloneSignature((StandaloneSignatureHandle)MetadataTokens.EntityHandle(tok));
                    inst.Operand = MethodSigText(sig.Signature);
                    break;
                }
                case OperandType.InlineMethod: case OperandType.InlineField: case OperandType.InlineType: case OperandType.InlineTok:
                {
                    int tok = BitConverter.ToInt32(il, pc); pc += 4;
                    inst.Operand = EntityText(MetadataTokens.EntityHandle(tok));
                    break;
                }
                default: inst.Operand = "<operand " + op.OperandType + ">"; break;
            }
            insts.Add(inst);
        }
        var index = new Dictionary<int, int>();
        for (int i = 0; i < insts.Count; i++) index[insts[i].Offset] = i;
        index[il.Length] = insts.Count;
        string Label(int off) => index.TryGetValue(off, out var i) ? "L" + i : "<offset 0x" + off.ToString("X") + ">";
        for (int i = 0; i < insts.Count; i++)
        {
            var x = insts[i];
            o.Append("    L").Append(i).Append(": ").Append(OpName(x));
            if (x.Targets != null) o.Append(' ').Append(string.Join(", ", x.Targets.Select(Label)));
            else if (x.Operand != null) o.Append(' ').Append(x.Operand);
            o.Append('\n');
        }
        foreach (var r in body.ExceptionRegions)
        {
            o.Append("    .try ").Append(Label(r.TryOffset)).Append(" to ").Append(Label(r.TryOffset + r.TryLength))
             .Append(' ').Append(r.Kind);
            if (r.Kind == ExceptionRegionKind.Filter) o.Append(' ').Append(Label(r.FilterOffset));
            if (r.Kind == ExceptionRegionKind.Catch) o.Append(' ').Append(TypeText(r.CatchType));
            o.Append(" handler ").Append(Label(r.HandlerOffset)).Append(" to ").Append(Label(r.HandlerOffset + r.HandlerLength)).Append('\n');
        }
    }

    static string OpName(Inst x)
    {
        if (x.Op.Name == null) return "<bad>";
        var n = x.Op.Name;
        if (!canonForms) return n;
        // --forms=canon: encodings that differ only in operand width print alike.
        if (x.Op.OperandType == OperandType.ShortInlineBrTarget && n.EndsWith(".s")) return n.Substring(0, n.Length - 2);
        return n switch
        {
            "ldarg.s" => "ldarg", "ldarga.s" => "ldarga", "starg.s" => "starg",
            "ldloc.s" => "ldloc", "ldloca.s" => "ldloca", "stloc.s" => "stloc", "ldc.i4.s" => "ldc.i4",
            _ => n,
        };
    }

    // ---------------------------------------------------------------- entities
    static string EntityText(EntityHandle h)
    {
        switch (h.Kind)
        {
            case HandleKind.MethodDefinition: case HandleKind.MemberReference: case HandleKind.MethodSpecification:
                return MethodRefText(h);
            case HandleKind.FieldDefinition:
            {
                var f = md.GetFieldDefinition((FieldDefinitionHandle)h);
                return FieldSig(f.Signature) + " '" + TypeDefName(f.GetDeclaringType()) + "'::'" + md.GetString(f.Name) + "'";
            }
            default: return TypeText(h);
        }
    }

    static string MethodRefText(EntityHandle h)
    {
        switch (h.Kind)
        {
            case HandleKind.MethodDefinition:
            {
                var m = md.GetMethodDefinition((MethodDefinitionHandle)h);
                return MethodSigText(m.Signature) + " '" + TypeDefName(m.GetDeclaringType()) + "'::'" + md.GetString(m.Name) + "'";
            }
            case HandleKind.MemberReference:
            {
                var r = md.GetMemberReference((MemberReferenceHandle)h);
                string parent = r.Parent.Kind switch
                {
                    HandleKind.ModuleReference => "[.module " + md.GetString(md.GetModuleReference((ModuleReferenceHandle)r.Parent).Name) + "]",
                    HandleKind.MethodDefinition => MethodRefText(r.Parent),
                    _ => TypeText(r.Parent),
                };
                var sig = r.GetKind() == MemberReferenceKind.Field ? FieldSig(r.Signature) : MethodSigText(r.Signature);
                return sig + " " + parent + "::'" + md.GetString(r.Name) + "'";
            }
            case HandleKind.MethodSpecification:
            {
                var s = md.GetMethodSpecification((MethodSpecificationHandle)h);
                return MethodRefText(s.Method) + "<" + string.Join(", ", s.DecodeSignature(Prov.I, null)) + ">";
            }
            default: return "<" + h.Kind + ">";
        }
    }

    static string TypeText(EntityHandle h)
    {
        switch (h.Kind)
        {
            case HandleKind.TypeDefinition: return "'" + TypeDefName((TypeDefinitionHandle)h) + "'";
            case HandleKind.TypeReference: return Prov.I.GetTypeFromReference(md, (TypeReferenceHandle)h, 0);
            case HandleKind.TypeSpecification: return md.GetTypeSpecification((TypeSpecificationHandle)h).DecodeSignature(Prov.I, null);
            default: return "<" + h.Kind + ">";
        }
    }

    static string FieldSig(BlobHandle b)
    {
        var reader = md.GetBlobReader(b);
        return new SignatureDecoder<string, object>(Prov.I, md, null).DecodeFieldSignature(ref reader);
    }

    static string MethodSigText(BlobHandle b)
    {
        var reader = md.GetBlobReader(b);
        var sig = new SignatureDecoder<string, object>(Prov.I, md, null).DecodeMethodSignature(ref reader);
        return SigText(sig);
    }

    internal static string SigText(MethodSignature<string> sig)
    {
        var sb = new StringBuilder();
        var h = sig.Header;
        if (h.IsInstance) sb.Append("instance ");
        if (h.HasExplicitThis) sb.Append("explicit ");
        sb.Append(h.CallingConvention switch
        {
            SignatureCallingConvention.Default => "",
            SignatureCallingConvention.CDecl => "unmanaged cdecl ",
            SignatureCallingConvention.StdCall => "unmanaged stdcall ",
            SignatureCallingConvention.ThisCall => "unmanaged thiscall ",
            SignatureCallingConvention.FastCall => "unmanaged fastcall ",
            SignatureCallingConvention.VarArgs => "vararg ",
            SignatureCallingConvention.Unmanaged => "unmanaged ",
            _ => h.CallingConvention + " ",
        });
        if (sig.GenericParameterCount > 0) sb.Append('<').Append(sig.GenericParameterCount).Append("> ");
        sb.Append(sig.ReturnType).Append('(');
        for (int i = 0; i < sig.ParameterTypes.Length; i++)
        {
            if (i > 0) sb.Append(", ");
            if (i == sig.RequiredParameterCount) sb.Append("..., ");
            sb.Append(sig.ParameterTypes[i]);
        }
        return sb.Append(')').ToString();
    }

    // ---------------------------------------------------------------- attributes
    static void CustomAttributes(StringBuilder o, EntityHandle parent, string indent)
    {
        var list = md.GetCustomAttributes(parent).Select(a =>
        {
            var ca = md.GetCustomAttribute(a);
            return ".custom " + MethodRefText(ca.Constructor) + " = " + Hex(md.GetBlobBytes(ca.Value));
        }).ToList();
        if (sorted) list.Sort(StringComparer.Ordinal);
        foreach (var s in list) o.Append(indent == "assembly" || indent == "module" ? indent + " " : indent).Append(s).Append('\n');
    }

    // ---------------------------------------------------------------- --stats (layout facts, not compared)
    static void Stats(StringBuilder o)
    {
        o.Append("\n# stats (layout facts; differ between writers by design)\n");
        foreach (TableIndex t in Enum.GetValues(typeof(TableIndex)))
        {
            int n = md.GetTableRowCount(t);
            if (n > 0) o.Append("# table ").Append(t).Append(' ').Append(n).Append('\n');
        }
        foreach (HeapIndex hp in Enum.GetValues(typeof(HeapIndex)))
            o.Append("# heap ").Append(hp).Append(' ').Append(md.GetHeapSize(hp)).Append('\n');
        o.Append("# image ").Append(pe.PEHeaders.PEHeader.SizeOfImage).Append(" sections ")
         .Append(string.Join(",", pe.PEHeaders.SectionHeaders.Select(s => s.Name))).Append('\n');
    }

    static IEnumerable<ModuleReferenceHandle> ModuleReferences(this MetadataReader r)
    {
        for (int i = 1; i <= r.GetTableRowCount(TableIndex.ModuleRef); i++)
            yield return MetadataTokens.ModuleReferenceHandle(i);
    }

    static string Hex(byte[] b) => b.Length == 0 ? "-" : BitConverter.ToString(b).Replace("-", " ");

    static string Quote(string s)
    {
        var sb = new StringBuilder("\"");
        foreach (var c in s)
        {
            if (c == '"' || c == '\\') sb.Append('\\').Append(c);
            else if (c < 0x20 || c > 0x7E) sb.Append("\\u").Append(((int)c).ToString("X4"));
            else sb.Append(c);
        }
        return sb.Append('"').ToString();
    }

    // ---------------------------------------------------------------- signature provider
    sealed class Prov : ISignatureTypeProvider<string, object>
    {
        public static readonly Prov I = new();
        public string GetPrimitiveType(PrimitiveTypeCode c) => c switch
        {
            PrimitiveTypeCode.Void => "void", PrimitiveTypeCode.Boolean => "bool", PrimitiveTypeCode.Char => "char",
            PrimitiveTypeCode.SByte => "int8", PrimitiveTypeCode.Byte => "uint8", PrimitiveTypeCode.Int16 => "int16",
            PrimitiveTypeCode.UInt16 => "uint16", PrimitiveTypeCode.Int32 => "int32", PrimitiveTypeCode.UInt32 => "uint32",
            PrimitiveTypeCode.Int64 => "int64", PrimitiveTypeCode.UInt64 => "uint64", PrimitiveTypeCode.Single => "float32",
            PrimitiveTypeCode.Double => "float64", PrimitiveTypeCode.IntPtr => "native int", PrimitiveTypeCode.UIntPtr => "native uint",
            PrimitiveTypeCode.String => "string", PrimitiveTypeCode.Object => "object", PrimitiveTypeCode.TypedReference => "typedref",
            _ => c.ToString(),
        };
        static string Kind(byte k) => k == (byte)SignatureTypeKind.ValueType ? "valuetype " : k == (byte)SignatureTypeKind.Class ? "class " : "";
        public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k) => Kind(k) + "'" + TypeDefName(h) + "'";
        public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k)
        {
            var t = r.GetTypeReference(h);
            var ns = r.GetString(t.Namespace);
            var name = (ns.Length > 0 ? ns + "." : "") + r.GetString(t.Name);
            string scope = t.ResolutionScope.Kind switch
            {
                HandleKind.AssemblyReference => stripScopes ? "[*]" : "[" + r.GetString(r.GetAssemblyReference((AssemblyReferenceHandle)t.ResolutionScope).Name) + "]",
                HandleKind.ModuleReference => "[.module " + r.GetString(r.GetModuleReference((ModuleReferenceHandle)t.ResolutionScope).Name) + "]",
                HandleKind.TypeReference => GetTypeFromReference(r, (TypeReferenceHandle)t.ResolutionScope, 0) + "/",
                HandleKind.ModuleDefinition => "[.module self]",
                _ => "",
            };
            return Kind(k) + scope + name;
        }
        public string GetTypeFromSpecification(MetadataReader r, object g, TypeSpecificationHandle h, byte k) =>
            r.GetTypeSpecification(h).DecodeSignature(this, g);
        public string GetSZArrayType(string e) => e + "[]";
        public string GetArrayType(string e, ArrayShape s) => e + "[" + new string(',', s.Rank - 1) + "]";
        public string GetPointerType(string e) => e + "*";
        public string GetByReferenceType(string e) => e + "&";
        public string GetPinnedType(string e) => e + " pinned";
        public string GetGenericInstantiation(string g, ImmutableArray<string> a) => g + "<" + string.Join(", ", a) + ">";
        public string GetGenericTypeParameter(object g, int i) => "!" + i;
        public string GetGenericMethodParameter(object g, int i) => "!!" + i;
        public string GetFunctionPointerType(MethodSignature<string> s) => "method " + SigText(s);
        public string GetModifiedType(string m, string u, bool req) => u + (req ? " modreq(" : " modopt(") + m + ")";
    }
}
