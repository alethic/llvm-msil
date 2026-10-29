// Survey: which BCL members do reference-assembly sets define? Follows type forwarders.
using System.Collections.Immutable;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

class Sig : ISignatureTypeProvider<string, object> {
  public string GetPrimitiveType(PrimitiveTypeCode t) => t switch {
    PrimitiveTypeCode.Boolean => "bool", PrimitiveTypeCode.Byte => "uint8", PrimitiveTypeCode.SByte => "int8",
    PrimitiveTypeCode.Int16 => "int16", PrimitiveTypeCode.UInt16 => "uint16", PrimitiveTypeCode.Int32 => "int32",
    PrimitiveTypeCode.UInt32 => "uint32", PrimitiveTypeCode.Int64 => "int64", PrimitiveTypeCode.UInt64 => "uint64",
    PrimitiveTypeCode.Single => "float32", PrimitiveTypeCode.Double => "float64", PrimitiveTypeCode.IntPtr => "native int",
    PrimitiveTypeCode.UIntPtr => "native uint", PrimitiveTypeCode.Void => "void", PrimitiveTypeCode.String => "string",
    PrimitiveTypeCode.Object => "object", PrimitiveTypeCode.Char => "char", _ => t.ToString() };
  public string GetTypeFromDefinition(MetadataReader r, TypeDefinitionHandle h, byte k) { var d = r.GetTypeDefinition(h); return r.GetString(d.Namespace) + "." + r.GetString(d.Name); }
  public string GetTypeFromReference(MetadataReader r, TypeReferenceHandle h, byte k) { var d = r.GetTypeReference(h); return r.GetString(d.Namespace) + "." + r.GetString(d.Name); }
  public string GetTypeFromSpecification(MetadataReader r, object c, TypeSpecificationHandle h, byte k) => "spec";
  public string GetSZArrayType(string e) => e + "[]";
  public string GetArrayType(string e, ArrayShape s) => e + "[,]";
  public string GetByReferenceType(string e) => e + "&";
  public string GetPointerType(string e) => e + "*";
  public string GetGenericInstantiation(string g, ImmutableArray<string> a) => g + "<" + string.Join(",", a) + ">";
  public string GetGenericTypeParameter(object c, int i) => "!" + i;
  public string GetGenericMethodParameter(object c, int i) => "!!" + i;
  public string GetFunctionPointerType(MethodSignature<string> s) => "fnptr";
  public string GetModifiedType(string m, string u, bool req) => u;
  public string GetPinnedType(string e) => e;
}

class Program {
  record Asm(string Path, MetadataReader R, PEReader P);
  static void Main(string[] args) {
    // args[0]: file of sets "name|dir"; args[1]: file of queries "Ns.Type|Member|sig" (sig "" for type only; "*" any)
    var sets = File.ReadAllLines(args[0]).Where(l => l.Trim().Length > 0 && !l.StartsWith("#")).Select(l => l.Split('|')).ToList();
    var queries = File.ReadAllLines(args[1]).Where(l => l.Trim().Length > 0 && !l.StartsWith("#")).Select(l => l.Split('|')).ToList();
    var results = new Dictionary<string, Dictionary<string, string>>();
    foreach (var s in sets) {
      var defs = new Dictionary<string, (Asm, TypeDefinitionHandle)>();
      foreach (var f in s[1].Split(';').SelectMany(d => Directory.Exists(d) ? Directory.GetFiles(d, "*.dll") : new[] { d })) {
        try {
          var pe = new PEReader(File.OpenRead(f)); if (!pe.HasMetadata) continue;
          var r = pe.GetMetadataReader(); var a = new Asm(f, r, pe);
          foreach (var th in r.TypeDefinitions) {
            var td = r.GetTypeDefinition(th);
            if (td.GetDeclaringType().IsNil == false) continue;
            if ((td.Attributes & System.Reflection.TypeAttributes.VisibilityMask) != System.Reflection.TypeAttributes.Public) continue;
            var n = r.GetString(td.Namespace) + "." + r.GetString(td.Name);
            defs.TryAdd(n, (a, th));
          }
        } catch { }
      }
      var col = new Dictionary<string, string>();
      foreach (var q in queries) {
        string key = q[0] + "::" + q[1] + (q.Length > 2 && q[2].Length > 0 ? "(" + q[2] + ")" : "");
        if (!defs.TryGetValue(q[0], out var t)) { col[key] = "-"; continue; }
        if (q[1].Length == 0) { col[key] = Path.GetFileNameWithoutExtension(t.Item1.Path); continue; }
        var r = t.Item1.R; var td = r.GetTypeDefinition(t.Item2); bool found = false;
        foreach (var mh in td.GetMethods()) {
          var md = r.GetMethodDefinition(mh); if (r.GetString(md.Name) != q[1]) continue;
          if ((md.Attributes & System.Reflection.MethodAttributes.MemberAccessMask) != System.Reflection.MethodAttributes.Public) continue;
          var sig = md.DecodeSignature(new Sig(), null);
          var ps = string.Join(",", sig.ParameterTypes);
          if (q.Length < 3 || q[2] == "*" || q[2] == ps) { found = true; break; }
        }
        foreach (var fh in td.GetFields()) { var fd = r.GetFieldDefinition(fh); if (r.GetString(fd.Name) == q[1]) found = true; }
        col[key] = found ? Path.GetFileNameWithoutExtension(t.Item1.Path) : "-";
      }
      results[s[0]] = col;
    }
    var names = sets.Select(s => s[0]).ToList();
    Console.WriteLine("| Member | " + string.Join(" | ", names) + " |");
    Console.WriteLine("|---|" + string.Join("|", names.Select(_ => "---")) + "|");
    foreach (var key in results[names[0]].Keys)
      Console.WriteLine("| `" + key + "` | " + string.Join(" | ", names.Select(n => results[n][key])) + " |");
  }
}
