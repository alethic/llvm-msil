//===-- mdcanon.cs - Canonical listing v1 of an ECMA-335 image --*- C# -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// mdcanon writes canonical listing v1 of an IL-only PE image (an assembly or a
// netmodule): a layout-independent text that two writers of the same program
// (ilasm and msil-ld, Framework ilasm and the NuGet ilasm, ...) must agree on.
// The listing is specified in llvm/docs/MSIL/ImplementationPlan.md, 5.7.10;
// the normalization options in 5.12.6; the tool in 5.7.11. M17's
// 'llvm-cil-dump --canonical' writes the same listing from the same
// specification, byte for byte. Where the specification and this file
// disagree, the specification wins.
//
// Usage:
//   dotnet exec --runtimeconfig mdcanon.runtimeconfig.json mdcanon.dll
//       [--canonical] [--scopes=keep|strip] [--order=sorted|decl]
//       [--forms=exact|canon] [--assembly-hash=keep|ignore]
//       [--params=keep|drop] [--initlocals=keep|ignore-without-locals]
//       [--snan=keep|quiet] [--pinvoke-module=keep|strip]
//       [--pinvoke-access=keep|ignore] [--stats|--stats-only]
//       [--out-dir DIR] <file>...
//
// Without --out-dir the listings go to stdout; with several files each one is
// preceded by 'File: <path>'. --out-dir DIR writes DIR/<file name>.canon for
// every input in one process (no 'File:' line), which saves the .NET start-up
// of one process per file. An input that cannot be read or decoded prints
// 'mdcanon: error: '<file>': <message>' on stderr and the tool goes on with
// the next file. Exit status: 0, 1 if any input failed, 2 on a usage error.
//
// Everything is decoded here from the raw bytes (signatures, IL, method
// headers and their data sections, the ClassLayout, FieldLayout, FieldRVA and
// MethodSemantics tables, the stream headers), so the listing does not depend
// on System.Reflection.Metadata's own signature or body decoders; SRM is used
// for the PE headers and the row and heap accessors only. #Strings entries are
// read as raw bytes (each byte becomes one char), so names print as raw UTF-8
// and every comparison of listing text is a comparison of UTF-8 bytes, never of
// UTF-16 code units.
//
// Where 5.7.10 leaves a detail open, this file chooses (and M17 must match):
// - <propsig> prints as '[instance ]<type>(<p>, <p>)'.
// - The header lists come kind by kind (assemblyref, moduleref, file,
//   exportedtype, resource), each kind sorted by its text (or in table order).
// - .implements lines, .param lines (Sequence order) and EH clauses keep the
//   same order under both --order values; sort keys use the raw name
//   ('<n> <type>', '<n> <sig>', '<full>'), not its escaped form.
// - '.data <n> bytes' needs a FIELD signature that is exactly a primitive
//   (bool, char, int8 to uint64, float32, float64) or VALUETYPE of a TypeDef
//   with a ClassLayout row; anything else (native int, pointers, modifiers)
//   prints '.data (unknown size)'. Data past a section's raw data reads as
//   zeros up to its VirtualSize; an RVA outside every section is an error.
// - 'const <hex>' is the Constant row's Value blob; '# heap' sizes are the
//   stream headers' sizes; a branch target before the code prints
//   '<offset -0x<X>>'; a switch without targets prints 'switch' alone.
// - MethodSemantics 0x0002 prints .get, 0x0001 .set, any other value .other.
// - No entrypoint line when CorFlags has NativeEntryPoint; an entry point
//   that is not a MethodDef is an error.
// - --stats lines follow the listing directly.
// - Names inside [...] scopes and TypeRef names print raw, unquoted.
// - Not listed: custom attributes of other parents (Param, InterfaceImpl,
//   GenericParam, ...), events, generic parameters, MethodImpl, DeclSecurity.
// - An unknown opcode, a truncated operand, a body or data section that leaves
//   its section, an OptILTable section, bad clause flags, a bad token or a bad
//   signature fail the file.
//
//===----------------------------------------------------------------------===//

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

sealed class CanonOptions
{
    public bool StripScopes, DeclarationOrder, CanonicalForms;
    // The six normalizations of plan 5.12.6; false means 'keep'.
    public bool IgnoreAssemblyHash, DropParams, IgnoreInitLocalsWithoutLocals, QuietSignalingNaN,
        StripPInvokeModule, IgnorePInvokeAccess;
    public enum StatsMode { None, Append, Only }
    public StatsMode Stats = StatsMode.None;
}

sealed class CanonException : Exception
{
    public CanonException(string message) : base(message) { }
}

static class MdCanon
{
    const string Usage =
        "usage: mdcanon [--canonical] [--scopes=keep|strip] [--order=sorted|decl] [--forms=exact|canon]\n" +
        "               [--assembly-hash=keep|ignore] [--params=keep|drop]\n" +
        "               [--initlocals=keep|ignore-without-locals] [--snan=keep|quiet]\n" +
        "               [--pinvoke-module=keep|strip] [--pinvoke-access=keep|ignore]\n" +
        "               [--stats|--stats-only] [--out-dir DIR] <file>...\n";

    static int UsageError(string message)
    {
        Console.Error.Write("mdcanon: error: " + message + "\n" + Usage);
        return 2;
    }

    static int Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.DefaultThreadCurrentCulture = CultureInfo.InvariantCulture;
        var o = new CanonOptions();
        string outDir = null;
        var files = new List<string>();
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            switch (a)
            {
                case "--canonical": break;
                case "--scopes=keep": o.StripScopes = false; break;
                case "--scopes=strip": o.StripScopes = true; break;
                case "--order=sorted": o.DeclarationOrder = false; break;
                case "--order=decl": o.DeclarationOrder = true; break;
                case "--forms=exact": o.CanonicalForms = false; break;
                case "--forms=canon": o.CanonicalForms = true; break;
                case "--assembly-hash=keep": o.IgnoreAssemblyHash = false; break;
                case "--assembly-hash=ignore": o.IgnoreAssemblyHash = true; break;
                case "--params=keep": o.DropParams = false; break;
                case "--params=drop": o.DropParams = true; break;
                case "--initlocals=keep": o.IgnoreInitLocalsWithoutLocals = false; break;
                case "--initlocals=ignore-without-locals": o.IgnoreInitLocalsWithoutLocals = true; break;
                case "--snan=keep": o.QuietSignalingNaN = false; break;
                case "--snan=quiet": o.QuietSignalingNaN = true; break;
                case "--pinvoke-module=keep": o.StripPInvokeModule = false; break;
                case "--pinvoke-module=strip": o.StripPInvokeModule = true; break;
                case "--pinvoke-access=keep": o.IgnorePInvokeAccess = false; break;
                case "--pinvoke-access=ignore": o.IgnorePInvokeAccess = true; break;
                case "--stats": o.Stats = CanonOptions.StatsMode.Append; break;
                case "--stats-only": o.Stats = CanonOptions.StatsMode.Only; break;
                case "--out-dir":
                    if (++i == args.Length) return UsageError("--out-dir needs a directory");
                    outDir = args[i];
                    break;
                case "-h": case "--help":
                    Console.Out.Write(Usage);
                    return 0;
                default:
                    if (a.StartsWith("--out-dir=", StringComparison.Ordinal))
                        outDir = a.Substring("--out-dir=".Length);
                    else if (a.StartsWith("-", StringComparison.Ordinal) && a != "-")
                        return UsageError("unknown option '" + a + "'");
                    else
                        files.Add(a);
                    break;
            }
        }
        if (files.Count == 0) return UsageError("no input files");
        if (outDir != null)
        {
            if (outDir.Length == 0) return UsageError("--out-dir needs a directory");
            var seen = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var f in files)
            {
                string name = Path.GetFileName(f) + ".canon";
                if (seen.TryGetValue(name, out var other))
                    return UsageError("'" + f + "' and '" + other + "' would both write " + name);
                seen[name] = f;
            }
            Directory.CreateDirectory(outDir);
        }

        bool failed = false;
        var stdout = Console.OpenStandardOutput();
        foreach (var f in files)
        {
            if (outDir == null && files.Count > 1)
            {
                var head = Encoding.UTF8.GetBytes("File: " + f + "\n");
                stdout.Write(head, 0, head.Length);
            }
            byte[] listing;
            try
            {
                listing = Encoding.Latin1.GetBytes(new Canon(File.ReadAllBytes(f), o).Run());
            }
            catch (Exception e)
            {
                stdout.Flush();
                Console.Error.WriteLine("mdcanon: error: '" + f + "': " + e.Message);
                failed = true;
                continue;
            }
            if (outDir != null)
            {
                try
                {
                    File.WriteAllBytes(Path.Combine(outDir, Path.GetFileName(f) + ".canon"), listing);
                }
                catch (Exception e)
                {
                    Console.Error.WriteLine("mdcanon: error: '" + f + "': cannot write listing: " + e.Message);
                    failed = true;
                }
            }
            else
            {
                stdout.Write(listing, 0, listing.Length);
            }
        }
        stdout.Flush();
        return failed ? 1 : 0;
    }
}

