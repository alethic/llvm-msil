"""Generate the library images of lld/test/MSIL/shared-checks.test as yaml2obj YAML.

Usage: gen.py [--ilasm PATH] [--out DIR] [--work DIR] [--only NAME ...]
              [--check --yaml2obj PATH --llvm-cil-dump PATH]

shared-checks.test links programs against libraries whose symbol table
(ImplementationPlan.md 5.9.15) or export class is wrong in one way each:
another triple, ABI version, data model, table version, C runtime, a table
that does not decode, and export tables that do not match the metadata
(M22.c item 2). msil-ld never writes such a library, so this script
assembles zl.il with ilasm ('-dll -pe64 -x64', the envelope of msil-ld's
x86_64 outputs), puts the payload that symtab.py encodes in place of
@BLOB@ (the custom-attribute value of the Assembly row), and converts each
image into zl-<variant>.yaml with lld/test/MSIL/Inputs/fakepack/gen.py's
converter, which also checks the yaml2obj copy (--check). No binary is
committed; the test runs 'yaml2obj zl-<variant>.yaml -o <dir>/zl.dll'.

Every table but zl-nocls's lists the export class Contoso.Zl.Native and the
data symbol counter (4 bytes, alignment 4); zl-nocls's lists no export class
and no data. TARGETS gives each variant's '#define' lines for zl.il and its
symtab.py options. Regenerating changes each image's MVID (ilasm makes a new
one each time); no test depends on it.
"""

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

spec = importlib.util.spec_from_file_location(
    "fakepack_gen", os.path.join(HERE, os.pardir, "fakepack", "gen.py"))
fakepack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fakepack)

TABLE = ["--class", "Contoso.Zl.Native", "--data", "counter:4:2:0"]
MISSING = ["--class", "Contoso.Zl.Missing", "--data", "counter:4:2:0"]

# (output name, zl.il defines, symtab.py options, description)
TARGETS = [
    ("zl-ok", [], TABLE, "a valid table"),
    ("zl-vendor", [], TABLE + ["--triple", "x86_64-unknown-windows-msvc-msil"], "another vendor"),
    ("zl-envver", [], TABLE + ["--triple", "x86_64-pc-windows-msvc19.33.0-msil"],
     "an environment version"),
    ("zl-minor", [], TABLE + ["--minor", "7"], "table version 1.7"),
    ("zl-gnu", [], TABLE + ["--triple", "x86_64-w64-windows-gnu-msil"], "the windows-gnu triple"),
    ("zl-arm", [], TABLE + ["--triple", "aarch64-pc-windows-msvc-msil"], "the aarch64 triple"),
    ("zl-linux", [], TABLE + ["--triple", "x86_64-unknown-linux-gnu-msil"], "the linux-gnu triple"),
    ("zl-abi", [], TABLE + ["--abi", "2"], "toolchain ABI version 2"),
    ("zl-model", [], TABLE + ["--dm", "8,8,4,8"], "data model 8,8,4,8"),
    ("zl-major", [], TABLE + ["--major", "2"], "table version 2.0"),
    ("zl-ucrt", [], TABLE + ["--runtime", "ucrt"], "C runtime ucrt"),
    ("zl-magic", [], TABLE + ["--magic", "MSYX"], "magic MSYX"),
    ("zl-trailing", [], TABLE + ["--trailing", "00"], "a trailing byte"),
    ("zl-flags", [], TABLE + ["--flags", "2"], "the unknown header flag 0x02"),
    ("zl-noclass", [], MISSING, "a table naming Contoso.Zl.Missing"),
    ("zl-nocls", [], [], "a table with no export class and no data"),
    ("zl-private", ["NOTPUBLIC"], TABLE, "a private export class"),
    ("zl-noaddr", ["NOADDR_FN"], TABLE, "no '<msil>addr_twice'"),
    ("zl-noprop", ["NOPROP"], TABLE, "no property counter"),
]


def run(cmd):
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if proc.returncode:
        sys.exit("gen.py: %s failed:\n%s" % (" ".join(cmd), proc.stdout.decode("utf-8", "replace")))
    return proc.stdout.decode("utf-8")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ilasm", default=fakepack.default_ilasm())
    p.add_argument("--out", default=HERE)
    p.add_argument("--work", help="keep the assembled images here (default: a temporary directory)")
    p.add_argument("--only", nargs="+", help="generate only these outputs")
    p.add_argument("--check", action="store_true")
    p.add_argument("--yaml2obj", default="yaml2obj")
    p.add_argument("--llvm-cil-dump", default="llvm-cil-dump")
    a = p.parse_args()
    if not a.ilasm:
        sys.exit("gen.py: no ilasm found; pass --ilasm")
    work = a.work or tempfile.mkdtemp(prefix="shared-")
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(HERE, "zl.il"), encoding="utf-8") as f:
        source = f.read()
    for name, defines, table, what in TARGETS:
        if a.only and name not in a.only:
            continue
        blob = run([sys.executable, os.path.join(HERE, "symtab.py"), "--format", "il"] + table).strip()
        il = os.path.join(work, name + ".il")
        with open(il, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join("#define %s\n" % d for d in defines) + source.replace("@BLOB@", blob))
        image = os.path.join(work, name + ".dll")
        run([a.ilasm, "-nologo", "-quiet", "-dll", "-pe64", "-x64", il, "-output=" + image])
        with open(image, "rb") as f:
            data = bytearray(f.read())
        how = "ilasm -dll -pe64 -x64 zl.il"
        if defines:
            how += " with #define " + " ".join(defines)
        how += ", symtab.py " + (" ".join(table) if table else "without options")
        header = ("# Generated by gen.py (%s: %s).\n"
                  "# Do not edit: change the source and run gen.py again." % (what, how))
        yaml_path = os.path.join(a.out, name + ".yaml")
        with open(yaml_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(fakepack.pe_to_yaml(data, header))
        if a.check:
            copy = os.path.join(work, name + ".copy.dll")
            run([a.yaml2obj, yaml_path, "-o", copy])
            for mode in ("--tables", "--headers"):
                want = fakepack.comparable(run([a.llvm_cil_dump, mode, image]).encode("utf-8"))
                got = fakepack.comparable(run([a.llvm_cil_dump, mode, copy]).encode("utf-8"))
                if want != got:
                    sys.exit("gen.py: %s: %s of the yaml2obj copy differs from ilasm's image" % (name, mode))
        print("%s.yaml" % name)


if __name__ == "__main__":
    main()
