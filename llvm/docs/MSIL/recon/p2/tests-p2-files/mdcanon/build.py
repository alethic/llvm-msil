# Build mdcanon.cs offline (the way msil/utils/jitall/build-jitall.py builds jitall).
import glob, json, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
dotnet = r"C:\Program Files\dotnet\dotnet.exe"
sdks = sorted(glob.glob(r"C:\Program Files\dotnet\sdk\*\Roslyn\bincore\csc.dll"))
csc = sdks[-1]
refdir = sorted(glob.glob(r"C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Ref\8.0.*\ref\net8.0"))[-1]
out = os.path.join(here, "bin")
os.makedirs(out, exist_ok=True)
rsp = os.path.join(out, "csc.rsp")
with open(rsp, "w") as f:
    for r in sorted(glob.glob(os.path.join(refdir, "*.dll"))):
        f.write('-r:"%s"\n' % r)
cmd = [dotnet, csc, "-nologo", "-noconfig", "-nostdlib", "-target:exe", "-nullable-", "-optimize+",
       "-langversion:latest", "-deterministic", "-out:" + os.path.join(out, "mdcanon.dll"), "@" + rsp,
       os.path.join(here, "mdcanon.cs")]
r = subprocess.run(cmd)
json.dump({"runtimeOptions": {"tfm": "net8.0", "rollForward": "Major",
           "framework": {"name": "Microsoft.NETCore.App", "version": "8.0.0"}}},
          open(os.path.join(out, "mdcanon.runtimeconfig.json"), "w"), indent=2)
print("csc:", csc, "\nrefs:", refdir, "\nexit", r.returncode)
sys.exit(r.returncode)