// One image's listing. Text is built from "byte strings": every char is one
// byte (names are read from #Strings byte by byte; everything else is
// ASCII), so StringComparer.Ordinal orders by UTF-8 bytes and Latin-1
// encoding writes the bytes back unchanged.
sealed class Canon
{
    readonly CanonOptions O;
    readonly byte[] Image;
    PEReader pe;
    MetadataReader md;
    byte[] mdb;
    string metadataVersion = "";
    readonly Dictionary<string, (int Offset, int Size)> streams = new Dictionary<string, (int, int)>();
    readonly Dictionary<int, (ushort Pack, uint Size)> classLayout = new Dictionary<int, (ushort, uint)>();
    readonly Dictionary<int, uint> fieldOffset = new Dictionary<int, uint>();
    readonly Dictionary<int, uint> fieldRva = new Dictionary<int, uint>();
    readonly Dictionary<int, List<(ushort Semantics, int Method)>> propertySemantics =
        new Dictionary<int, List<(ushort, int)>>();
    readonly Dictionary<int, string> fullNames = new Dictionary<int, string>();
    readonly Dictionary<EntityHandle, string> entityText = new Dictionary<EntityHandle, string>();

    // ECMA-335 II.22 table names in id order (llvm/include/llvm/BinaryFormat/CLITables.def).
    static readonly string[] TableNames =
    {
        "Module", "TypeRef", "TypeDef", "FieldPtr", "Field", "MethodPtr", "MethodDef", "ParamPtr",
        "Param", "InterfaceImpl", "MemberRef", "Constant", "CustomAttribute", "FieldMarshal",
        "DeclSecurity", "ClassLayout", "FieldLayout", "StandAloneSig", "EventMap", "EventPtr", "Event",
        "PropertyMap", "PropertyPtr", "Property", "MethodSemantics", "MethodImpl", "ModuleRef",
        "TypeSpec", "ImplMap", "FieldRVA", "ENCLog", "ENCMap", "Assembly", "AssemblyProcessor",
        "AssemblyOS", "AssemblyRef", "AssemblyRefProcessor", "AssemblyRefOS", "File", "ExportedType",
        "ManifestResource", "NestedClass", "GenericParam", "MethodSpec", "GenericParamConstraint",
    };

    public Canon(byte[] image, CanonOptions o)
    {
        Image = image;
        O = o;
    }

    public string Run()
    {
        try
        {
            pe = new PEReader(new MemoryStream(Image, writable: false));
            if (pe.PEHeaders.PEHeader == null) throw new CanonException("not a PE image (no optional header)");
            if (pe.PEHeaders.CorHeader == null || !pe.HasMetadata)
                throw new CanonException("no CLI header (not an IL image)");
            md = pe.GetMetadataReader(MetadataReaderOptions.None);
            mdb = pe.GetMetadata().GetContent().ToArray();
            ReadMetadataRoot();
            ReadRawTables();
            var o = new StringBuilder();
            if (O.Stats != CanonOptions.StatsMode.Only)
            {
                Header(o);
                Types(o);
                Trailer(o);
            }
            if (O.Stats != CanonOptions.StatsMode.None) Stats(o);
            return o.ToString();
        }
        catch (BadImageFormatException e)
        {
            throw new CanonException("malformed image: " + e.Message);
        }
    }

    // ------------------------------------------------------------ text helpers

    static readonly Encoding Latin1 = Encoding.Latin1;

    // A string decoded by SRM as UTF-16 (section names) as a byte string.
    static string Bytes(string utf16) => Latin1.GetString(Encoding.UTF8.GetBytes(utf16));

    // '<n>': raw UTF-8; \ -> \\, ' -> \', bytes 0x00-0x1F and 0x7F -> \xHH.
    static string Q(string s)
    {
        var sb = new StringBuilder(s.Length + 2);
        foreach (char c in s)
        {
            if (c == '\\') sb.Append("\\\\");
            else if (c == '\'') sb.Append("\\'");
            else if (c < 0x20 || c == 0x7F) sb.Append("\\x").Append(((int)c).ToString("X2"));
            else sb.Append(c);
        }
        return sb.ToString();
    }

    // <hex>: uppercase byte pairs separated by one blank; '-' when empty.
    internal static string Hex(byte[] b, int start, int count)
    {
        if (count == 0) return "-";
        var sb = new StringBuilder(count * 3);
        for (int i = 0; i < count; i++)
        {
            if (i > 0) sb.Append(' ');
            sb.Append(b[start + i].ToString("X2"));
        }
        return sb.ToString();
    }

    internal static string Hex(byte[] b) => Hex(b, 0, b.Length);

    string BlobHex(BlobHandle h) => h.IsNil ? "-" : Hex(md.GetBlobBytes(h));

    // A #Strings entry as raw bytes (SRM's GetString would decode UTF-8 and
    // replace invalid sequences).
    string Str(StringHandle h)
    {
        if (h.IsNil) return "";
        int off = MetadataTokens.GetHeapOffset(h);
        if (!streams.TryGetValue("#Strings", out var heap) || off < 0 || off >= heap.Size)
            throw new CanonException("invalid #Strings index 0x" + off.ToString("X"));
        int start = heap.Offset + off, end = start, limit = heap.Offset + heap.Size;
        while (end < limit && mdb[end] != 0) end++;
        if (end == limit) throw new CanonException("unterminated #Strings entry at 0x" + off.ToString("X"));
        return Latin1.GetString(mdb, start, end - start);
    }

    static string NsName(string ns, string name) => ns.Length > 0 ? ns + "." + name : name;

    static string VersionText(Version v) => v.Major + "." + v.Minor + "." + v.Build + "." + v.Revision;

    // ldstr operands: \" \\ and \u<4X> for UTF-16 code units < 0x20 or > 0x7E.
    static string UserString(string s)
    {
        var sb = new StringBuilder(s.Length + 2);
        sb.Append('"');
        foreach (char c in s)
        {
            if (c == '"' || c == '\\') sb.Append('\\').Append(c);
            else if (c < 0x20 || c > 0x7E) sb.Append("\\u").Append(((int)c).ToString("X4"));
            else sb.Append(c);
        }
        return sb.Append('"').ToString();
    }

    // --order=sorted: by key, then by the whole text, then declaration order
    // (OrderBy is stable). Ordinal order of byte strings is UTF-8 byte order.
    IEnumerable<string> Order(List<(string Key, string Text)> items)
    {
        if (O.DeclarationOrder) return items.Select(x => x.Text);
        return items.OrderBy(x => x.Key, StringComparer.Ordinal)
            .ThenBy(x => x.Text, StringComparer.Ordinal)
            .Select(x => x.Text);
    }

    IEnumerable<string> OrderLines(List<string> lines) =>
        O.DeclarationOrder ? lines : lines.OrderBy(x => x, StringComparer.Ordinal);

    // ------------------------------------------------------------ raw metadata

    static ushort U16(byte[] b, int off) => (ushort)(b[off] | b[off + 1] << 8);
    static uint U32(byte[] b, int off) => (uint)(b[off] | b[off + 1] << 8 | b[off + 2] << 16 | b[off + 3] << 24);

    void ReadMetadataRoot()
    {
        if (mdb.Length < 20 || U32(mdb, 0) != 0x424A5342) throw new CanonException("bad metadata signature");
        int verLen = (int)U32(mdb, 12);
        if (verLen < 0 || 16 + verLen + 4 > mdb.Length) throw new CanonException("bad metadata version length");
        int end = 16;
        while (end < 16 + verLen && mdb[end] != 0) end++;
        metadataVersion = Latin1.GetString(mdb, 16, end - 16);
        int p = 16 + verLen;
        int count = U16(mdb, p + 2);
        p += 4;
        for (int i = 0; i < count; i++)
        {
            if (p + 8 > mdb.Length) throw new CanonException("truncated stream header");
            int offset = (int)U32(mdb, p), size = (int)U32(mdb, p + 4);
            int q = p + 8, e = q;
            while (e < mdb.Length && mdb[e] != 0) e++;
            if (e == mdb.Length) throw new CanonException("truncated stream name");
            string name = Latin1.GetString(mdb, q, e - q);
            if (offset < 0 || size < 0 || (long)offset + size > mdb.Length)
                throw new CanonException("stream " + name + " extends past the metadata");
            if (!streams.ContainsKey(name)) streams[name] = (offset, size);
            p = q + ((e - q + 1 + 3) & ~3);
        }
    }

