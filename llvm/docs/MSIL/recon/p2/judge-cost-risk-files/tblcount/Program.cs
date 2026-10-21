// tblcount: print the non-empty ECMA-335 tables and heap sizes of a PE/netmodule (System.Reflection.Metadata only).
using System; using System.IO; using System.Linq;
using System.Reflection.Metadata; using System.Reflection.Metadata.Ecma335; using System.Reflection.PortableExecutable;
class P {
  static int Main(string[] a) {
    foreach (var f in a) {
      using var s = File.OpenRead(f); using var pe = new PEReader(s);
      var md = pe.GetMetadataReader();
      var used = Enum.GetValues<TableIndex>().Distinct().Where(t => md.GetTableRowCount(t) > 0)
                     .Select(t => $"{t}={md.GetTableRowCount(t)}").ToArray();
      int il = 0; foreach (var h in md.MethodDefinitions) { var m = md.GetMethodDefinition(h); if (m.RelativeVirtualAddress != 0) il += pe.GetMethodBody(m.RelativeVirtualAddress).Size; }
      Console.WriteLine($"{Path.GetFileName(f)}: {used.Length} tables: {string.Join(' ', used)}");
      Console.WriteLine($"  heaps: Strings={md.GetHeapSize(HeapIndex.String)} Blob={md.GetHeapSize(HeapIndex.Blob)} US={md.GetHeapSize(HeapIndex.UserString)} GUID={md.GetHeapSize(HeapIndex.Guid)}; method bodies {il} bytes; file {new FileInfo(f).Length} bytes");
    }
    return 0;
  }
}
