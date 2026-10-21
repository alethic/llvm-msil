"""Generates the AssemblyRef binding experiments (one .il per variant).

Every variant calls the Phase-1 BCL members (Marshal::AllocHGlobal,
Marshal::StringToHGlobalAnsi, Environment::GetCommandLineArgs,
Buffer::MemoryCopy, Interlocked::MemoryBarrier) and has a value type that
extends System.ValueType, like the data image of D12; only the assembly each
member is bound to (and that assembly's version and public key token)
changes. Exit status 42 means every call worked.
"""
import os, sys

MSCORLIB = "mscorlib", "B7 7A 5C 56 19 34 E0 89", "4:0:0:0"
NETSTD20 = "netstandard", "CC 7B 13 FF CD 2D DD 51", "2:0:0:0"
def ms(name, ver):  # Microsoft's b03f5f7f11d50a3a key
    return name, "B0 3F 5F 7F 11 D5 0A 3A", ver

# scope per member: (ValueType, Marshal, Environment, Buffer, Interlocked)
def same(a):
    return dict(vt=a, marshal=a, env=a, buf=a, ilk=a, nm=None)

VARIANTS = {
    # Phase 1 today (D10)
    "v01-mscorlib": same(MSCORLIB),
    # Bound against the .NET 8 reference pack (definitions, not facades)
    "v02-net8refs": dict(vt=ms("System.Runtime", "8:0:0:0"),
                         marshal=ms("System.Runtime.InteropServices", "8:0:0:0"),
                         env=ms("System.Runtime", "8:0:0:0"),
                         buf=ms("System.Runtime", "8:0:0:0"),
                         ilk=ms("System.Threading", "8:0:0:0"), nm=None),
    # Bound against the .NET 10 reference pack
    "v03-net10refs": dict(vt=ms("System.Runtime", "10:0:0:0"),
                          marshal=ms("System.Runtime.InteropServices", "10:0:0:0"),
                          env=ms("System.Runtime", "10:0:0:0"),
                          buf=ms("System.Runtime", "10:0:0:0"),
                          ilk=ms("System.Threading", "10:0:0:0"), nm=None),
    # Bound against the netstandard 2.0 reference set
    "v04-netstandard20": same(NETSTD20),
    # Wrong defining assembly: Marshal through System.Runtime 8.0.0.0
    "v05-marshal-via-sysruntime": dict(vt=ms("System.Runtime", "8:0:0:0"),
                                       marshal=ms("System.Runtime", "8:0:0:0"),
                                       env=ms("System.Runtime", "8:0:0:0"),
                                       buf=ms("System.Runtime", "8:0:0:0"),
                                       ilk=ms("System.Threading", "8:0:0:0"), nm=None),
    # mscorlib with a wrong public key token
    "v06-mscorlib-badpkt": same(("mscorlib", "00 11 22 33 44 55 66 77", "4:0:0:0")),
    # mscorlib without a public key token
    "v07-mscorlib-nopkt": same(("mscorlib", None, "4:0:0:0")),
    # mscorlib facade asked for a type it does not forward (NativeMemory)
    "v08-mscorlib-nativememory": dict(same(MSCORLIB), nm=MSCORLIB),
    # Framework-era facade versions (what the GAC holds)
    "v09-facades-4.0": dict(vt=ms("System.Runtime", "4:0:0:0"),
                            marshal=ms("System.Runtime.InteropServices", "4:0:0:0"),
                            env=ms("System.Runtime.Extensions", "4:0:0:0"),
                            buf=ms("System.Runtime", "4:0:0:0"),
                            ilk=ms("System.Threading", "4:0:0:0"), nm=None),
    # mscorlib asked for a version above the one the runtime has
    "v10-mscorlib-ver5": same(("mscorlib", "B7 7A 5C 56 19 34 E0 89", "5:0:0:0")),
    # NativeMemory bound correctly (net8 refs): .NET only
    "v11-net8-nativememory": dict(vt=ms("System.Runtime", "8:0:0:0"),
                                  marshal=ms("System.Runtime.InteropServices", "8:0:0:0"),
                                  env=ms("System.Runtime", "8:0:0:0"),
                                  buf=ms("System.Runtime", "8:0:0:0"),
                                  ilk=ms("System.Threading", "8:0:0:0"),
                                  nm=ms("System.Runtime.InteropServices", "8:0:0:0")),
    # Mixed: mscorlib for everything, plus one modern-only API bound to
    # System.Runtime.InteropServices 8.0 (a switch-gated API, Design 7.1)
    "v12-mixed-mscorlib+nm8": dict(same(MSCORLIB),
                                   nm=ms("System.Runtime.InteropServices", "8:0:0:0")),
    # Non-mscorlib reference with a wrong public key token
    "v13-sri8-badpkt": dict(vt=ms("System.Runtime", "8:0:0:0"),
                            marshal=("System.Runtime.InteropServices", "00 11 22 33 44 55 66 77", "8:0:0:0"),
                            env=ms("System.Runtime", "8:0:0:0"),
                            buf=ms("System.Runtime", "8:0:0:0"),
                            ilk=ms("System.Threading", "8:0:0:0"), nm=None),
    "v14-facades-4.0-badpkt": dict(vt=MSCORLIB, marshal=("System.Runtime.InteropServices", "00 11 22 33 44 55 66 77", "4:0:0:0"),
                                   env=MSCORLIB, buf=MSCORLIB, ilk=MSCORLIB, nm=None),
    # netstandard 2.1 (Framework has only the 2.0 facade)
    "v15-netstandard21": same(("netstandard", "CC 7B 13 FF CD 2D DD 51", "2:1:0:0")),
    # System.Runtime facade at the net48 reference-pack version (4.1.2.0)
    "v16-facades-net48ref": dict(vt=ms("System.Runtime", "4:1:2:0"),
                                 marshal=ms("System.Runtime.InteropServices", "4:1:2:0"),
                                 env=ms("System.Runtime.Extensions", "4:1:2:0"),
                                 buf=ms("System.Runtime", "4:1:2:0"),
                                 ilk=ms("System.Threading", "4:0:11:0"), nm=None),
}

