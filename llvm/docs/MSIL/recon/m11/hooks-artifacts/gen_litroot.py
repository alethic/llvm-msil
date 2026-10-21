"""Make a private lit root: lit.site.cfg copies whose obj roots (and so the
test exec roots / Output dirs) point into a scratch directory, while every tool
and library path still points at the shared build tree. Nothing in the shared
build tree is written.

  python gen_litroot.py <build> <private-root>
then
  python <private-root>/bin/llvm-lit.py -sv D:/llvm-msil/llvm/test/...
"""
import os
import re
import sys

build = os.path.abspath(sys.argv[1])
root = os.path.abspath(sys.argv[2])

# (site cfg relative to build, names of obj-root variables -> private path relative to root)
SITES = {
    r"test\lit.site.cfg.py": {"llvm_obj_root": ""},
    r"test\Unit\lit.site.cfg.py": {"llvm_obj_root": ""},
    r"tools\clang\test\lit.site.cfg.py": {"llvm_obj_root": "", "clang_obj_root": r"tools\clang"},
}

PATH_RE = re.compile(r'path\(r"([^"]*)"\)')


def rewrite(site_rel, objvars):
    src = os.path.join(build, site_rel)
    dst = os.path.join(root, site_rel)
    base = os.path.dirname(src)
    out = []
    for line in open(src, encoding="utf-8").read().splitlines():
        m = re.match(r"\s*config\.(\w+)\s*=", line)
        if m and m.group(1) in objvars:
            p = os.path.join(root, objvars[m.group(1)]) if objvars[m.group(1)] else root
            line = "config.%s = r\"%s\"" % (m.group(1), os.path.normpath(p))
        else:
            def sub(mm):
                rel = mm.group(1)
                if not rel:
                    return "''"
                return 'r"%s"' % os.path.normpath(os.path.join(base, rel))
            line = PATH_RE.sub(sub, line)
        out.append(line)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    return dst


maps = []
lit_src = open(os.path.join(build, "bin", "llvm-lit.py"), encoding="utf-8").read()
for m in re.finditer(r"map_config\(path\(r'([^']*)'\), path\(r'([^']*)'\)\)", lit_src):
    srccfg = os.path.normpath(os.path.join(build, "bin", m.group(1)))
    site = os.path.normpath(os.path.join(build, "bin", m.group(2)))
    rel = os.path.relpath(site, build)
    if rel in SITES:
        site = rewrite(rel, SITES[rel])
    maps.append((srccfg, site))

# CMakeCache.txt: llvm/test/CodeGen/MSIL/lit.local.cfg reads it from llvm_obj_root.
import shutil
shutil.copyfile(os.path.join(build, "CMakeCache.txt"), os.path.join(root, "CMakeCache.txt"))

os.makedirs(os.path.join(root, "bin"), exist_ok=True)
llvm_src = os.path.normpath(os.path.join(build, "..", "llvm"))
with open(os.path.join(root, "bin", "llvm-lit.py"), "w", encoding="utf-8") as f:
    f.write("import os, sys\nconfig_map = {}\n")
    f.write("def map_config(s, c):\n    config_map[os.path.normcase(os.path.abspath(s))] = os.path.normpath(c)\n")
    for s, c in maps:
        f.write("map_config(r'%s', r'%s')\n" % (s, c))
    f.write("builtin_parameters = {'build_mode': '.', 'config_map': config_map}\n")
    f.write("sys.path.insert(0, os.path.join(r'%s', 'utils', 'lit'))\n" % llvm_src)
    f.write("if __name__ == '__main__':\n    from lit.main import main\n    main(builtin_parameters)\n")
print("private lit root:", root)
for s, c in maps:
    print("  ", s, "->", c)
