#!/usr/bin/env python3
"""tests-p2 recon: c-testsuite single-exec tests that need no libc, through
the Phase-1 path (clang -S + ilasm) and the prototype link path (clang -flto
-c + msil-ld --lto-emit-asm + ilasm), run on .NET 8. Pass = exit 0 and
stdout equal to <test>.c.expected (empty for these tests)."""
import concurrent.futures as cf, glob, os, re, subprocess, sys, time
S = "C:/llvm-msil-scratch/p2/tests-p2"
T = S + "/e4-corpus/src/c-testsuite/repo/tests/single-exec"
OUT = S + "/e4-corpus/ctest-out"
CLANG = "D:/llvm-msil/build/bin/clang.exe"
ILASM = "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
MLD = S + "/tools/msil-ld.exe"
DOTNET = "C:/Program Files/dotnet/dotnet.exe"
RC = S + "/e6-litproto/obj/test/msil-net8.runtimeconfig.json"
LIBC = re.compile(r"\b(printf|fprintf|sprintf|snprintf|vprintf|malloc|calloc|realloc|free|strlen|strcmp|strncmp|strcpy|strncpy|strcat|memcmp|exit|abort|atoi|strtol|qsort|bsearch|getchar|fopen|setjmp|longjmp|va_start|alloca|assert)\s*\(")
TRIPLE = "--target=x86_64-pc-windows-msvc-msil"


def sh(cmd):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def one(src, path):
    stem = os.path.basename(src)[:-2]
    d = os.path.join(OUT, path, stem)
    os.makedirs(d, exist_ok=True)
    flags = [TRIPLE, "-O2", "-w"]
    if path == "asm":
        rc, out = sh([CLANG] + flags + ["-S", src, "-o", d + "/p.il"])
        if rc: return stem, "compile", out
    else:
        rc, out = sh([CLANG] + flags + ["-flto", "-c", src, "-o", d + "/p.o"])
        if rc: return stem, "compile", out
        rc, out = sh([MLD, "--lto-emit-asm", "-o", d + "/p.il", d + "/p.o", "-L", "C:/Windows/System32", "-lucrtbase"])
        if rc: return stem, "link", out
    rc, out = sh([ILASM, "-nologo", "-quiet", "-dll", "-pe64", "-x64", "-output=" + d + "/p.dll", d + "/p.il"])
    if rc: return stem, "ilasm", out
    try:
        r = subprocess.run([DOTNET, "exec", "--runtimeconfig", RC, d + "/p.dll"], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60, cwd=d)
    except subprocess.TimeoutExpired:
        return stem, "timeout", ""
    exp = open(src + ".expected", "rb").read()
    if r.returncode != 0 or r.stdout.replace(b"\r\n", b"\n") != exp.replace(b"\r\n", b"\n"):
        return stem, "run", "exit %d %s" % (r.returncode, r.stderr.decode("utf-8", "replace")[:200])
    return stem, "pass", ""


def main():
    srcs = [f.replace("\\", "/") for f in sorted(glob.glob(T + "/*.c")) if not LIBC.search(open(f, encoding="latin-1").read())]
    for path in ("asm", "lto"):
        t = time.time()
        with cf.ThreadPoolExecutor(4) as ex:
            res = list(ex.map(lambda s: one(s, path), srcs))
        summary = {}
        for stem, what, _ in res:
            summary[what] = summary.get(what, 0) + 1
        print(path, len(srcs), summary, "%.0f s" % (time.time() - t))
        for stem, what, why in res:
            if what != "pass":
                print("  ", stem, what, why.strip().splitlines()[0][:160] if why.strip() else "")


if __name__ == "__main__":
    main()
