#!/usr/bin/env python3
"""hosted-libc recon driver: compile real hosted C programs for the MSIL
triples (and natively), collect every diagnostic, link, and summarize.

  hl.py build <prog> <triple> [--variant V] [-O LEVEL] [--jobs N]
  hl.py summary <prog> <triple> [--variant V]

Outputs go to C:/llvm-msil-scratch/p3/hosted-libc/build/<prog>-<variant>/<tshort>-<O>/
"""
import argparse, os, re, subprocess, sys, json, time, concurrent.futures as cf

S = r"C:/llvm-msil-scratch/p3/hosted-libc"
SRC = S + "/src"
B = r"D:/llvm-msil/build/bin"
GS = r"C:/llvm-msil-scratch/glibc-sysroot"
CLANG = B + "/clang.exe"

TRIPLES = {
    "win": "x86_64-pc-windows-msvc-msil",
    "lin": "x86_64-unknown-linux-gnu-msil",
    "win-native": "x86_64-pc-windows-msvc",
    "lin-native": "x86_64-unknown-linux-gnu",
}

LUA_CORE = "lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio".split()
LUA_LIB = "lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit".split()
ZLIB = "adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil".split()


def progs():
    z = SRC + "/zlib-1.3.2"
    l = SRC + "/lua-5.4.9/src"
    q = SRC + "/sqlite-amalgamation-3540100"
    return {
        "zlib": {
            "lib": [f"{z}/{n}.c" for n in ZLIB],
            "mains": {"example": [f"{z}/test/example.c"], "minigzip": [f"{z}/test/minigzip.c"]},
            "cflags": {"win": ["-I" + z, "-D_CRT_SECURE_NO_DEPRECATE", "-D_CRT_NONSTDC_NO_DEPRECATE"],
                       "lin": ["-I" + z, "-DZ_HAVE_UNISTD_H", "-D_LARGEFILE64_SOURCE=1"]},
            "libs": {"win": [], "lin": []},
        },
        "lua": {
            "lib": [f"{l}/{n}.c" for n in LUA_CORE + LUA_LIB],
            "mains": {"lua": [f"{l}/lua.c"], "luac": [f"{l}/luac.c"]},
            "cflags": {"win": ["-DLUA_COMPAT_5_3"],
                       "lin": ["-DLUA_COMPAT_5_3", "-DLUA_USE_LINUX", "-std=gnu99"]},
            "libs": {"win": [], "lin": ["-lm", "-ldl"]},
        },
        "sqlite": {
            "lib": [f"{q}/sqlite3.c"],
            "mains": {"shell": [f"{q}/shell.c"]},
            "cflags": {"win": ["-I" + q], "lin": ["-I" + q]},
            "libs": {"win": [], "lin": ["-lm", "-lpthread", "-ldl"]},
        },
    }


VARIANTS = {
    "default": [],
    "ts0": ["-DSQLITE_THREADSAFE=0", "-DSQLITE_OMIT_LOAD_EXTENSION"],
    "nounistd": [],
}


def tshort(triple):
    return "win" if "windows" in triple else "lin"


def base_flags(triple, opt):
    f = [CLANG, "--target=" + triple, "-" + opt, "-fno-color-diagnostics",
         "-fno-caret-diagnostics", "-ferror-limit=0"]
    if "linux" in triple:
        f.append("--sysroot=" + GS)
    return f


def run(cmd, log):
    t0 = time.time()
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    dt = time.time() - t0
    with open(log, "w", encoding="utf-8") as fh:
        fh.write("$ " + " ".join(cmd) + "\n")
        fh.write(p.stdout)
        fh.write(f"\n[rc {p.returncode}] [{dt:.1f}s]\n")
    return p.returncode, p.stdout, dt


def build(args):
    P = progs()[args.prog]
    triple = TRIPLES[args.triple]
    ts = tshort(triple)
    native = args.triple.endswith("native")
    out = f"{S}/build/{args.prog}-{args.variant}/{args.triple}-{args.opt}"
    os.makedirs(out, exist_ok=True)
    cflags = P["cflags"][ts] + VARIANTS[args.variant] + args.extra
    tus = list(P["lib"]) + [s for m in P["mains"].values() for s in m]

    def one(src):
        stem = os.path.splitext(os.path.basename(src))[0]
        res = {}
        bf = base_flags(triple, args.opt) + cflags
        rc, o, dt = run(bf + ["-c", src, "-o", f"{out}/{stem}.o"], f"{out}/{stem}.obj.log")
        res["obj"] = (rc, dt)
        if not native:
            rc2, o2, dt2 = run(bf + ["-flto", "-c", src, "-o", f"{out}/{stem}.bc"], f"{out}/{stem}.bc.log")
            res["bc"] = (rc2, dt2)
            rc3, o3, dt3 = run(bf + ["-flto", "-S", "-w", src, "-o", f"{out}/{stem}.ll"], f"{out}/{stem}.ll.log")
            res["ll"] = (rc3, dt3)
        return stem, res

    results = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for stem, res in ex.map(one, tus):
            results[stem] = res
            print(stem, res, flush=True)
    # links
    lib_stems = [os.path.splitext(os.path.basename(s))[0] for s in P["lib"]]
    links = {}
    for mname, msrcs in P["mains"].items():
        mst = [os.path.splitext(os.path.basename(s))[0] for s in msrcs]
        libs = P["libs"][ts] + args.ldextra
        lf = base_flags(triple, args.opt)
        if native and ts == "lin":
            continue  # native linux link done by a separate script
        exe = f"{out}/{mname}.exe"
        if all(results[s]["obj"][0] == 0 for s in lib_stems + mst):
            rc, o, dt = run(lf + [f"{out}/{s}.o" for s in lib_stems + mst] + libs + ["-o", exe],
                            f"{out}/{mname}.link-obj.log")
            links[mname + ".obj"] = (rc, dt)
        if not native and all(results[s]["bc"][0] == 0 for s in lib_stems + mst):
            rc, o, dt = run(lf + ["-flto"] + [f"{out}/{s}.bc" for s in lib_stems + mst] + libs +
                            ["-Wl,--print-imports", "-o", f"{out}/{mname}.lto.exe"],
                            f"{out}/{mname}.link-lto.log")
            links[mname + ".lto"] = (rc, dt)
            rc, o, dt = run(lf + ["-flto"] + [f"{out}/{s}.bc" for s in lib_stems + mst] + libs +
                            ["-Wl,--lto-emit-asm", "-Wl,--error-limit=0", "-Wl,--print-imports",
                             "-o", f"{out}/{mname}.lto.il"],
                            f"{out}/{mname}.link-asm.log")
            links[mname + ".asm"] = (rc, dt)
    print("links", links)
    json.dump({"tus": results, "links": links}, open(f"{out}/results.json", "w"), indent=1)


