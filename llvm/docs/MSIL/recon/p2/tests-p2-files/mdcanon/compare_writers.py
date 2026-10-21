# E3a: the same .il through two writers (Framework ilasm 4.8 and .NET ilasm 10.0.12):
# raw bytes, ildasm listings and mdcanon listings compared.
import glob, os, subprocess, sys, difflib, time, re
DOTNET = r"C:\Program Files\dotnet\dotnet.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
MDC = [DOTNET, "exec", "--runtimeconfig", HERE + "/bin/mdcanon.runtimeconfig.json", HERE + "/bin/mdcanon.dll"]
ILDASM = "D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ildasm/10.0.12/runtimes/win-x64/native/ildasm.exe"
def canon(dll, *opts):
    r = subprocess.run(MDC + list(opts) + [dll], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode: print("mdcanon failed", dll, r.stderr.decode()); sys.exit(1)
    return [l for l in r.stdout.decode().splitlines() if not l.startswith("module ")]
def ildasm(dll):
    r = subprocess.run([ILDASM, dll], stdout=subprocess.PIPE)
    return [l for l in r.stdout.decode("utf-8", "replace").splitlines()
            if not re.match(r"\s*// (MVID|Image base|WARNING)", l) and ".module" not in l]
stats = dict(pairs=0, bytes_eq=0, ildasm_eq=0, canon_eq=0)
t0 = time.time()
for d in sorted(glob.glob(sys.argv[1] + "/*.O?")):
    a, b = d + "/p.dll", d + "/new.dll"
    stats["pairs"] += 1
    stats["bytes_eq"] += open(a, "rb").read() == open(b, "rb").read()
    ia, ib = ildasm(a), ildasm(b)
    stats["ildasm_eq"] += ia == ib
    ca, cb = canon(a), canon(b)
    stats["canon_eq"] += ca == cb
    if ca != cb:
        print("CANON DIFF", d); print("\n".join(list(difflib.unified_diff(ca, cb, lineterm=""))[:30]))
    if ia != ib and stats["pairs"] < 3:
        print("ILDASM DIFF", d); print("\n".join(list(difflib.unified_diff(ia, ib, lineterm=""))[:20]))
print(stats, "%.0f s" % (time.time() - t0))
