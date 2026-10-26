"""Generate lld/test/MSIL's synthetic reference assemblies as yaml2obj YAML.

Usage: gen.py [--ilasm PATH] [--out DIR] [--work DIR] [--only NAME ...]
              [--check --yaml2obj PATH --llvm-cil-dump PATH]

msil-ld binds BCL types and members by searching reference assemblies
(ImplementationPlan.md 5.11, M21). Real reference packs cannot be checked in,
so the binding tests (lld/test/MSIL/bcl-*.test, and M22's tests) link against
small assemblies assembled with ilasm from the .il sources in this directory
(M21.d; bcl-binding 7.5). No binary is committed: this script assembles each
source once and converts the image into yaml2obj YAML (<name>.yaml here),
and the tests run 'yaml2obj <name>.yaml -o <dir>/<Assembly>.dll'.

TARGETS lists every output: its source, the '#define' lines prepended to the
source (ilasm's preprocessor builds the variants of FakeCore.il,
FakeInterop.il, FakeThreading.il, FakeBarrierBase.il, FakeTypesB.il,
FakeNativeLibrary.il, MiniCore.il, named-obj.il and field-obj.il), and
whether it is a library ('-dll', PE32 AnyCPU like the assemblies of the .NET
reference packs) or a CIL object ('-dll -pe64 -x64', the envelope of
%ilasm-verify and of llc -filetype=obj).

PATCHES lists the outputs whose assembled image is changed before the
conversion, for metadata that ilasm cannot write: BaseCore's System.Object
has a base type (ilasm drops 'extends' on System.Object under '.mscorlib'),
so the TypeDef table's Extends column is rewritten (set_extends).

A core library other than mscorlib: by Roslyn's rule it must have no
AssemblyRef, and ilasm derives every class without its own base from
[mscorlib]System.Object and adds that AssemblyRef whatever the assembly is
called (bcl-binding 7.5, which suggests csc -nostdlib instead). The
'.mscorlib' directive avoids it: FakeCore.il declares the module to be the
one that defines the core types (Framework ilasm 4.8 and ilasm 10.0.12 both
write no AssemblyRef then). The other assemblies declare
'.assembly extern FakeCore as mscorlib', so the base ilasm adds is
[FakeCore]System.Object.

The YAML holds the optional header, the data directories and each section's
virtual address, virtual size and raw contents (up to the virtual size), the
fields yaml2obj needs to lay out an image with the same RVAs; yaml2obj picks
the file layout. No obj2yaml is needed. --check runs yaml2obj on each YAML
written and requires 'llvm-cil-dump --tables' and '--headers' of the copy to
equal those of ilasm's image (after the patch, for PATCHES), except the COFF
line (timestamp and symbol count) and the sections' raw offsets and sizes,
which yaml2obj chooses.

Regenerating changes each image's MVID (ilasm makes a new one each time);
no test depends on it.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# (output name, source, defines, kind)
TARGETS = [
    ("FakeCore", "FakeCore.il", [], "dll"),
    ("FakeCoreNoCtor", "FakeCore.il", ["NOCTOR"], "dll"),
    ("FakeCoreNoInAttr", "FakeCore.il", ["NOINATTR"], "dll"),
    ("FakeInterop", "FakeInterop.il", [], "dll"),
    ("FakeInteropMods", "FakeInterop.il", ["MODS"], "dll"),
    ("FakeInteropV9", "FakeInterop.il", ["V9"], "dll"),
    ("FakeInteropK2", "FakeInterop.il", ["K2"], "dll"),
    ("FakeInteropExtra", "FakeInterop.il", ["EXTRA"], "dll"),
    ("FakeThreading", "FakeThreading.il", ["PLAIN"], "dll"),
    ("FakeThreadingNoBarrier", "FakeThreading.il", [], "dll"),
    ("FakeThreadingRet", "FakeThreading.il", ["RET", "PLAIN"], "dll"),
    ("FakeThreadingOverloads", "FakeThreading.il", ["OVERLOADS"], "dll"),
    ("FakeThreadingInstance", "FakeThreading.il", ["INSTANCE"], "dll"),
    ("FakeThreadingBase", "FakeThreading.il", ["BASE"], "dll"),
    ("FakeThreadingObsolete", "FakeThreading.il", ["OBSOLETE"], "dll"),
    ("FakeThreadingPreview", "FakeThreading.il", ["PREVIEW"], "dll"),
    ("FakeThreadingTwo", "FakeThreading.il", ["TWO", "PLAIN"], "dll"),
    ("FakeThreadingFr", "FakeThreading.il", ["FR", "PLAIN"], "dll"),
    ("System.Private.FakeLib", "FakeThreading.il", ["PRIVATE", "PLAIN"], "dll"),
    ("System.Private.FakeLibRef", "FakeThreading.il", ["PRIVREF", "PLAIN"], "dll"),
    ("FakeThreadingImpl", "FakeThreading.il", ["NOREFASM", "PLAIN"], "dll"),
    ("FakeThreadingGeneric", "FakeThreading.il", ["GENERIC"], "dll"),
    ("FakeThreadingVararg", "FakeThreading.il", ["VARARG"], "dll"),
    ("FakeThreadingNonPublic", "FakeThreading.il", ["NONPUBLIC"], "dll"),
    ("FakeThreadingHidden", "FakeThreading.il", ["BASE", "NONPUBLIC"], "dll"),
    ("FakeThreadingHiddenMod", "FakeThreading.il", ["BASE", "NONPUBMOD"], "dll"),
    ("FakeThreadingXBase", "FakeThreading.il", ["XBASE"], "dll"),
    ("FakeBarrierBase", "FakeBarrierBase.il", [], "dll"),
    ("FakeBarrierBaseCopy", "FakeBarrierBase.il", ["COPY"], "dll"),
    ("FakeNativeLibrary", "FakeNativeLibrary.il", [], "dll"),
    ("FakeNativeLibraryLoadOnly", "FakeNativeLibrary.il", ["LOADONLY"], "dll"),
    ("FakeNativeLibraryExportOnly", "FakeNativeLibrary.il", ["EXPORTONLY"], "dll"),
    ("FakeNativeLibraryWrongSig", "FakeNativeLibrary.il", ["WRONGSIG"], "dll"),
    ("FakeNativeLibraryPrivExport", "FakeNativeLibrary.il", ["PRIVEXPORT"], "dll"),
    ("FakeFacade", "FakeFacade.il", [], "dll"),
    ("FakeMscorlib", "FakeMscorlib.il", [], "dll"),
    ("IlasmCore", "IlasmCore.il", [], "dll"),
    ("CoreA", "MiniCore.il", ["CORE_A"], "dll"),
    ("CoreB", "MiniCore.il", ["CORE_B"], "dll"),
    ("CoreC", "MiniCore.il", ["CORE_C"], "dll"),
    ("BaseCore", "MiniCore.il", ["BASETYPE"], "dll"),
    ("FakeTypesA", "FakeTypesA.il", [], "dll"),
    ("FakeTypesB", "FakeTypesB.il", [], "dll"),
    ("FakeTypesC", "FakeTypesB.il", ["TYPES_C"], "dll"),
    ("ToolLib", "ToolLib.il", [], "dll"),
    ("named-obj", "named-obj.il", [], "obj"),
    ("named-obj-vararg", "named-obj.il", ["VARARG"], "obj"),
    ("FakeFields", "FakeFields.il", [], "dll"),
    ("field-obj-s", "field-obj.il", ["FIELD_S"], "obj"),
    ("field-obj-i", "field-obj.il", ["FIELD_I"], "obj"),
    ("field-obj-p", "field-obj.il", ["FIELD_P"], "obj"),
    ("field-obj-q", "field-obj.il", ["FIELD_Q"], "obj"),
    ("field-obj-m", "field-obj.il", ["FIELD_M"], "obj"),
    ("field-obj-base", "field-obj.il", ["FIELD_BASE"], "obj"),
]

# output name -> (description for the YAML header, function(bytearray) that
# patches the assembled image in place)
PATCHES = {
    "BaseCore": ("System.Object's Extends set to Fake.Root and Fake.Root's to 0",
                 lambda image: set_extends(image, {("System", "Object"): ("Fake", "Root"),
                                                   ("Fake", "Root"): None})),
}

MACHINES = {0x14C: "IMAGE_FILE_MACHINE_I386", 0x8664: "IMAGE_FILE_MACHINE_AMD64",
            0xAA64: "IMAGE_FILE_MACHINE_ARM64"}
SUBSYSTEMS = {2: "IMAGE_SUBSYSTEM_WINDOWS_GUI", 3: "IMAGE_SUBSYSTEM_WINDOWS_CUI"}
FILE_FLAGS = [(0x0001, "IMAGE_FILE_RELOCS_STRIPPED"), (0x0002, "IMAGE_FILE_EXECUTABLE_IMAGE"),
              (0x0004, "IMAGE_FILE_LINE_NUMS_STRIPPED"), (0x0008, "IMAGE_FILE_LOCAL_SYMS_STRIPPED"),
              (0x0020, "IMAGE_FILE_LARGE_ADDRESS_AWARE"), (0x0100, "IMAGE_FILE_32BIT_MACHINE"),
              (0x0200, "IMAGE_FILE_DEBUG_STRIPPED"), (0x2000, "IMAGE_FILE_DLL")]
DLL_FLAGS = [(0x0020, "IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA"),
             (0x0040, "IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE"),
             (0x0100, "IMAGE_DLL_CHARACTERISTICS_NX_COMPAT"),
             (0x0400, "IMAGE_DLL_CHARACTERISTICS_NO_SEH"),
             (0x8000, "IMAGE_DLL_CHARACTERISTICS_TERMINAL_SERVER_AWARE")]
SECTION_FLAGS = [(0x00000020, "IMAGE_SCN_CNT_CODE"), (0x00000040, "IMAGE_SCN_CNT_INITIALIZED_DATA"),
                 (0x00000080, "IMAGE_SCN_CNT_UNINITIALIZED_DATA"),
                 (0x02000000, "IMAGE_SCN_MEM_DISCARDABLE"), (0x10000000, "IMAGE_SCN_MEM_SHARED"),
                 (0x20000000, "IMAGE_SCN_MEM_EXECUTE"), (0x40000000, "IMAGE_SCN_MEM_READ"),
                 (0x80000000, "IMAGE_SCN_MEM_WRITE")]
DIRECTORIES = ["ExportTable", "ImportTable", "ResourceTable", "ExceptionTable", "CertificateTable",
               "BaseRelocationTable", "Debug", "Architecture", "GlobalPtr", "TlsTable",
               "LoadConfigTable", "BoundImport", "IAT", "DelayImportDescriptor", "ClrRuntimeHeader"]


def fail(msg):
    sys.exit("gen.py: " + msg)


def flag_names(value, table, what):
    names = [name for bit, name in table if value & bit]
    rest = value & ~sum(bit for bit, _ in table)
    if rest:
        fail("unknown %s flags 0x%X" % (what, rest))
    return "[ " + ", ".join(names) + " ]" if names else "[  ]"


def pe_to_yaml(data, header):
    """The yaml2obj COFF document of the PE image in data."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        fail("not a PE image")
    machine, nsec, _, _, _, optsize, chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic not in (0x10B, 0x20B):
        fail("unknown optional header magic 0x%X" % magic)
    plus = magic == 0x20B
    entry = struct.unpack_from("<I", data, opt + 16)[0]
    image_base = struct.unpack_from("<Q" if plus else "<I", data, opt + (24 if plus else 28))[0]
    (sect_align, file_align, maj_os, min_os, maj_img, min_img, maj_sub, min_sub, _, _, _, _,
     subsystem, dll_chars) = struct.unpack_from("<IIHHHHHHIIIIHH", data, opt + 32)
    sizes = struct.unpack_from("<QQQQ" if plus else "<IIII", data, opt + 72)
    dirs_at = opt + (112 if plus else 96)
    ndirs = struct.unpack_from("<I", data, dirs_at - 4)[0]
    if machine not in MACHINES or subsystem not in SUBSYSTEMS:
        fail("unknown machine 0x%X or subsystem %d" % (machine, subsystem))

    out = [header, "--- !COFF", "OptionalHeader:"]
    out += ["  AddressOfEntryPoint: %d" % entry, "  ImageBase:       %d" % image_base,
            "  SectionAlignment: %d" % sect_align, "  FileAlignment:   %d" % file_align,
            "  MajorOperatingSystemVersion: %d" % maj_os, "  MinorOperatingSystemVersion: %d" % min_os,
            "  MajorImageVersion: %d" % maj_img, "  MinorImageVersion: %d" % min_img,
            "  MajorSubsystemVersion: %d" % maj_sub, "  MinorSubsystemVersion: %d" % min_sub,
            "  Subsystem:       %s" % SUBSYSTEMS[subsystem],
            "  DLLCharacteristics: %s" % flag_names(dll_chars, DLL_FLAGS, "DLL"),
            "  SizeOfStackReserve: %d" % sizes[0], "  SizeOfStackCommit: %d" % sizes[1],
            "  SizeOfHeapReserve: %d" % sizes[2], "  SizeOfHeapCommit: %d" % sizes[3]]
    for i in range(min(ndirs, len(DIRECTORIES))):
        rva, size = struct.unpack_from("<II", data, dirs_at + 8 * i)
        if rva or size:
            out += ["  %s:" % DIRECTORIES[i], "    RelativeVirtualAddress: %d" % rva,
                    "    Size:            %d" % size]
    out += ["header:", "  Machine:         %s" % MACHINES[machine],
            "  Characteristics: %s" % flag_names(chars, FILE_FLAGS, "file"), "sections:"]
    for i in range(nsec):
        at = opt + optsize + 40 * i
        name = data[at:at + 8].rstrip(b"\0").decode("ascii")
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        contents = data[rawptr:rawptr + (min(vsize, rawsize) if vsize else rawsize)]
        out += ["  - Name:            %s" % name,
                "    Characteristics: %s" % flag_names(flags, SECTION_FLAGS, "section"),
                "    VirtualAddress:  %d" % va, "    VirtualSize:     %d" % vsize,
                "    SectionData:     %s" % contents.hex().upper()]
    out += ["symbols:         []", "..."]
    return "\n".join(out) + "\n"