VTOOL = r"C:/llvm-msil-scratch/p3/variadics/proto/msil-expand-variadics.exe"
LLC = B + "/llc.exe"


def pbuild(args):
    """Simulated P3-B variadics: clang -S -emit-llvm, the variadics recon's
    ExpandVariadics prototype, optional IR fix-ups (fix.py), llc -filetype=obj,
    then the clang driver links the CIL objects with msil-ld."""
    P = progs()[args.prog]
    triple = TRIPLES[args.triple]
    ts = tshort(triple)
    out = f"{S}/build/{args.prog}-{args.variant}/{args.triple}-{args.opt}-{args.tag}"
    os.makedirs(out, exist_ok=True)
    cflags = P["cflags"][ts] + VARIANTS[args.variant] + args.extra
    tus = list(P["lib"]) + [s for m in P["mains"].values() for s in m] + args.simsrc
    simstems = [os.path.splitext(os.path.basename(s))[0] for s in args.simsrc]

    def one(src):
        stem = os.path.splitext(os.path.basename(src))[0]
        if src.endswith(".ll"):
            rc, o, dt = run([LLC, "-mtriple=" + triple, "-" + args.opt, "-filetype=obj", src, "-o", f"{out}/{stem}.o"],
                            f"{out}/{stem}.obj.log")
            return stem, ("ok" if rc == 0 else "llc", rc, dt)
        bf = base_flags(triple, args.opt) + (cflags if src not in args.simsrc else [])
        rc, o, dt = run(bf + ["-S", "-emit-llvm", "-Wno-deprecated-declarations", src, "-o", f"{out}/{stem}.ll"],
                        f"{out}/{stem}.ll.log")
        if rc:
            return stem, ("clang", rc, dt)
        cur = f"{out}/{stem}.ll"
        if args.fix:
            rc, o, dt2 = run([sys.executable, "-I", S + "/tools/fix.py", args.fix, cur, f"{out}/{stem}.f.ll"],
                             f"{out}/{stem}.fix.log")
            if rc:
                return stem, ("fix", rc, dt2)
            cur = f"{out}/{stem}.f.ll"
        rc, o, dt2 = run([VTOOL, cur, f"{out}/{stem}.x.ll"], f"{out}/{stem}.x.log")
        if rc:
            return stem, ("expand", rc, dt2)
        rc, o, dt3 = run([LLC, "-" + args.opt, "-filetype=obj", f"{out}/{stem}.x.ll", "-o", f"{out}/{stem}.o"],
                         f"{out}/{stem}.obj.log")
        if rc:
            return stem, ("llc", rc, dt3)
        return stem, ("ok", 0, dt + dt2 + dt3)

    results = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for stem, res in ex.map(one, tus):
            results[stem] = res
            print(stem, res, flush=True)
    lib_stems = [os.path.splitext(os.path.basename(s))[0] for s in P["lib"]]
    links = {}
    for mname, msrcs in P["mains"].items():
        mst = [os.path.splitext(os.path.basename(s))[0] for s in msrcs]
        if not all(results[s][0] == "ok" for s in lib_stems + mst + simstems):
            continue
        libs = P["libs"][ts] + args.ldextra
        lf = base_flags(triple, args.opt)
        rc, o, dt = run(lf + [f"{out}/{s}.o" for s in lib_stems + mst + simstems] + libs +
                        ["-Wl,--error-limit=0", "-Wl,--print-imports", "-o", f"{out}/{mname}.exe"],
                        f"{out}/{mname}.link-obj.log")
        links[mname] = (rc, dt)
    print("links", links)
    json.dump({"tus": results, "links": links}, open(f"{out}/results.json", "w"), indent=1)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd")
    b = sp.add_parser("build")
    b.add_argument("prog")
    b.add_argument("triple")
    b.add_argument("--variant", default="default")
    b.add_argument("--opt", default="O2")
    b.add_argument("--jobs", type=int, default=3)
    b.add_argument("--extra", action="append", default=[])
    b.add_argument("--ldextra", action="append", default=[])
    pb = sp.add_parser("pbuild")
    pb.add_argument("prog")
    pb.add_argument("triple")
    pb.add_argument("--variant", default="default")
    pb.add_argument("--opt", default="O2")
    pb.add_argument("--jobs", type=int, default=3)
    pb.add_argument("--extra", action="append", default=[])
    pb.add_argument("--ldextra", action="append", default=[])
    pb.add_argument("--fix", default="")
    pb.add_argument("--simsrc", action="append", default=[])
    pb.add_argument("--tag", default="proto")
    a = ap.parse_args()
    if a.cmd == "build":
        build(a)
    elif a.cmd == "pbuild":
        pbuild(a)


if __name__ == "__main__":
    main()
