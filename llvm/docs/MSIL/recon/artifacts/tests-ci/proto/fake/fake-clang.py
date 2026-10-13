"""PROTOTYPE ONLY: stands in for a clang that has the MSIL backend.

--target=*-msil -S x.c -o y.il  -> writes Inputs/x.il.in next to x.c (with the
P/Invoke library chosen by triple, as the real backend would), so the lit
pipeline (ilasm, ilverify, dotnet, compare) can be exercised before the
backend exists. Everything else is forwarded to the stock VS clang.
"""
import os, subprocess, sys
REAL = r"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe"
args = sys.argv[1:]
as_llc = bool(args) and args[0] == "--as-llc"
if as_llc:  # llc -mtriple=T -O2 x.ll -o y.il
    args = args[1:]
    target = next(a.split("=", 1)[1] for a in args if a.startswith("-mtriple="))
else:
    target = next((a.split("=", 1)[1] for a in args if a.startswith("--target=")), "")
if not target.endswith("-msil"):
    sys.exit(subprocess.call([REAL] + args))
src = next(a for a in args if a.endswith((".c", ".ll")))
out = args[args.index("-o") + 1]
stem = os.path.splitext(os.path.basename(src))[0]
tmpl = os.path.join(os.path.dirname(src), "Inputs", stem + ".il.in")
libc = "ucrtbase" if "-windows-" in target else "libc"
with open(tmpl) as f:
    text = f.read().replace("@LIBC@", libc).replace("@NAME@", stem)
with open(out, "w") as f:
    f.write(text)
