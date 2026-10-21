"""Assembles every v*.il with the .NET Framework ilasm and runs it on .NET
Framework 4.8 (the .exe directly), .NET 8 and .NET 10 (dotnet exec with a
runtimeconfig), printing a markdown table of exit statuses and the first line
of any unhandled exception."""
import json, os, re, subprocess, sys

ILASM = r"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe"
DOTNET = r"C:\Program Files\dotnet\dotnet.exe"

def rc(path, ver):
    with open(path, "w") as f:
        json.dump({"runtimeOptions": {"framework": {"name": "Microsoft.NETCore.App", "version": ver}}}, f)

def first_error(text):
    for line in text.splitlines():
        line = line.strip()
        if "Exception" in line or "error" in line.lower():
            line = re.sub(r"\s+", " ", line)
            return line[:230]
    return text.strip().splitlines()[0][:230] if text.strip() else ""

def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=60)
    code = p.returncode
    if code > 0x7FFFFFFF:
        code -= 1 << 32
    return code, first_error(p.stderr + p.stdout)

def main(d):
    rc(os.path.join(d, "rc8.json"), "8.0.0")
    rc(os.path.join(d, "rc10.json"), "10.0.0")
    rows = []
    for il in sorted(f for f in os.listdir(d) if f.endswith(".il")):
        n = il[:-3]
        exe = n + ".exe"
        a = subprocess.run([ILASM, "-nologo", "-quiet", "-exe", "-pe64", "-x64", "-output=" + exe, il],
                           cwd=d, capture_output=True, text=True)
        if a.returncode:
            rows.append((n, "ilasm failed: " + first_error(a.stdout + a.stderr), "", ""))
            continue
        res = []
        res.append(run([os.path.join(d, exe)], d))
        res.append(run([DOTNET, "exec", "--runtimeconfig", "rc8.json", exe], d))
        res.append(run([DOTNET, "exec", "--runtimeconfig", "rc10.json", exe], d))
        rows.append((n,) + tuple(f"{c}" + (f" ({e})" if c != 42 else "") for c, e in res))
    print("| Variant | .NET Framework 4.8 | .NET 8.0.31 | .NET 10.0.12 |")
    print("|---|---|---|---|")
    for r in rows:
        print("| " + " | ".join(r) + " |")

if __name__ == "__main__":
    main(sys.argv[1])