    int Rows(TableIndex t) => md.GetTableRowCount(t);

    int IndexSize(TableIndex t) => Rows(t) < 0x10000 ? 2 : 4;

    int CodedIndexSize(int tagBits, params TableIndex[] tables) =>
        tables.Max(t => Rows(t)) < (1 << (16 - tagBits)) ? 2 : 4;

    static uint ReadIndex(byte[] b, int off, int size) => size == 2 ? U16(b, off) : U32(b, off);

    // Calls Row(offset) for every row of table t, checking the row size.
    void ForEachRow(TableIndex t, int expectedRowSize, Action<int> row)
    {
        int n = Rows(t);
        if (n == 0) return;
        int size = md.GetTableRowSize(t);
        if (size != expectedRowSize)
            throw new CanonException("unexpected " + TableNames[(int)t] + " row size " + size);
        int start = md.GetTableMetadataOffset(t);
        if (start < 0 || (long)start + (long)n * size > mdb.Length)
            throw new CanonException(TableNames[(int)t] + " table extends past the metadata");
        for (int i = 0; i < n; i++) row(start + i * size);
    }

    // The tables whose rows SRM cannot tell apart from a missing row (a
    // ClassLayout of pack 0 size 0, a FieldRVA of 0, all MethodSemantics rows
    // in table order) are read here.
    void ReadRawTables()
    {
        int typeDef = IndexSize(TableIndex.TypeDef);
        ForEachRow(TableIndex.ClassLayout, 6 + typeDef, off =>
        {
            int parent = (int)ReadIndex(mdb, off + 6, typeDef);
            if (!classLayout.ContainsKey(parent)) classLayout[parent] = (U16(mdb, off), U32(mdb, off + 2));
        });
        int field = IndexSize(TableIndex.Field);
        ForEachRow(TableIndex.FieldLayout, 4 + field, off =>
        {
            int f = (int)ReadIndex(mdb, off + 4, field);
            if (!fieldOffset.ContainsKey(f)) fieldOffset[f] = U32(mdb, off);
        });
        ForEachRow(TableIndex.FieldRva, 4 + field, off =>
        {
            int f = (int)ReadIndex(mdb, off + 4, field);
            if (!fieldRva.ContainsKey(f)) fieldRva[f] = U32(mdb, off);
        });
        int method = IndexSize(TableIndex.MethodDef);
        int assoc = CodedIndexSize(1, TableIndex.Event, TableIndex.Property);
        ForEachRow(TableIndex.MethodSemantics, 2 + method + assoc, off =>
        {
            uint a = ReadIndex(mdb, off + 2 + method, assoc);
            if ((a & 1) == 0) return; // an Event
            int prop = (int)(a >> 1);
            if (!propertySemantics.TryGetValue(prop, out var list))
                propertySemantics[prop] = list = new List<(ushort, int)>();
            list.Add((U16(mdb, off), (int)ReadIndex(mdb, off + 2, method)));
        });
    }

    // ------------------------------------------------------------ RVAs

