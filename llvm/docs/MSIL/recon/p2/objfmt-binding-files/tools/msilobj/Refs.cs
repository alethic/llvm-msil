// Refs.cs: bind BCL type references by searching reference assemblies (the P2-B mechanism), so that
// an object's [mscorlib] (or any placeholder) TypeRef becomes a TypeRef in whichever reference
// assembly defines the type. It is the same "bind by name at link time" step as cross-object symbols.
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;

class RefIndex
{
    public record Asm(string Name, Version Version, byte[] PublicKeyToken, AssemblyFlags Flags);
    readonly Dictionary<(string, string), Asm> types = new();
    public int Assemblies;

    public void Add(string path)
    {
        var files = Directory.Exists(path) ? Directory.GetFiles(path, "*.dll").Concat(Directory.Exists(Path.Combine(path, "Facades")) ? Directory.GetFiles(Path.Combine(path, "Facades"), "*.dll") : Array.Empty<string>()) : new[] { path };
        foreach (var f in files)
        {
            try
            {
                using var pe = new PEReader(File.OpenRead(f));
                if (!pe.HasMetadata) continue;
                var md = pe.GetMetadataReader();
                if (!md.IsAssembly) continue;
                var ad = md.GetAssemblyDefinition();
                byte[] pk = md.GetBlobBytes(ad.PublicKey), tok = Array.Empty<byte>();
                if (pk.Length > 0) { var h = SHA1.HashData(pk); tok = h[^8..]; Array.Reverse(tok); }
                var asm = new Asm(md.GetString(ad.Name), ad.Version, tok, 0);
                Assemblies++;
                foreach (var th in md.TypeDefinitions)
                {
                    var t = md.GetTypeDefinition(th);
                    if ((t.Attributes & TypeAttributes.VisibilityMask) != TypeAttributes.Public) continue;
                    types.TryAdd((md.GetString(t.Namespace), md.GetString(t.Name)), asm);   // a definition beats a forwarder
                }
            }
            catch (BadImageFormatException) { }
        }
    }

    public Asm? Find(string ns, string name) => types.TryGetValue((ns, name), out var a) ? a : null;
}