def rva_to_offset(data, rva):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec, optsize = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    for i in range(nsec):
        at = pe + 24 + optsize + 40 * i
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, at + 8)
        if va <= rva < va + max(vsize, rawsize):
            return rva - va + rawptr
    fail("RVA 0x%X is in no section" % rva)


def set_extends(data, changes):
    """Rewrite the Extends column (ECMA-335 II.22.37) of TypeDef rows in the
    PE image in data: changes maps (namespace, name) of a TypeDef to the
    (namespace, name) of the TypeDef it is to extend, or to None for no base
    type. Every named TypeDef must exist."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe + 24
    plus = struct.unpack_from("<H", data, opt)[0] == 0x20B
    cli_rva = struct.unpack_from("<I", data, opt + (112 if plus else 96) + 14 * 8)[0]
    md = rva_to_offset(data, struct.unpack_from("<I", data, rva_to_offset(data, cli_rva) + 8)[0])
    if struct.unpack_from("<I", data, md)[0] != 0x424A5342:
        fail("no metadata root")
    at = md + 16 + struct.unpack_from("<I", data, md + 12)[0]  # Flags, then Streams
    count = struct.unpack_from("<H", data, at + 2)[0]
    at += 4
    streams = {}
    for _ in range(count):
        offset = struct.unpack_from("<I", data, at)[0]
        end = data.index(b"\0", at + 8)
        streams[bytes(data[at + 8:end]).decode("ascii")] = md + offset
        at += 8 + ((end - at - 8 + 4) & ~3)  # the name, its NUL and padding to 4
    tables, strings = streams["#~"], streams["#Strings"]
    heaps = data[tables + 6]
    if heaps & 0x40:
        fail("table stream with extra data")
    valid = struct.unpack_from("<Q", data, tables + 8)[0]
    rows, at = {}, tables + 24
    for t in range(64):
        if valid >> t & 1:
            rows[t] = struct.unpack_from("<I", data, at)[0]
            at += 4
    sidx = 4 if heaps & 1 else 2
    gidx = 4 if heaps & 2 else 2

    def coded(ts, bits):
        return 4 if max(rows.get(t, 0) for t in ts) >= 1 << (16 - bits) else 2

    def index(t):
        return 4 if rows.get(t, 0) >= 1 << 16 else 2

    # Module (0x00), TypeRef (0x01), then TypeDef (0x02).
    at += rows.get(0, 0) * (2 + sidx + 3 * gidx)
    at += rows.get(1, 0) * (coded([0x00, 0x1A, 0x23, 0x01], 2) + 2 * sidx)
    ext = coded([0x02, 0x01, 0x1B], 2)
    size = 4 + 2 * sidx + ext + index(0x04) + index(0x06)

    def read(offset, width):
        return struct.unpack_from("<I" if width == 4 else "<H", data, offset)[0]

    def string(i):
        return bytes(data[strings + i:data.index(b"\0", strings + i)]).decode("utf-8")

    names = {}
    for r in range(rows.get(2, 0)):
        row = at + r * size
        names[(string(read(row + 4 + sidx, sidx)), string(read(row + 4, sidx)))] = (r + 1, row)
    for typedef, base in changes.items():
        if typedef not in names or (base is not None and base not in names):
            fail("set_extends: no TypeDef %s or %s" % (typedef, base))
        value = names[base][0] << 2 if base is not None else 0  # tag 0: TypeDef
        struct.pack_into("<I" if ext == 4 else "<H", data, names[typedef][1] + 4 + 2 * sidx, value)


def default_ilasm():
    windir = os.environ.get("WINDIR") or os.environ.get("SystemRoot")
    if windir:
        framework = os.path.join(windir, "Microsoft.NET", "Framework64", "v4.0.30319", "ilasm.exe")
        if os.path.isfile(framework):
            return framework
    return shutil.which("ilasm")


def comparable(dump):
    """llvm-cil-dump output without what yaml2obj lays out on its own: the
    COFF line and the raw offsets and sizes of the section lines."""
    lines = []
    for line in dump.decode("utf-8", "replace").splitlines():
        if line.startswith("COFF: "):
            continue
        if line.startswith("Section: "):
            line = line.split(" raw=")[0]
        lines.append(line)
    return lines


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if proc.returncode:
        fail("%s failed:\n%s" % (" ".join(cmd), proc.stdout.decode("utf-8", "replace")))
    return proc.stdout


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ilasm", default=default_ilasm())
    p.add_argument("--out", default=HERE)
    p.add_argument("--work", help="keep the assembled images here (default: a temporary directory)")
    p.add_argument("--only", nargs="+", help="generate only these outputs")
    p.add_argument("--check", action="store_true")
    p.add_argument("--yaml2obj", default="yaml2obj")
    p.add_argument("--llvm-cil-dump", default="llvm-cil-dump")
    a = p.parse_args()
    if not a.ilasm:
        fail("no ilasm found; pass --ilasm")
    work = a.work or tempfile.mkdtemp(prefix="fakepack-")
    os.makedirs(work, exist_ok=True)
    for name, source, defines, kind in TARGETS:
        if a.only and name not in a.only:
            continue
        with open(os.path.join(HERE, source), encoding="utf-8") as f:
            text = f.read()
        il = os.path.join(work, name + ".il")
        with open(il, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join("#define %s\n" % d for d in defines) + text)
        image = os.path.join(work, name + (".o" if kind == "obj" else ".dll"))
        flags = ["-nologo", "-quiet", "-dll"] + (["-pe64", "-x64"] if kind == "obj" else [])
        run([a.ilasm] + flags + [il, "-output=" + image])
        with open(image, "rb") as f:
            data = bytearray(f.read())
        how = "ilasm %s %s" % (" ".join(flags[2:]), source)
        if defines:
            how += " with #define " + " ".join(defines)
        if name in PATCHES:
            what, patch = PATCHES[name]
            patch(data)
            with open(image, "wb") as f:  # --check compares with the patched image
                f.write(data)
            how += ", then " + what
        header = ("# Generated by gen.py (%s).\n# Do not edit: change the source and run gen.py again."
                  % how)
        yaml_path = os.path.join(a.out, name + ".yaml")
        with open(yaml_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(pe_to_yaml(data, header))
        if a.check:
            copy = os.path.join(work, name + ".copy" + os.path.splitext(image)[1])
            run([a.yaml2obj, yaml_path, "-o", copy])
            for mode in ("--tables", "--headers"):
                want = comparable(run([a.llvm_cil_dump, mode, image]))
                got = comparable(run([a.llvm_cil_dump, mode, copy]))
                if want != got:
                    fail("%s: %s of the yaml2obj copy differs from ilasm's image" % (name, mode))
        print("%s.yaml" % name)


if __name__ == "__main__":
    main()