    // The section that maps 'rva', with the size the loader maps (VirtualSize,
    // or SizeOfRawData when VirtualSize is 0).
    SectionHeader Section(long rva, out long extent)
    {
        foreach (var s in pe.PEHeaders.SectionHeaders)
        {
            long size = s.VirtualSize != 0 ? s.VirtualSize : s.SizeOfRawData;
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + size)
            {
                extent = size;
                return s;
            }
        }
        throw new CanonException("RVA 0x" + rva.ToString("X") + " is not in a section");
    }

    // Bytes [rva, rva + n) as mapped: raw data, then zeros up to the mapped size.
    byte[] ReadRva(long rva, int n)
    {
        var s = Section(rva, out long extent);
        long rel = rva - s.VirtualAddress;
        if (rel + n > extent)
            throw new CanonException("data at RVA 0x" + rva.ToString("X") + " (" + n + " bytes) extends past its section");
        var r = new byte[n];
        for (int k = 0; k < n; k++)
        {
            long at = rel + k;
            if (at >= s.SizeOfRawData) break; // zero fill
            long file = s.PointerToRawData + at;
            if (file >= Image.Length) throw new CanonException("section data past the end of the file");
            r[k] = Image[file];
        }
        return r;
    }

    // A reader over the raw data of the section that contains an RVA.
    sealed class RvaCursor
    {
        readonly byte[] image;
        readonly long rawStart, rawEnd;
        public long Pos; // file offset
        readonly long rvaBase; // file offset - RVA
        public RvaCursor(byte[] image, SectionHeader s, long rva, long extent)
        {
            this.image = image;
            rvaBase = s.PointerToRawData - (long)s.VirtualAddress;
            rawStart = s.PointerToRawData;
            rawEnd = Math.Min(Math.Min(s.PointerToRawData + (long)s.SizeOfRawData, s.PointerToRawData + extent),
                image.Length);
            Pos = rva + rvaBase;
        }
        public long Rva => Pos - rvaBase;
        void Need(long n)
        {
            if (Pos < rawStart || Pos + n > rawEnd) throw new CanonException("method body extends past its section");
        }
        public byte U8() { Need(1); return image[Pos++]; }
        public ushort U16() { Need(2); var v = (ushort)(image[Pos] | image[Pos + 1] << 8); Pos += 2; return v; }
        public uint U24() { Need(3); var v = (uint)(image[Pos] | image[Pos + 1] << 8 | image[Pos + 2] << 16); Pos += 3; return v; }
        public uint U32() { Need(4); var v = Canon.U32(image, (int)Pos); Pos += 4; return v; }
        public byte[] Bytes(long n) { Need(n); var r = new byte[n]; Array.Copy(image, Pos, r, 0, n); Pos += n; return r; }
        public void AlignRva4() { long pad = (4 - (Rva & 3)) & 3; Pos += pad; }
    }

    // ------------------------------------------------------------ tokens

    static readonly HashSet<int> EntityTables = new HashSet<int>
    {
        0x01, 0x02, 0x04, 0x06, 0x0A, 0x11, 0x1A, 0x1B, 0x23, 0x26, 0x27, 0x2B,
    };

    internal EntityHandle Token(uint token, string what)
    {
        int table = (int)(token >> 24);
        int row = (int)(token & 0xFFFFFF);
        if (!EntityTables.Contains(table) || row == 0 || row > Rows((TableIndex)table))
            throw new CanonException("invalid " + what + " token 0x" + token.ToString("X8"));
        return MetadataTokens.EntityHandle((int)token);
    }

    internal EntityHandle CodedTypeDefOrRef(uint coded)
    {
        uint tag = coded & 3;
        uint row = coded >> 2;
        uint table = tag switch { 0 => 0x02u, 1 => 0x01u, 2 => 0x1Bu, _ => 0xFFu };
        if (table == 0xFF) throw new CanonException("invalid signature: TypeDefOrRef tag 3");
        if (row == 0 || row > (uint)Rows((TableIndex)table))
            throw new CanonException("invalid signature: TypeDefOrRef row " + row + " out of range");
        return MetadataTokens.EntityHandle((int)(table << 24 | row));
    }

    // ------------------------------------------------------------ names and types

    // <full>: NS.Name, nested <enclosing full>/<NS.Name>.
    string Full(TypeDefinitionHandle h, int depth = 0)
    {
        int row = MetadataTokens.GetRowNumber(h);
        if (fullNames.TryGetValue(row, out var s)) return s;
        if (depth > 64) throw new CanonException("nested types deeper than 64 levels");
        var t = md.GetTypeDefinition(h);
        s = NsName(Str(t.Namespace), Str(t.Name));
        var outer = t.GetDeclaringType();
        if (!outer.IsNil) s = Full(outer, depth + 1) + "/" + s;
        fullNames[row] = s;
        return s;
    }

    string Owner(TypeDefinitionHandle h, string what)
    {
        if (h.IsNil) throw new CanonException(what + " has no owner type");
        return Full(h);
    }

    // <type> outside signatures. 'depth' is the signature depth at which a
    // TypeSpec's own type is decoded: 0 for a token outside a signature, one
    // more than the naming type's depth inside one (class Sig); TypeDef and
    // TypeRef names do not depend on it.
    internal string TypeText(EntityHandle h, int depth)
    {
        switch (h.Kind)
        {
            case HandleKind.TypeDefinition:
                return "'" + Q(Full((TypeDefinitionHandle)h)) + "'";
            case HandleKind.TypeReference:
                return TypeRefText((TypeReferenceHandle)h, 0);
            case HandleKind.TypeSpecification:
                return new Sig(this, md.GetBlobBytes(md.GetTypeSpecification((TypeSpecificationHandle)h).Signature))
                    .TypeSpec(depth);
            default:
                throw new CanonException("a " + h.Kind + " is not a type");
        }
    }

    string TypeRefText(TypeReferenceHandle h, int depth)
    {
        if (depth > 64) throw new CanonException("nested type references deeper than 64 levels");
        var t = md.GetTypeReference(h);
        string name = NsName(Str(t.Namespace), Str(t.Name));
        var scope = t.ResolutionScope;
        if (scope.IsNil) return name;
        switch (scope.Kind)
        {
            case HandleKind.AssemblyReference:
                return (O.StripScopes ? "[*]" : "[" + Str(md.GetAssemblyReference((AssemblyReferenceHandle)scope).Name) + "]") + name;
            case HandleKind.ModuleReference:
                return "[.module " + Str(md.GetModuleReference((ModuleReferenceHandle)scope).Name) + "]" + name;
            case HandleKind.ModuleDefinition:
                return "[.module self]" + name;
            case HandleKind.TypeReference:
                return TypeRefText((TypeReferenceHandle)scope, depth + 1) + "/" + name;
            default:
                throw new CanonException("invalid TypeRef resolution scope " + scope.Kind);
        }
    }

    string MethodSigText(BlobHandle b) => new Sig(this, md.GetBlobBytes(b)).Method();

    string FieldSigText(BlobHandle b) => new Sig(this, md.GetBlobBytes(b)).Field();

    // <method>: MethodDef, MemberRef or MethodSpec; also field MemberRefs.
    internal string MethodText(EntityHandle h)
    {
        if (entityText.TryGetValue(h, out var s)) return s;
        switch (h.Kind)
        {
            case HandleKind.MethodDefinition:
            {
                var m = md.GetMethodDefinition((MethodDefinitionHandle)h);
                s = MethodSigText(m.Signature) + " '" + Q(Owner(m.GetDeclaringType(), "a method")) + "'::'" +
                    Q(Str(m.Name)) + "'";
                break;
            }
            case HandleKind.MemberReference:
            {
                var r = md.GetMemberReference((MemberReferenceHandle)h);
                var sig = new Sig(this, md.GetBlobBytes(r.Signature));
                string sigText = sig.IsField ? sig.Field() : sig.Method();
                string parent;
                switch (r.Parent.Kind)
                {
                    case HandleKind.TypeDefinition:
                    case HandleKind.TypeReference:
                    case HandleKind.TypeSpecification:
                        parent = TypeText(r.Parent, 0);
                        break;
                    case HandleKind.ModuleReference:
                        parent = "[.module " + Str(md.GetModuleReference((ModuleReferenceHandle)r.Parent).Name) + "]";
                        break;
                    case HandleKind.MethodDefinition:
                        parent = MethodText(r.Parent);
                        break;
                    default:
                        throw new CanonException("invalid MemberRef parent " + r.Parent.Kind);
                }
                s = sigText + " " + parent + "::'" + Q(Str(r.Name)) + "'";
                break;
            }
            case HandleKind.MethodSpecification:
            {
                var spec = md.GetMethodSpecification((MethodSpecificationHandle)h);
                if (spec.Method.Kind != HandleKind.MethodDefinition && spec.Method.Kind != HandleKind.MemberReference)
                    throw new CanonException("invalid MethodSpec method " + spec.Method.Kind);
                s = MethodText(spec.Method) + "<" +
                    string.Join(", ", new Sig(this, md.GetBlobBytes(spec.Signature)).MethodSpec()) + ">";
                break;
            }
            default:
                throw new CanonException("a " + h.Kind + " is not a method");
        }
        entityText[h] = s;
        return s;
    }

    string FieldText(FieldDefinitionHandle h)
    {
        if (entityText.TryGetValue(h, out var s)) return s;
        var f = md.GetFieldDefinition(h);
        s = FieldSigText(f.Signature) + " '" + Q(Owner(f.GetDeclaringType(), "a field")) + "'::'" + Q(Str(f.Name)) + "'";
        entityText[h] = s;
        return s;
    }

    // A token operand: a type, a method or a field.
    string EntityText(EntityHandle h)
    {
        switch (h.Kind)
        {
            case HandleKind.TypeDefinition:
            case HandleKind.TypeReference:
            case HandleKind.TypeSpecification:
                return TypeText(h, 0);
            case HandleKind.FieldDefinition:
                return FieldText((FieldDefinitionHandle)h);
            case HandleKind.MethodDefinition:
            case HandleKind.MemberReference:
            case HandleKind.MethodSpecification:
                return MethodText(h);
            default:
                throw new CanonException("a " + h.Kind + " token is not a type, method or field");
        }
    }

    // ------------------------------------------------------------ header

    void Header(StringBuilder o)
    {
        var h = pe.PEHeaders;
        o.Append("pe ").Append(h.PEHeader.Magic == PEMagic.PE32Plus ? "PE32+" : "PE32")
         .Append(" machine=0x").Append(((ushort)h.CoffHeader.Machine).ToString("X4"))
         .Append(h.IsDll ? " dll" : " exe")
         .Append(" subsystem=").Append(((ushort)h.PEHeader.Subsystem).ToString()).Append('\n');
        var cor = h.CorHeader;
        o.Append("cor flags=0x").Append(((uint)cor.Flags).ToString("X8"))
         .Append(" runtime=").Append(cor.MajorRuntimeVersion.ToString()).Append('.').Append(cor.MinorRuntimeVersion.ToString())
         .Append(" metadata-version=").Append(metadataVersion).Append('\n');
        // With NativeEntryPoint the field is an RVA, which the listing leaves out.
        if ((cor.Flags & CorFlags.NativeEntryPoint) == 0 && cor.EntryPointTokenOrRelativeVirtualAddress != 0)
        {
            var ep = Token((uint)cor.EntryPointTokenOrRelativeVirtualAddress, "entry point");
            if (ep.Kind != HandleKind.MethodDefinition)
                throw new CanonException("the entry point token 0x" +
                    cor.EntryPointTokenOrRelativeVirtualAddress.ToString("X8") + " is not a MethodDef");
            o.Append("entrypoint ").Append(MethodText(ep)).Append('\n');
        }
        if (md.IsAssembly)
        {
            var a = md.GetAssemblyDefinition();
            o.Append("assembly '").Append(Q(Str(a.Name))).Append("' ver ").Append(VersionText(a.Version))
             .Append(" flags=0x").Append(((uint)a.Flags).ToString("X8"))
             .Append(" hash=").Append(O.IgnoreAssemblyHash ? "*" : "0x" + ((uint)a.HashAlgorithm).ToString("X8"))
             .Append(" culture='").Append(Q(Str(a.Culture))).Append("' key=").Append(BlobHex(a.PublicKey)).Append('\n');
        }
        o.Append("module '").Append(Q(Str(md.GetModuleDefinition().Name))).Append("'\n");

        // The header lists, one kind after the other, each sorted by its text.
        var lines = new List<string>();
        if (!O.StripScopes)
        {
            foreach (var r in md.AssemblyReferences)
            {
                var a = md.GetAssemblyReference(r);
                lines.Add("assemblyref '" + Q(Str(a.Name)) + "' ver " + VersionText(a.Version) +
                          " flags=0x" + ((uint)a.Flags).ToString("X8") + " key=" + BlobHex(a.PublicKeyOrToken) +
                          " culture='" + Q(Str(a.Culture)) + "'");
            }
            Lines(o, lines);
        }
        if (!O.StripPInvokeModule)
        {
            for (int i = 1; i <= Rows(TableIndex.ModuleRef); i++)
                lines.Add("moduleref '" + Q(Str(md.GetModuleReference(MetadataTokens.ModuleReferenceHandle(i)).Name)) + "'");
            Lines(o, lines);
        }
        foreach (var f in md.AssemblyFiles)
            lines.Add("file '" + Q(Str(md.GetAssemblyFile(f).Name)) + "'");
        Lines(o, lines);
        foreach (var e in md.ExportedTypes)
            lines.Add("exportedtype '" + Q(ExportedTypeFull(e, 0)) + "'");
        Lines(o, lines);
        foreach (var r in md.ManifestResources)
            lines.Add("resource '" + Q(Str(md.GetManifestResource(r).Name)) + "'");
        Lines(o, lines);
    }

    void Lines(StringBuilder o, List<string> lines)
    {
        foreach (var l in OrderLines(lines)) o.Append(l).Append('\n');
        lines.Clear();
    }

    string ExportedTypeFull(ExportedTypeHandle h, int depth)
    {
        if (depth > 64) throw new CanonException("nested exported types deeper than 64 levels");
        var e = md.GetExportedType(h);
        string name = NsName(Str(e.Namespace), Str(e.Name));
        if (e.Implementation.Kind == HandleKind.ExportedType)
            return ExportedTypeFull((ExportedTypeHandle)e.Implementation, depth + 1) + "/" + name;
        return name;
    }

    // ------------------------------------------------------------ types

    void Types(StringBuilder o)
    {
        var types = new List<(string, string)>();
        foreach (var h in md.TypeDefinitions)
        {
            string full = Full(h);
            types.Add((full, TypeBlock(h, full)));
        }
        foreach (var t in Order(types)) o.Append(t);
    }

    string TypeBlock(TypeDefinitionHandle h, string full)
    {
        var t = md.GetTypeDefinition(h);
        var o = new StringBuilder();
        o.Append("\n.class '").Append(Q(full)).Append("' flags=0x").Append(((uint)t.Attributes).ToString("X8"));
        if (!t.BaseType.IsNil) o.Append(" extends ").Append(TypeText(t.BaseType, 0));
        o.Append('\n');
        if (classLayout.TryGetValue(MetadataTokens.GetRowNumber(h), out var layout))
            o.Append("  .layout pack ").Append(layout.Pack.ToString()).Append(" size ").Append(layout.Size.ToString()).Append('\n');
        foreach (var i in t.GetInterfaceImplementations())
            o.Append("  .implements ").Append(TypeText(md.GetInterfaceImplementation(i).Interface, 0)).Append('\n');
        CustomAttributes(o, h, "  ");

        var fields = new List<(string, string)>();
        foreach (var fh in t.GetFields())
        {
            var f = md.GetFieldDefinition(fh);
            string type = FieldSigText(f.Signature);
            fields.Add((Str(f.Name) + " " + type, FieldBlock(fh, f, type)));
        }
        foreach (var s in Order(fields)) o.Append(s);

        var props = new List<(string, string)>();
        foreach (var ph in t.GetProperties())
        {
            var p = md.GetPropertyDefinition(ph);
            string sig = new Sig(this, md.GetBlobBytes(p.Signature)).Property();
            props.Add((Str(p.Name) + " " + sig, PropertyBlock(ph, p, sig)));
        }
        foreach (var s in Order(props)) o.Append(s);

        var methods = new List<(string, string)>();
        foreach (var mh in t.GetMethods())
        {
            var m = md.GetMethodDefinition(mh);
            string sig = MethodSigText(m.Signature);
            methods.Add((Str(m.Name) + " " + sig, MethodBlock(mh, m, sig)));
        }
        foreach (var s in Order(methods)) o.Append(s);
        return o.ToString();
    }

    void CustomAttributes(StringBuilder o, EntityHandle parent, string prefix)
    {
        var lines = new List<string>();
        foreach (var ah in md.GetCustomAttributes(parent))
        {
            var ca = md.GetCustomAttribute(ah);
            if (ca.Constructor.Kind != HandleKind.MethodDefinition && ca.Constructor.Kind != HandleKind.MemberReference)
                throw new CanonException("invalid custom attribute constructor " + ca.Constructor.Kind);
            lines.Add(".custom " + MethodText(ca.Constructor) + " = " + BlobHex(ca.Value));
        }
        foreach (var l in OrderLines(lines)) o.Append(prefix).Append(l).Append('\n');
    }

    string FieldBlock(FieldDefinitionHandle h, FieldDefinition f, string type)
    {
        var o = new StringBuilder();
        int row = MetadataTokens.GetRowNumber(h);
        o.Append("  .field '").Append(Q(Str(f.Name))).Append("' ").Append(type)
         .Append(" flags=0x").Append(((ushort)f.Attributes).ToString("X4"));
        if (fieldOffset.TryGetValue(row, out uint offset)) o.Append(" offset ").Append(offset.ToString());
        var c = f.GetDefaultValue();
        if (!c.IsNil) o.Append(" const ").Append(BlobHex(md.GetConstant(c).Value));
        var marshal = f.GetMarshallingDescriptor();
        if (!marshal.IsNil) o.Append(" marshal ").Append(BlobHex(marshal));
        o.Append('\n');
        CustomAttributes(o, h, "    ");
        if (fieldRva.TryGetValue(row, out uint rva))
        {
            int size = FieldDataSize(md.GetBlobBytes(f.Signature));
            if (size < 0)
            {
                o.Append("    .data (unknown size)\n");
            }
            else
            {
                var data = ReadRva(rva, size);
                o.Append("    .data ").Append(size.ToString()).Append(" bytes\n");
                for (int i = 0; i < size; i += 32)
                    o.Append("      ").Append(Hex(data, i, Math.Min(32, size - i))).Append('\n');
            }
        }
        return o.ToString();
    }

    // The size of a primitive field type, or the ClassLayout size of a TypeDef
    // valuetype; -1 otherwise (5.7.10). The signature was validated by FieldSigText.
    int FieldDataSize(byte[] sig)
    {
        if (sig.Length < 2 || sig[0] != 0x06) return -1;
        switch (sig[1])
        {
            case 0x02: case 0x04: case 0x05: return 1;          // bool int8 uint8
            case 0x03: case 0x06: case 0x07: return 2;          // char int16 uint16
            case 0x08: case 0x09: case 0x0C: return 4;          // int32 uint32 float32
            case 0x0A: case 0x0B: case 0x0D: return 8;          // int64 uint64 float64
            case 0x11:
            {
                var r = new Sig(this, sig);
                var h = CodedTypeDefOrRef(r.CompressedAt(2));
                if (h.Kind != HandleKind.TypeDefinition) return -1;
                if (!classLayout.TryGetValue(MetadataTokens.GetRowNumber(h), out var l)) return -1;
                if (l.Size > int.MaxValue) throw new CanonException("ClassLayout size too large");
                return (int)l.Size;
            }
            default:
                return -1;
        }
    }

    string PropertyBlock(PropertyDefinitionHandle h, PropertyDefinition p, string sig)
    {
        var o = new StringBuilder();
        o.Append("  .property '").Append(Q(Str(p.Name))).Append("' ").Append(sig)
         .Append(" flags=0x").Append(((ushort)p.Attributes).ToString("X4")).Append('\n');
        var lines = new List<string>();
        if (propertySemantics.TryGetValue(MetadataTokens.GetRowNumber(h), out var rows))
        {
            foreach (var (sem, method) in rows)
            {
                if (method == 0 || method > Rows(TableIndex.MethodDef))
                    throw new CanonException("invalid MethodSemantics method row " + method);
                string kind = sem == 0x0002 ? ".get " : sem == 0x0001 ? ".set " : ".other ";
                lines.Add(kind + MethodText(MetadataTokens.MethodDefinitionHandle(method)));
            }
        }
        foreach (var l in OrderLines(lines)) o.Append("    ").Append(l).Append('\n');
        CustomAttributes(o, h, "    ");
        return o.ToString();
    }

    string MethodBlock(MethodDefinitionHandle h, MethodDefinition m, string sig)
    {
        var o = new StringBuilder();
        ushort flags = (ushort)m.Attributes;
        if (O.IgnorePInvokeAccess && (flags & 0x2000) != 0) flags &= unchecked((ushort)~0x0007);
        o.Append("  .method '").Append(Q(Str(m.Name))).Append("' ").Append(sig)
         .Append(" flags=0x").Append(flags.ToString("X4"))
         .Append(" impl=0x").Append(((ushort)m.ImplAttributes).ToString("X4")).Append('\n');
        var imp = m.GetImport();
        if (!imp.Module.IsNil)
        {
            o.Append("    .pinvokeimpl '")
             .Append(O.StripPInvokeModule ? "*" : Q(Str(md.GetModuleReference(imp.Module).Name)))
             .Append("' entry '").Append(Q(Str(imp.Name))).Append("' flags=0x")
             .Append(((ushort)imp.Attributes).ToString("X4")).Append('\n');
        }
        var pars = m.GetParameters().Select(p => md.GetParameter(p)).ToList();
        foreach (var p in pars.OrderBy(p => p.SequenceNumber))
        {
            var marshal = p.GetMarshallingDescriptor();
            var c = p.GetDefaultValue();
            if (O.DropParams && p.Attributes == 0 && marshal.IsNil && c.IsNil) continue;
            o.Append("    .param ").Append(p.SequenceNumber.ToString()).Append(" '").Append(Q(Str(p.Name)))
             .Append("' flags=0x").Append(((ushort)p.Attributes).ToString("X4"));
            if (!marshal.IsNil) o.Append(" marshal ").Append(BlobHex(marshal));
            if (!c.IsNil) o.Append(" const ").Append(BlobHex(md.GetConstant(c).Value));
            o.Append('\n');
        }
        CustomAttributes(o, h, "    ");
        if (m.RelativeVirtualAddress != 0) Body(o, m.RelativeVirtualAddress);
        return o.ToString();
    }

    // ------------------------------------------------------------ method bodies (II.25.4)

    struct Clause
    {
        public uint Flags, TryOffset, TryLength, HandlerOffset, HandlerLength, ClassTokenOrFilter;
    }

    void Body(StringBuilder o, int rva)
    {
        var s = Section(rva, out long extent);
        var r = new RvaCursor(Image, s, rva, extent);
        byte b0 = r.U8();
        int maxStack;
        uint localTok = 0;
        bool initLocals = false, moreSects = false;
        byte[] code;
        if ((b0 & 3) == 2)
        {
            maxStack = 8;
            code = r.Bytes(b0 >> 2);
        }
        else if ((b0 & 3) == 3)
        {
            r.Pos--;
            ushort flagsAndSize = r.U16();
            if (flagsAndSize >> 12 != 3)
                throw new CanonException("method at RVA 0x" + rva.ToString("X") + ": fat header size " + (flagsAndSize >> 12) + " is not 3");
            moreSects = (flagsAndSize & 0x08) != 0;
            initLocals = (flagsAndSize & 0x10) != 0;
            maxStack = r.U16();
            uint codeSize = r.U32();
            localTok = r.U32();
            code = r.Bytes(codeSize);
        }
        else
        {
            throw new CanonException("method at RVA 0x" + rva.ToString("X") + ": invalid header byte 0x" + b0.ToString("X2"));
        }

        var clauses = new List<Clause>();
        while (moreSects)
        {
            r.AlignRva4();
            byte kind = r.U8();
            if ((kind & 0x3F) != 0x01)
                throw new CanonException("method at RVA 0x" + rva.ToString("X") + ": unsupported data section kind 0x" + kind.ToString("X2"));
            bool fat = (kind & 0x40) != 0;
            moreSects = (kind & 0x80) != 0;
            uint dataSize = fat ? r.U24() : r.U8();
            if (!fat) r.U16();
            if (dataSize < 4)
                throw new CanonException("method at RVA 0x" + rva.ToString("X") + ": exception section of " + dataSize + " bytes");
            uint n = (dataSize - 4) / (fat ? 24u : 12u);
            long next = r.Pos - 4 + dataSize;
            for (uint i = 0; i < n; i++)
            {
                var c = new Clause();
                if (fat)
                {
                    c.Flags = r.U32(); c.TryOffset = r.U32(); c.TryLength = r.U32();
                    c.HandlerOffset = r.U32(); c.HandlerLength = r.U32(); c.ClassTokenOrFilter = r.U32();
                }
                else
                {
                    c.Flags = r.U16(); c.TryOffset = r.U16(); c.TryLength = r.U8();
                    c.HandlerOffset = r.U16(); c.HandlerLength = r.U8(); c.ClassTokenOrFilter = r.U32();
                }
                clauses.Add(c);
            }
            r.Pos = next;
        }

        o.Append("    .maxstack ").Append(maxStack.ToString());
        if (initLocals && !(O.IgnoreInitLocalsWithoutLocals && localTok == 0)) o.Append(" initlocals");
        o.Append('\n');
        if (localTok != 0)
        {
            var lh = Token(localTok, "local signature");
            if (lh.Kind != HandleKind.StandaloneSignature)
                throw new CanonException("local signature token 0x" + localTok.ToString("X8") + " is not a StandAloneSig");
            var blob = md.GetBlobBytes(md.GetStandaloneSignature((StandaloneSignatureHandle)lh).Signature);
            o.Append("    .locals (").Append(string.Join(", ", new Sig(this, blob).Locals())).Append(")\n");
        }
        Instructions(o, code, clauses, rva);
    }

    sealed class OpInfo
    {
        public string Name;
        public OperandType Operand;
    }

    // System.Reflection.Emit.OpCodes without the reserved prefixes 0xF8-0xFF,
    // plus ECMA-335's 'no.' (0xFE19), which System.Reflection.Emit lacks: the
    // 219 opcodes of llvm/include/llvm/BinaryFormat/CILOpcodes.def.
    static readonly Dictionary<int, OpInfo> Ops = BuildOps();

    static Dictionary<int, OpInfo> BuildOps()
    {
        var d = new Dictionary<int, OpInfo>();
        foreach (var f in typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static))
        {
            var op = (OpCode)f.GetValue(null);
            if (op.Name.StartsWith("prefix", StringComparison.Ordinal)) continue;
            d[(ushort)op.Value] = new OpInfo { Name = op.Name, Operand = op.OperandType };
        }
        d[0xFE19] = new OpInfo { Name = "no.", Operand = OperandType.ShortInlineI };
        return d;
    }

    sealed class Inst
    {
        public int Offset;
        public OpInfo Op;
        public string Operand;
        public long[] Targets;
    }

    void Instructions(StringBuilder o, byte[] code, List<Clause> clauses, int rva)
    {
        var insts = new List<Inst>();
        int pc = 0;
        string Where(int at) => "method at RVA 0x" + rva.ToString("X") + ", IL offset 0x" + at.ToString("X");
        while (pc < code.Length)
        {
            int start = pc;
            int v = code[pc++];
            if (v == 0xFE)
            {
                if (pc >= code.Length) throw new CanonException(Where(start) + ": truncated opcode");
                v = 0xFE00 | code[pc++];
            }
            if (!Ops.TryGetValue(v, out var op))
                throw new CanonException(Where(start) + ": unknown opcode 0x" + v.ToString(v > 0xFF ? "X4" : "X2"));
            var inst = new Inst { Offset = start, Op = op };
            void Need(int n)
            {
                if (pc + n > code.Length) throw new CanonException(Where(start) + ": truncated operand of " + op.Name);
            }
            switch (op.Operand)
            {
                case OperandType.InlineNone:
                    break;
                case OperandType.ShortInlineVar:
                    Need(1); inst.Operand = code[pc].ToString(); pc += 1;
                    break;
                case OperandType.InlineVar:
                    Need(2); inst.Operand = U16(code, pc).ToString(); pc += 2;
                    break;
                case OperandType.ShortInlineI:
                    Need(1);
                    inst.Operand = v == 0x1F ? ((sbyte)code[pc]).ToString() : code[pc].ToString(); // ldc.i4.s signed
                    pc += 1;
                    break;
                case OperandType.InlineI:
                    Need(4); inst.Operand = ((int)U32(code, pc)).ToString(); pc += 4;
                    break;
                case OperandType.InlineI8:
                    Need(8); inst.Operand = BitConverter.ToInt64(code, pc).ToString(); pc += 8;
                    break;
                case OperandType.ShortInlineR:
                {
                    Need(4);
                    uint bits = U32(code, pc);
                    pc += 4;
                    // --snan=quiet: a float32 signaling NaN prints with bit 22 set (5.12.6).
                    if (O.QuietSignalingNaN && (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0 &&
                        (bits & 0x00400000) == 0)
                        bits |= 0x00400000;
                    inst.Operand = "float32(0x" + bits.ToString("X8") + ")";
                    break;
                }
                case OperandType.InlineR:
                    Need(8); inst.Operand = "float64(0x" + BitConverter.ToUInt64(code, pc).ToString("X16") + ")"; pc += 8;
                    break;
                case OperandType.ShortInlineBrTarget:
                {
                    Need(1);
                    int d = (sbyte)code[pc];
                    pc += 1;
                    inst.Targets = new[] { (long)pc + d };
                    break;
                }
                case OperandType.InlineBrTarget:
                {
                    Need(4);
                    int d = (int)U32(code, pc);
                    pc += 4;
                    inst.Targets = new[] { (long)pc + d };
                    break;
                }
                case OperandType.InlineSwitch:
                {
                    Need(4);
                    uint n = U32(code, pc);
                    pc += 4;
                    if ((long)n * 4 > code.Length - pc) throw new CanonException(Where(start) + ": truncated switch table");
                    long next = pc + 4L * n;
                    inst.Targets = new long[n];
                    for (int i = 0; i < n; i++) inst.Targets[i] = next + (int)U32(code, pc + 4 * i);
                    pc = (int)next;
                    break;
                }
                case OperandType.InlineString:
                {
                    Need(4);
                    uint tok = U32(code, pc);
                    pc += 4;
                    int off = (int)(tok & 0xFFFFFF);
                    if (tok >> 24 != 0x70 || off == 0 || off >= md.GetHeapSize(HeapIndex.UserString))
                        throw new CanonException(Where(start) + ": invalid string token 0x" + tok.ToString("X8"));
                    inst.Operand = UserString(md.GetUserString(MetadataTokens.UserStringHandle(off)));
                    break;
                }
                case OperandType.InlineSig:
                {
                    Need(4);
                    uint tok = U32(code, pc);
                    pc += 4;
                    var sh = Token(tok, "signature");
                    if (sh.Kind != HandleKind.StandaloneSignature)
                        throw new CanonException(Where(start) + ": calli token 0x" + tok.ToString("X8") + " is not a StandAloneSig");
                    inst.Operand = MethodSigText(md.GetStandaloneSignature((StandaloneSignatureHandle)sh).Signature);
                    break;
                }
                case OperandType.InlineMethod:
                case OperandType.InlineField:
                case OperandType.InlineType:
                case OperandType.InlineTok:
                {
                    Need(4);
                    uint tok = U32(code, pc);
                    pc += 4;
                    inst.Operand = EntityText(Token(tok, "operand"));
                    break;
                }
                default:
                    throw new CanonException(Where(start) + ": unsupported operand type " + op.Operand);
            }
            insts.Add(inst);
        }

        var index = new Dictionary<long, int>();
        for (int i = 0; i < insts.Count; i++) index[insts[i].Offset] = i;
        index[code.Length] = insts.Count;
        string Label(long off)
        {
            if (index.TryGetValue(off, out int i)) return "L" + i;
            return off < 0 ? "<offset -0x" + (-off).ToString("X") + ">" : "<offset 0x" + off.ToString("X") + ">";
        }
        for (int i = 0; i < insts.Count; i++)
        {
            var x = insts[i];
            o.Append("    L").Append(i.ToString()).Append(": ").Append(OpName(x.Op));
            if (x.Targets != null)
            {
                if (x.Targets.Length > 0) o.Append(' ').Append(string.Join(", ", x.Targets.Select(Label)));
            }
            else if (x.Operand != null)
            {
                o.Append(' ').Append(x.Operand);
            }
            o.Append('\n');
        }
        foreach (var c in clauses)
        {
            o.Append("    .try ").Append(Label(c.TryOffset)).Append(" to ").Append(Label((long)c.TryOffset + c.TryLength)).Append(' ');
            switch (c.Flags)
            {
                case 0x0000:
                {
                    var ch = Token(c.ClassTokenOrFilter, "catch type");
                    o.Append("catch ").Append(TypeText(ch, 0));
                    break;
                }
                case 0x0001: o.Append("filter ").Append(Label(c.ClassTokenOrFilter)); break;
                case 0x0002: o.Append("finally"); break;
                case 0x0004: o.Append("fault"); break;
                default:
                    throw new CanonException("method at RVA 0x" + rva.ToString("X") + ": invalid exception clause flags 0x" + c.Flags.ToString("X"));
            }
            o.Append(" handler ").Append(Label(c.HandlerOffset)).Append(" to ").Append(Label((long)c.HandlerOffset + c.HandlerLength)).Append('\n');
        }
    }

    static readonly HashSet<string> ShortVarForms = new HashSet<string>
    {
        "ldarg.s", "ldarga.s", "starg.s", "ldloc.s", "ldloca.s", "stloc.s", "ldc.i4.s",
    };

    // --forms=canon: short branches as their long forms, the short variable and
    // ldc.i4.s forms without '.s'.
    string OpName(OpInfo op)
    {
        string n = op.Name;
        if (!O.CanonicalForms) return n;
        if ((op.Operand == OperandType.ShortInlineBrTarget || ShortVarForms.Contains(n)) && n.EndsWith(".s", StringComparison.Ordinal))
            return n.Substring(0, n.Length - 2);
        return n;
    }

    // ------------------------------------------------------------ trailer and stats

    void Trailer(StringBuilder o)
    {
        CustomAttributes(o, EntityHandle.AssemblyDefinition, "assembly ");
        CustomAttributes(o, EntityHandle.ModuleDefinition, "module ");
    }

    void Stats(StringBuilder o)
    {
        for (int id = 0; id < TableNames.Length; id++)
        {
            int n = Rows((TableIndex)id);
            if (n > 0)
                o.Append("# table ").Append(TableNames[id]).Append(" rows=").Append(n.ToString())
                 .Append(" rowsize=").Append(md.GetTableRowSize((TableIndex)id).ToString()).Append('\n');
        }
        foreach (var heap in new[] { "#Strings", "#US", "#GUID", "#Blob" })
        {
            streams.TryGetValue(heap, out var s);
            o.Append("# heap ").Append(heap).Append(" size=").Append(s.Size.ToString()).Append('\n');
        }
        var h = pe.PEHeaders;
        o.Append("# image size=").Append(h.PEHeader.SizeOfImage.ToString()).Append(" sections=")
         .Append(string.Join(",", h.SectionHeaders.Select(s => Bytes(s.Name)))).Append('\n');
    }
}

