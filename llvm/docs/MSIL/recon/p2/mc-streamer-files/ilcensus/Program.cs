// ilcensus: decode the IL method bodies of assemblies built by ilasm from the
// MSIL backend's -S output, and report what a CIL MC code emitter and object
// writer must produce: opcodes and their encodings, token operand kinds per
// opcode, method header kinds, branch/switch offset semantics, and metadata
// table row counts.
//
// Usage: ilcensus <dll>...            census over all inputs
//        ilcensus --opcodes            print the ECMA-335 opcode table
//        ilcensus --dump <dll>         one line per instruction, tokens resolved
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class P
{
    static readonly Dictionary<ushort, OpCode> Ops = new();

    static void LoadOps()
    {
        foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
        {
            var op = (OpCode)f.GetValue(null);
            Ops[(ushort)op.Value] = op;
        }
    }

    static int OperandSize(OpCode op, BlobReader r)
    {
        switch (op.OperandType)
        {
            case OperandType.InlineNone: return 0;
            case OperandType.ShortInlineBrTarget:
            case OperandType.ShortInlineI:
            case OperandType.ShortInlineVar: return 1;
            case OperandType.InlineVar: return 2;
            case OperandType.InlineI8:
            case OperandType.InlineR: return 8;
            case OperandType.InlineSwitch: return -1;
            default: return 4;
        }
    }

    static string TableName(int token) => (token >> 24) switch
    {
        0x01 => "TypeRef", 0x02 => "TypeDef", 0x04 => "FieldDef", 0x06 => "MethodDef",
        0x0A => "MemberRef", 0x11 => "StandAloneSig", 0x1B => "TypeSpec", 0x2B => "MethodSpec",
        0x70 => "UserString", _ => "table" + (token >> 24).ToString("X2")
    };

    static string Describe(MetadataReader md, int token)
    {
        var h = MetadataTokens.EntityHandle(token);
        switch (h.Kind)
        {
            case HandleKind.MethodDefinition:
            {
                var m = md.GetMethodDefinition((MethodDefinitionHandle)h);
                bool pinv = (m.Attributes & MethodAttributes.PinvokeImpl) != 0;
                return "MethodDef " + md.GetString(m.Name) + (pinv ? " [pinvokeimpl]" : m.RelativeVirtualAddress == 0 ? " [no body]" : "");
            }
            case HandleKind.FieldDefinition:
            {
                var f = md.GetFieldDefinition((FieldDefinitionHandle)h);
                return "FieldDef " + md.GetString(f.Name) + (f.GetRelativeVirtualAddress() != 0 ? " [RVA]" : "");
            }
            case HandleKind.MemberReference:
            {
                var mr = md.GetMemberReference((MemberReferenceHandle)h);
                string parent = "?";
                if (mr.Parent.Kind == HandleKind.TypeReference)
                {
                    var tr = md.GetTypeReference((TypeReferenceHandle)mr.Parent);
                    string scope = "";
                    if (tr.ResolutionScope.Kind == HandleKind.AssemblyReference)
                        scope = "[" + md.GetString(md.GetAssemblyReference((AssemblyReferenceHandle)tr.ResolutionScope).Name) + "]";
                    parent = scope + md.GetString(tr.Namespace) + "." + md.GetString(tr.Name);
                }
                return "MemberRef " + parent + "::" + md.GetString(mr.Name);
            }
            case HandleKind.StandaloneSignature: return "StandAloneSig";
            case HandleKind.TypeReference:
            {
                var tr = md.GetTypeReference((TypeReferenceHandle)h);
                return "TypeRef " + md.GetString(tr.Namespace) + "." + md.GetString(tr.Name);
            }
            case HandleKind.TypeDefinition:
            {
                var td = md.GetTypeDefinition((TypeDefinitionHandle)h);
                return "TypeDef " + md.GetString(td.Name);
            }
            default: return h.Kind.ToString();
        }
    }

    static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        LoadOps();
        if (args.Length == 1 && args[0] == "--opcodes")
        {
            foreach (var kv in Ops.OrderBy(k => k.Key))
                Console.WriteLine($"0x{kv.Key:X4}\t{kv.Value.Name}\t{kv.Value.OperandType}\t{kv.Value.Size}");
            return 0;
        }
        if (args.Length == 2 && args[0] == "--bodies")
        {
            // One line per method with a body, in MethodDef order:
            // name <TAB> maxstack <TAB> code hex <TAB> off=desc|off=desc...
            using var fs0 = File.OpenRead(args[1]);
            using var pe0 = new PEReader(fs0);
            var md0 = pe0.GetMetadataReader();
            foreach (var mh in md0.MethodDefinitions)
            {
                var m = md0.GetMethodDefinition(mh);
                if (m.RelativeVirtualAddress == 0) continue;
                var body = pe0.GetMethodBody(m.RelativeVirtualAddress);
                var bytes = body.GetILBytes();
                var il = body.GetILReader();
                var toks = new List<string>();
                while (il.RemainingBytes > 0)
                {
                    ushort v = il.ReadByte();
                    if (v == 0xFE) v = (ushort)(0xFE00 | il.ReadByte());
                    var op = Ops[v];
                    switch (op.OperandType)
                    {
                        case OperandType.InlineMethod: case OperandType.InlineField: case OperandType.InlineSig:
                        case OperandType.InlineTok: case OperandType.InlineType: case OperandType.InlineString:
                        { int at = il.Offset; int tok = il.ReadInt32(); toks.Add(at + "=" + Describe(md0, tok)); break; }
                        case OperandType.InlineSwitch:
                        { uint n = il.ReadUInt32(); for (int i = 0; i < n; i++) il.ReadInt32(); break; }
                        default:
                        { int sz = OperandSize(op, il); for (int i = 0; i < sz; i++) il.ReadByte(); break; }
                    }
                }
                Console.WriteLine(md0.GetString(m.Name) + "\t" + body.MaxStack + "\t" + Convert.ToHexString(bytes) + "\t" + string.Join("|", toks));
            }
            return 0;
        }
        bool dump = args.Length == 2 && args[0] == "--dump";
        var files = dump ? new[] { args[1] } : args;

        var opCount = new SortedDictionary<string, long>();
        var tokKinds = new SortedDictionary<string, long>();
        var memberRefs = new SortedDictionary<string, long>();
        var tables = new SortedDictionary<string, long>();
        long methods = 0, tiny = 0, fat = 0, fatNoLocals = 0, codeBytes = 0, tokenOperands = 0, branchOperands = 0, switchTargets = 0;
        long maxCode = 0, maxStackMax = 0, withEH = 0, badTargets = 0, headerBytes = 0;
        foreach (var file in files)
        {
            using var fs = File.OpenRead(file);
            using var pe = new PEReader(fs);
            var md = pe.GetMetadataReader();
            foreach (TableIndex t in Enum.GetValues(typeof(TableIndex)))
            {
                int n = md.GetTableRowCount(t);
                if (n > 0) { tables.TryGetValue(t.ToString(), out var c); tables[t.ToString()] = c + n; }
            }
            foreach (var mh in md.MethodDefinitions)
            {
                var m = md.GetMethodDefinition(mh);
                if (m.RelativeVirtualAddress == 0) continue;
                methods++;
                var body = pe.GetMethodBody(m.RelativeVirtualAddress);
                var raw = pe.GetSectionData(m.RelativeVirtualAddress).GetReader();
                byte b0 = raw.ReadByte();
                bool isTiny = (b0 & 3) == 2;
                if (isTiny) { tiny++; headerBytes += 1; } else { fat++; headerBytes += 12; if (body.LocalSignature.IsNil) fatNoLocals++; }
                if (body.ExceptionRegions.Length > 0) withEH++;
                var il = body.GetILReader();
                int size = il.Length;
                codeBytes += size; maxCode = Math.Max(maxCode, size); maxStackMax = Math.Max(maxStackMax, body.MaxStack);
                var starts = new HashSet<int>();
                var targets = new List<int>();
                if (dump) Console.WriteLine($"method {md.GetString(m.Name)} tiny={isTiny} maxstack={body.MaxStack} code={size}");
                while (il.RemainingBytes > 0)
                {
                    int off = il.Offset;
                    starts.Add(off);
                    ushort v = il.ReadByte();
                    if (v == 0xFE) v = (ushort)(0xFE00 | il.ReadByte());
                    var op = Ops[v];
                    string name = op.Name;
                    var sb = new StringBuilder();
                    switch (op.OperandType)
                    {
                        case OperandType.InlineMethod:
                        case OperandType.InlineField:
                        case OperandType.InlineSig:
                        case OperandType.InlineTok:
                        case OperandType.InlineType:
                        case OperandType.InlineString:
                        {
                            int tok = il.ReadInt32();
                            tokenOperands++;
                            string k = name + " -> " + TableName(tok);
                            tokKinds.TryGetValue(k, out var c); tokKinds[k] = c + 1;
                            string d = Describe(md, tok);
                            if (d.StartsWith("MemberRef")) { memberRefs.TryGetValue(name + " " + d, out var c2); memberRefs[name + " " + d] = c2 + 1; }
                            sb.Append(d);
                            break;
                        }
                        case OperandType.InlineBrTarget:
                        {
                            int rel = il.ReadInt32(); branchOperands++;
                            targets.Add(il.Offset + rel); sb.Append("IL_" + (il.Offset + rel).ToString("x4"));
                            break;
                        }
                        case OperandType.ShortInlineBrTarget:
                        {
                            int rel = il.ReadSByte(); branchOperands++;
                            targets.Add(il.Offset + rel); sb.Append("IL_" + (il.Offset + rel).ToString("x4"));
                            break;
                        }
                        case OperandType.InlineSwitch:
                        {
                            uint n = il.ReadUInt32();
                            var rels = new int[n];
                            for (int i = 0; i < n; i++) rels[i] = il.ReadInt32();
                            int end = il.Offset; // targets are relative to the end of the whole instruction
                            foreach (var rel in rels) { targets.Add(end + rel); switchTargets++; }
                            sb.Append($"({n} targets, base = end of instruction IL_{end:x4})");
                            break;
                        }
                        default:
                        {
                            int sz = OperandSize(op, il);
                            for (int i = 0; i < sz; i++) il.ReadByte();
                            break;
                        }
                    }
                    string key = $"0x{v:X4} {name}";
                    opCount.TryGetValue(key, out var cnt); opCount[key] = cnt + 1;
                    if (dump) Console.WriteLine($"  IL_{off:x4}: {name} {sb}");
                }
                foreach (var t in targets) if (!starts.Contains(t)) badTargets++;
            }
        }
        if (dump) return 0;
        Console.WriteLine($"files {files.Length}, methods with bodies {methods}: tiny headers {tiny}, fat {fat} (fat without locals {fatNoLocals}), with EH {withEH}");
        Console.WriteLine($"IL code bytes {codeBytes}, header bytes {headerBytes}, largest body {maxCode}, largest maxstack {maxStackMax}");
        Console.WriteLine($"token operands {tokenOperands}, branch operands {branchOperands}, switch targets {switchTargets}, targets not on an instruction boundary {badTargets}");
        Console.WriteLine("\n== opcodes (encoding, name, count)");
        foreach (var kv in opCount) Console.WriteLine($"  {kv.Key}\t{kv.Value}");
        Console.WriteLine("\n== token operands by opcode and table");
        foreach (var kv in tokKinds) Console.WriteLine($"  {kv.Key}\t{kv.Value}");
        Console.WriteLine("\n== member references (opcode, parent, name)");
        foreach (var kv in memberRefs) Console.WriteLine($"  {kv.Key}\t{kv.Value}");
        Console.WriteLine("\n== metadata table rows (summed over files)");
        foreach (var kv in tables) Console.WriteLine($"  {kv.Key}\t{kv.Value}");
        return 0;
    }
}
