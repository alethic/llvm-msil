// Dump sequence points of a Portable PDB next to an ECMA-335 module (object).
using System; using System.IO; using System.Reflection.Metadata; using System.Reflection.PortableExecutable;
var pe = new PEReader(File.OpenRead(args[0])); var md = pe.GetMetadataReader();
using var prov = MetadataReaderProvider.FromPortablePdbStream(File.OpenRead(args[1]));
var pdb = prov.GetMetadataReader();
Console.WriteLine($"module {md.GetString(md.GetModuleDefinition().Name)}  isAssembly={md.IsAssembly}  pdb docs={pdb.Documents.Count} methods={pdb.MethodDebugInformation.Count}");
foreach (var h in pdb.MethodDebugInformation) {
  var mdi = pdb.GetMethodDebugInformation(h); var def = h.ToDefinitionHandle();
  var name = md.GetString(md.GetMethodDefinition(def).Name);
  foreach (var sp in mdi.GetSequencePoints())
    Console.WriteLine($"  {name} (0x{System.Reflection.Metadata.Ecma335.MetadataTokens.GetToken(def):X8}) IL_{sp.Offset:X4} -> {pdb.GetString(pdb.GetDocument(sp.Document).Name)}:{sp.StartLine}:{sp.StartColumn}");
}