// A signature blob decoder (II.23.2) that prints canonical listing v1's
// signature grammar. Custom modifiers print after the type they precede, in
// blob order. It rejects exactly what the C++ decoders (llvm/lib/CLI/
// Signature.cpp) reject (plan 5.7.3 and 5.7.11): besides truncation, bad
// compressed integers, unknown element types and trailing bytes, an ARRAY of
// rank 0 or above 32, a GENERICINST without arguments or of a kind other than
// CLASS or VALUETYPE, a MethodSpec without arguments, PINNED anywhere but at
// the top of a local (its operand is not itself such a top), a SENTINEL
// outside a VARARG or C method signature or after another one, a
// TypeDefOrRefOrSpecEncoded value with tag 3 or row 0, and a type inside more
// than 64 PTR, BYREF, SZARRAY, ARRAY, GENERICINST, FNPTR or PINNED
// constructors (the top type of a blob, a method signature's return and
// parameter types included, is at depth 0). A TypeSpec token continues the
// depth of the type that names it, which bounds TypeSpec cycles.
sealed class Sig
{
    readonly Canon C;
    readonly byte[] B;
    int P;

    public Sig(Canon c, byte[] b)
    {
        C = c;
        B = b;
    }

    Exception Bad(string what) =>
        new CanonException("invalid signature: " + what + " at byte " + P + " of '" + Canon.Hex(B) + "'");