def alias(a):
    return "'" + a[0] + "#" + a[2].replace(":", "_") + "#" + (a[1] or "none").replace(" ", "") + "'"

def emit(name, v):
    refs = {}
    for k in ("vt", "marshal", "env", "buf", "ilk", "nm"):
        a = v[k]
        if a is not None:
            refs[alias(a)] = a
    out = []
    for al, (asm, pkt, ver) in refs.items():
        out.append(f".assembly extern {asm} as {al}")
        out.append("{")
        if pkt:
            out.append(f"  .publickeytoken = ({pkt} )")
        out.append(f"  .ver {ver}")
        out.append("}")
    out.append(f".assembly '{name}' {{ .ver 0:0:0:0 }}")
    S = lambda k: "[" + alias(v[k]) + "]"
    out.append(f".class private explicit ansi sealed 'img_t' extends {S('vt')}System.ValueType {{ .pack 1 .size 16 }}")
    out.append(".method public static int32 main() cil managed")
    out.append("{")
    out.append("  .entrypoint")
    out.append("  .maxstack 4")
    out.append("  .locals (native int, native int)")
    out.append("  ldc.i4 64")
    out.append("  conv.i")
    out.append(f"  call native int {S('marshal')}System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)")
    out.append("  stloc.0")
    out.append(f"  call string[] {S('env')}System.Environment::GetCommandLineArgs()")
    out.append("  ldc.i4.0")
    out.append("  ldelem.ref")
    out.append(f"  call native int {S('marshal')}System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)")
    out.append("  stloc.1")
    out.append("  ldloc.1")
    out.append("  ldloc.0")
    out.append("  ldc.i4 8")
    out.append("  conv.i8")
    out.append("  dup")
    out.append(f"  call void {S('buf')}System.Buffer::MemoryCopy(void*, void*, int64, int64)")
    out.append(f"  call void {S('ilk')}System.Threading.Interlocked::MemoryBarrier()")
    if v["nm"] is not None:
        out.append("  ldc.i4 16")
        out.append("  conv.u")
        out.append(f"  call void* {S('nm')}System.Runtime.InteropServices.NativeMemory::Alloc(native uint)")
        out.append("  pop")
    out.append("  ldc.i4.s 42")
    out.append("  ret")
    out.append("}")
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    outdir = sys.argv[1]
    for n, v in VARIANTS.items():
        with open(os.path.join(outdir, n + ".il"), "w", newline="\n") as f:
            f.write(emit(n, v))
    print("\n".join(VARIANTS))
