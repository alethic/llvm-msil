"""For every query in queries.txt: does a reference scoped to [mscorlib]
resolve at run time on .NET Framework 4.8, .NET 6, .NET 8 and .NET 10?
Follows the runtime's own mscorlib (Framework64) or the shared framework's
mscorlib facade through its ExportedType forwarders (bclresolve trace)."""
import subprocess
SF = "C:/Program Files/dotnet/shared/Microsoft.NETCore.App"
FW = "C:/Windows/Microsoft.NET/Framework64/v4.0.30319"
runs = {"fx48": ["--dir", FW + "/mscorlib.dll"], "net6": ["--dir", SF + "/6.0.36"],
        "net8": ["--dir", SF + "/8.0.31"], "net10": ["--dir", SF + "/10.0.12"]}
res = {}
for k, d in runs.items():
    out = subprocess.run(["dotnet", "bin/bclresolve.dll", "trace", "--queries", "queries.txt",
                          "--from", "mscorlib"] + d, capture_output=True, text=True).stdout.splitlines()
    for q, r in zip(out[0::2], out[1::2]):
        ok = "member OK" in r or r.strip().endswith("TYPEDEF")
        res.setdefault(q, {})[k] = "yes" if ok else ("no (type)" if "TYPE NOT FOUND" in r else "no (member)")
print("| Query | Framework 4.8 | .NET 6 | .NET 8 | .NET 10 | [mscorlib]-safe |")
print("|---|---|---|---|---|---|")
for q, m in res.items():
    safe = all(v == "yes" for v in m.values())
    print(f"| `{q}` | {m['fx48']} | {m['net6']} | {m['net8']} | {m['net10']} | {'**yes**' if safe else 'no'} |")