    byte U8()
    {
        if (P >= B.Length) throw Bad("truncated");
        return B[P++];
    }

    int Peek() => P < B.Length ? B[P] : -1;

    uint CU()
    {
        byte b0 = U8();
        if ((b0 & 0x80) == 0) return b0;
        if ((b0 & 0xC0) == 0x80) return (uint)((b0 & 0x3F) << 8 | U8());
        if ((b0 & 0xE0) == 0xC0)
        {
            uint v = (uint)(b0 & 0x1F) << 24;
            v |= (uint)U8() << 16;
            v |= (uint)U8() << 8;
            return v | U8();
        }
        throw Bad("bad compressed integer");
    }

    // A signed compressed integer (II.23.2): the unsigned encoding of the
    // 7-, 14- or 29-bit two's complement value rotated left by one bit, so
    // bit 0 holds the sign.
    int CS()
    {
        int start = P;
        uint u = CU();
        int bits = P - start == 1 ? 7 : P - start == 2 ? 14 : 29;
        int v = (int)(u >> 1);
        return (u & 1) != 0 ? v - (1 << (bits - 1)) : v;
    }

    public uint CompressedAt(int pos)
    {
        P = pos;
        return CU();
    }

    void End()
    {
        if (P != B.Length) throw Bad("trailing bytes");
    }

