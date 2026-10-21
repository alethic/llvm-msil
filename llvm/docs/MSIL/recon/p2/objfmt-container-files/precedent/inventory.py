# Inventory of object-format support per format: dedicated files (name matches the format) and
# shared files that mention it (outside tests, unittests, llvm/lib/Target and llvm/docs).
import subprocess, re, sys, os, collections
ROOT = r"D:\llvm-msil"
def git(*a):
    return subprocess.run(["git", "-C", ROOT, *a], capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
files = git("ls-files", "llvm/include", "llvm/lib", "llvm/tools", "lld").splitlines()
EXCL = re.compile(r"(/test/|/unittests/|^llvm/lib/Target/|^llvm/docs/)")
files = [f for f in files if not EXCL.search(f) and f.endswith((".cpp", ".h", ".def", ".td", ".txt", ".inc"))]
FORMATS = {
  "wasm": (re.compile(r"wasm", re.I), re.compile(r"(^|/)(wasm|Wasm)[^/]*$|/wasm/|Wasm[A-Z]")),
  "goff": (re.compile(r"GOFF|goff_object|goff_section"), re.compile(r"GOFF")),
  "dxcontainer": (re.compile(r"dxcontainer", re.I), re.compile(r"DXContainer|dxcontainer")),
  "xcoff": (re.compile(r"xcoff", re.I), re.compile(r"XCOFF")),
  "spirv": (re.compile(r"spirv", re.I), re.compile(r"SPIRV")),
}
def wc(f):
    try:
        with open(os.path.join(ROOT, f), encoding="utf-8", errors="replace") as h:
            return sum(1 for _ in h)
    except Exception:
        return 0
def area(f):
    parts = f.split("/")
    if f.startswith("lld/"): return "lld/" + parts[1]
    if f.startswith("llvm/tools/"): return "tools/" + parts[2]
    return "/".join(parts[1:3])
for name, (word, fname) in FORMATS.items():
    dedicated = [f for f in files if fname.search(os.path.basename(f)) or fname.search(f)]
    if name == "wasm":
        dedicated = [f for f in files if re.search(r"wasm", f, re.I)]
    ded_set = set(dedicated)
    by_area = collections.defaultdict(lambda: [0, 0])
    for f in dedicated:
        a = area(f); by_area[a][0] += 1; by_area[a][1] += wc(f)
    shared = []
    for f in files:
        if f in ded_set: continue
        try:
            with open(os.path.join(ROOT, f), encoding="utf-8", errors="replace") as h:
                n = sum(1 for line in h if word.search(line))
        except Exception:
            n = 0
        if n: shared.append((f, n))
    print(f"== {name}: dedicated files {len(dedicated)} ({sum(wc(f) for f in dedicated)} lines)")
    for a, (n, l) in sorted(by_area.items()):
        print(f"   {a:40s} {n:3d} files {l:6d} lines")
    print(f"   shared files mentioning it: {len(shared)}, mention lines {sum(n for _, n in shared)}")
    sa = collections.defaultdict(lambda: [0, 0])
    for f, n in shared:
        a = area(f); sa[a][0] += 1; sa[a][1] += n
    for a, (n, l) in sorted(sa.items()):
        print(f"      {a:40s} {n:3d} files {l:5d} lines")
