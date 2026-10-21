// IL.cs: CIL opcode table (from System.Reflection.Emit.OpCodes) and signature re-encoding.
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;

static class IL
{
    public struct Op { public string Name; public int Size; public OperandType Operand; }
    static readonly Op[] One = new Op[256];
    static readonly Op[] Two = new Op[256];
    static readonly bool[] HasOne = new bool[256];
    static readonly bool[] HasTwo = new bool[256];

    static IL()
    {
        foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
        {
            var oc = (OpCode)f.GetValue(null)!;
            var op = new Op { Name = oc.Name, Size = oc.Size, Operand = oc.OperandType };
            ushort v = (ushort)oc.Value;
            if (oc.Size == 1) { One[v & 0xff] = op; HasOne[v & 0xff] = true; }
            else { Two[v & 0xff] = op; HasTwo[v & 0xff] = true; }
        }
    }

    public static int OperandSize(OperandType t, byte[] il, int at) => t switch
    {
        OperandType.InlineNone => 0,
        OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
        OperandType.InlineVar => 2,
        OperandType.InlineI8 or OperandType.InlineR => 8,
        OperandType.InlineSwitch => 4 + 4 * BitConverter.ToInt32(il, at),
        _ => 4,
    };

    public static bool IsToken(OperandType t) => t is OperandType.InlineField or OperandType.InlineMethod
        or OperandType.InlineSig or OperandType.InlineString or OperandType.InlineTok or OperandType.InlineType;

    /// Walks the instruction stream; calls onToken(offsetOfToken, opcodeName, token) for every token
    /// operand. Returns the number of instructions.
    public static int Walk(byte[] il, Action<int, string, int> onToken, Action<string>? onOp = null)
    {
        int i = 0, n = 0;
        while (i < il.Length)
        {
            Op op;
            byte b = il[i];
            if (b == 0xFE) { op = Two[il[i + 1]]; if (!HasTwo[il[i + 1]]) throw new Exception($"bad opcode FE {il[i + 1]:X2} at {i}"); i += 2; }
            else { op = One[b]; if (!HasOne[b]) throw new Exception($"bad opcode {b:X2} at {i}"); i += 1; }
            onOp?.Invoke(op.Name);
            int sz = OperandSize(op.Operand, il, i);
            if (IsToken(op.Operand)) onToken(i, op.Name, BitConverter.ToInt32(il, i));
            i += sz; n++;
        }
        return n;
    }

    // ---- signatures ---------------------------------------------------------------------------
    // Re-encodes a signature blob, mapping every TypeDefOrRef coded index through mapType.
    // Handles method, field, property, local and standalone signatures (ECMA-335 II.23.2).
    public static byte[] RewriteSig(BlobReader r, Func<EntityHandle, EntityHandle> mapType, ref int codedIndices)
    {
        var w = new BlobBuilder();
        int ci = 0;
        byte hdr = r.ReadByte(); w.WriteByte(hdr);
        int kind = hdr & 0x0f;
        if (kind == 0x06) { CopyType(ref r, w, mapType, ref ci); }                    // FIELD
        else if (kind == 0x07)                                                        // LOCAL_SIG
        {
            int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
            for (int k = 0; k < n; k++) CopyType(ref r, w, mapType, ref ci);
        }
        else                                                                          // method / standalone / property
        {
            if ((hdr & 0x10) != 0) { int g = r.ReadCompressedInteger(); w.WriteCompressedInteger(g); }
            int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
            CopyType(ref r, w, mapType, ref ci);
            for (int k = 0; k < n; k++) CopyType(ref r, w, mapType, ref ci);
        }
        codedIndices += ci;
        return w.ToArray();
    }

    static void CopyType(ref BlobReader r, BlobBuilder w, Func<EntityHandle, EntityHandle> mapType, ref int ci)
    {
        byte et = r.ReadByte(); w.WriteByte(et);
        switch (et)
        {
            case 0x0F: case 0x10: case 0x1D: case 0x45:                               // PTR BYREF SZARRAY PINNED
                CopyType(ref r, w, mapType, ref ci); break;
            case 0x41: CopyType(ref r, w, mapType, ref ci); break;                    // SENTINEL then type
            case 0x11: case 0x12:                                                     // VALUETYPE CLASS
                {
                    var h = r.ReadTypeHandle(); ci++;
                    w.WriteCompressedInteger(CodedIndex.TypeDefOrRefOrSpec(mapType(h)));
                    break;
                }
            case 0x1F: case 0x20:                                                     // CMOD_REQD/OPT
                {
                    var h = r.ReadTypeHandle(); ci++;
                    w.WriteCompressedInteger(CodedIndex.TypeDefOrRefOrSpec(mapType(h)));
                    CopyType(ref r, w, mapType, ref ci); break;
                }
            case 0x1B:                                                                // FNPTR
                {
                    byte hdr = r.ReadByte(); w.WriteByte(hdr);
                    int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
                    CopyType(ref r, w, mapType, ref ci);
                    for (int k = 0; k < n; k++) CopyType(ref r, w, mapType, ref ci);
                    break;
                }
            case 0x13: case 0x1E: w.WriteCompressedInteger(r.ReadCompressedInteger()); break; // VAR MVAR
            case 0x15:                                                                // GENERICINST
                {
                    CopyType(ref r, w, mapType, ref ci);
                    int n = r.ReadCompressedInteger(); w.WriteCompressedInteger(n);
                    for (int k = 0; k < n; k++) CopyType(ref r, w, mapType, ref ci);
                    break;
                }
            case 0x14: throw new NotSupportedException("ARRAY in signature");
            default:
                if (et <= 0x0E || et == 0x16 || et == 0x18 || et == 0x19 || et == 0x1C) break; // primitives
                throw new NotSupportedException($"element type 0x{et:X2}");
        }
    }
}