    public bool IsField => B.Length > 0 && B[0] == 0x06;

    string TypeDefOrRef(int depth) => C.TypeText(C.CodedTypeDefOrRef(CU()), depth + 1);

    string Type(int depth, bool allowPinned = false)
    {
        if (depth > 64) throw Bad("nesting deeper than 64 levels");
        var mods = new StringBuilder();
        for (int b = Peek(); b == 0x1F || b == 0x20; b = Peek())
        {
            P++;
            mods.Append(b == 0x1F ? " modreq(" : " modopt(").Append(TypeDefOrRef(depth)).Append(')');
        }
        byte et = U8();
        string core;
        switch (et)
        {
            case 0x01: core = "void"; break;
            case 0x02: core = "bool"; break;
            case 0x03: core = "char"; break;
            case 0x04: core = "int8"; break;
            case 0x05: core = "uint8"; break;
            case 0x06: core = "int16"; break;
            case 0x07: core = "uint16"; break;
            case 0x08: core = "int32"; break;
            case 0x09: core = "uint32"; break;
            case 0x0A: core = "int64"; break;
            case 0x0B: core = "uint64"; break;
            case 0x0C: core = "float32"; break;
            case 0x0D: core = "float64"; break;
            case 0x0E: core = "string"; break;
            case 0x0F: core = Type(depth + 1) + "*"; break;
            case 0x10: core = Type(depth + 1) + "&"; break;
            case 0x11: core = "valuetype " + TypeDefOrRef(depth); break;
            case 0x12: core = "class " + TypeDefOrRef(depth); break;
            case 0x13: core = "!" + CU(); break;
            case 0x14:
            {
                // A general array (II.23.2.13) prints its shape losslessly:
                // dimension i is '<lo>...<hi>' (both), '<lo>...' (a lower
                // bound only), '<size>' (a size only) or empty (neither),
                // except that a rank-1 array with neither prints '...'; only
                // SZARRAY prints '<t>[]'.
                string element = Type(depth + 1);
                int rankAt = P;
                uint rank = CU();
                // 32 is the CLR's MAX_RANK: no runtime loads a higher rank.
                if (rank == 0 || rank > 32) { P = rankAt; throw Bad("array rank " + rank); }
                uint numSizes = CU();
                var sizes = new List<uint>();
                for (uint i = 0; i < numSizes; i++) sizes.Add(CU());
                uint numLoBounds = CU();
                var loBounds = new List<int>();
                for (uint i = 0; i < numLoBounds; i++) loBounds.Add(CS());
                var dims = new string[rank];
                for (int i = 0; i < dims.Length; i++)
                {
                    bool hasLo = i < loBounds.Count, hasSize = i < sizes.Count;
                    if (hasLo && hasSize)
                        dims[i] = loBounds[i] + "..." + ((long)loBounds[i] + sizes[i] - 1);
                    else if (hasLo)
                        dims[i] = loBounds[i] + "...";
                    else if (hasSize)
                        dims[i] = sizes[i].ToString();
                    else
                        dims[i] = rank == 1 ? "..." : "";
                }
                core = element + "[" + string.Join(",", dims) + "]";
                break;
            }
            case 0x15:
            {
                byte kind = U8();
                if (kind != 0x11 && kind != 0x12) throw Bad("GENERICINST of element type 0x" + kind.ToString("X2"));
                string generic = TypeDefOrRef(depth);
                int countAt = P;
                uint n = CU();
                if (n == 0) { P = countAt; throw Bad("generic instantiation without arguments"); }
                var args = new List<string>();
                for (uint i = 0; i < n; i++) args.Add(Type(depth + 1));
                core = (kind == 0x11 ? "valuetype " : "class ") + generic + "<" + string.Join(", ", args) + ">";
                break;
            }
            case 0x16: core = "typedref"; break;
            case 0x18: core = "native int"; break;
            case 0x19: core = "native uint"; break;
            case 0x1B: core = "method " + MethodSig(depth + 1); break;
            case 0x1C: core = "object"; break;
            case 0x1D: core = Type(depth + 1) + "[]"; break;
            case 0x1E: core = "!!" + CU(); break;
            case 0x45 when allowPinned: core = Type(depth + 1) + " pinned"; break;
            default:
                P--;
                throw Bad(et == 0x41 ? "misplaced SENTINEL"
                          : et == 0x45 ? "misplaced PINNED"
                          : "unknown element type 0x" + et.ToString("X2"));
        }
        return core + mods.ToString();
    }

    // <methodsig>: [instance ][explicit ]<cc>[<<n>> ]<ret>(<p>, <p>), with
    // '..., ' before the first parameter after the SENTINEL. The return and
    // parameter types are at 'depth' (0 for a MethodDefSig or MethodRefSig,
    // one more than the FNPTR's own depth for a function pointer).
    string MethodSig(int depth)
    {
        byte h = U8();
        string cc;
        switch (h & 0x0F)
        {
            case 0x0: cc = ""; break;
            case 0x1: cc = "unmanaged cdecl "; break;
            case 0x2: cc = "unmanaged stdcall "; break;
            case 0x3: cc = "unmanaged thiscall "; break;
            case 0x4: cc = "unmanaged fastcall "; break;
            case 0x5: cc = "vararg "; break;
            case 0x9: cc = "unmanaged "; break;
            default: P--; throw Bad("method signature with calling convention 0x" + (h & 0x0F).ToString("X"));
        }
        if ((h & 0x80) != 0) { P--; throw Bad("reserved signature header bit"); }
        uint generic = (h & 0x10) != 0 ? CU() : 0;
        uint count = CU();
        var o = new StringBuilder();
        if ((h & 0x20) != 0) o.Append("instance ");
        if ((h & 0x40) != 0) o.Append("explicit ");
        o.Append(cc);
        if ((h & 0x10) != 0 && generic > 0) o.Append('<').Append(generic.ToString()).Append("> ");
        o.Append(Type(depth)).Append('(');
        // A SENTINEL may precede one parameter of a VARARG or C signature.
        bool sentinelAllowed = (h & 0x0F) == 0x5 || (h & 0x0F) == 0x1;
        bool sentinel = false;
        for (uint i = 0; i < count; i++)
        {
            if (i > 0) o.Append(", ");
            if (Peek() == 0x41)
            {
                if (sentinel || !sentinelAllowed) throw Bad("misplaced SENTINEL");
                sentinel = true;
                P++;
                o.Append("..., ");
            }
            o.Append(Type(depth));
        }
        return o.Append(')').ToString();
    }

    public string Method()
    {
        string s = MethodSig(0);
        End();
        return s;
    }

    public string Field()
    {
        if (U8() != 0x06) { P--; throw Bad("not a field signature"); }
        string s = Type(0);
        End();
        return s;
    }

    // <propsig>: [instance ]<type>(<p>, <p>).
    public string Property()
    {
        byte h = U8();
        if ((h & 0xDF) != 0x08) { P--; throw Bad("not a property signature"); }
        uint count = CU();
        string type = Type(0);
        var ps = new List<string>();
        for (uint i = 0; i < count; i++) ps.Add(Type(0));
        End();
        return ((h & 0x20) != 0 ? "instance " : "") + type + "(" + string.Join(", ", ps) + ")";
    }

    public List<string> Locals()
    {
        if (U8() != 0x07) { P--; throw Bad("not a local signature"); }
        uint count = CU();
        var ls = new List<string>();
        for (uint i = 0; i < count; i++) ls.Add(Type(0, allowPinned: true));
        End();
        return ls;
    }

    public List<string> MethodSpec()
    {
        if (U8() != 0x0A) { P--; throw Bad("not a MethodSpec signature"); }
        uint count = CU();
        if (count == 0) { P = 1; throw Bad("MethodSpec without arguments"); }
        var args = new List<string>();
        for (uint i = 0; i < count; i++) args.Add(Type(0));
        End();
        return args;
    }

    public string TypeSpec(int depth)
    {
        string s = Type(depth);
        End();
        return s;
    }
}
