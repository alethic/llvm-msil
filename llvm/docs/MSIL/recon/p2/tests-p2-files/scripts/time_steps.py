#!/usr/bin/env python3
"""tests-p2 recon E1: wall time of every step of the Phase-1 execution pipeline,
and of a prototype link path (clang -flto -c + msil-ld --lto-emit-asm + ilasm),
per program and level, run sequentially on an otherwise idle host.

usage: time_steps.py OUTDIR PROG.c [PROG.c ...]
"""
import json, os, subprocess, sys, time, shutil, statistics

CLANG = "D:/llvm-msil/build/bin/clang.exe"
ILASM = "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
ILASM_NEW = "D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe"
ILDASM_NEW = "D:/packages/NuGet/runtime.win-x64.microsoft.netcore.ildasm/10.0.12/runtimes/win-x64/native/ildasm.exe"
NETFX_TOOLS = "C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64"
S = "C:/llvm-msil-scratch/p2/tests-p2"
UTILS = S + "/e6-litproto/msil/utils"
INPUTS = S + "/e6-litproto/msil/test/Inputs"
MSIL_LD = S + "/tools/msil-ld.exe"
RTCFG = S + "/e6-litproto/obj/test/msil-net8.runtimeconfig.json"
DOTNET = "C:/Program Files/dotnet/dotnet.exe"
ILVERIFY = os.path.expanduser("~/.dotnet/tools/ilverify.exe")
RTDIR = "C:/Program Files/dotnet/shared/Microsoft.NETCore.App/8.0.32"
PY = sys.executable
LIM = ["--timeout", "120", "--mem-limit-mb", "4096"]


def run(cmd, cwd=None):
    t = time.perf_counter()
    r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    dt = time.perf_counter() - t
    if r.returncode != 0:
        print("FAILED (%d): %s\n%s" % (r.returncode, " ".join(cmd), r.stdout.decode("utf-8", "replace")[-3000:]))
    return dt, r.returncode, r.stdout


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rows = []
    for src in sys.argv[2:]:
        stem = os.path.splitext(os.path.basename(src))[0]
        for lvl in ("O0", "O2"):
            d = os.path.join(out, stem + "." + lvl)
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d)
            il, dll = d + "/p.il", d + "/p.dll"
            row = {"prog": stem, "level": lvl}
            cf = ["-ffreestanding", "-Werror=return-type", "-I" + INPUTS, "-" + lvl]
            row["clang_S"], rc, _ = run([CLANG, "--target=x86_64-pc-windows-msvc-msil"] + cf + ["-S", src, "-o", il])
            row["ilasm_fx"], rc, _ = run([ILASM, "-nologo", "-quiet", "-pe64", "-x64", "-dll", "-output=" + dll, il])
            row["ilasm_new"], rc, _ = run([ILASM_NEW, "-nologo", "-quiet", "-pe64", "-x64", "-dll", "-output=" + d + "/new.dll", il])
            row["verify"], rc, _ = run([PY, UTILS + "/msil-verify.py", "--ilverify", ILVERIFY, "--runtime-dir", RTDIR,
                                       "--allow", UTILS + "/ilverify-allowed.txt", *LIM,
                                       "--peverify", NETFX_TOOLS + "/PEVerify.exe", "--ildasm", NETFX_TOOLS + "/ildasm.exe",
                                       "--ilasm", ILASM, dll])
            row["jitall"], rc, _ = run([PY, UTILS + "/msil-jitall.py", "--jitall", S + "/tools/jitall", *LIM,
                                       "--dotnet", DOTNET, "--runtimeconfig", RTCFG, dll])
            row["run_net8"], rc, _ = run([PY, UTILS + "/msil-run.py", *LIM, "--dotnet", DOTNET, "--runtimeconfig", RTCFG,
                                         "--out", d + "/run.out", dll])
            row["ildasm_new"], rc, _ = run([ILDASM_NEW, "-out=" + d + "/p.dis.il", dll])
            # prototype link path: bitcode object + msil-ld LTO to IL text + ilasm
            row["clang_flto_c"], rc, _ = run([CLANG, "--target=x86_64-pc-windows-msvc-msil"] + cf + ["-flto", "-c", src, "-o", d + "/p.o"])
            row["msil_ld_lto"], rc, _ = run([MSIL_LD, "--lto-emit-asm", "-o", d + "/l.il", d + "/p.o",
                                            "-L", "C:/Windows/System32", "-lucrtbase", "-mllvm", "-msil-assembly-name=p"])
            row["ilasm_fx_lto"], rc, _ = run([ILASM, "-nologo", "-quiet", "-pe64", "-x64", "-dll", "-output=" + d + "/l.dll", d + "/l.il"])
            row["run_net8_lto"], rc, _ = run([PY, UTILS + "/msil-run.py", *LIM, "--dotnet", DOTNET, "--runtimeconfig", RTCFG,
                                             "--out", d + "/lrun.out", d + "/l.dll"])
            same = open(d + "/run.out", "rb").read() == open(d + "/lrun.out", "rb").read()
            row["lto_same_transcript"] = same
            row["il_bytes"] = os.path.getsize(il)
            row["lto_il_identical"] = open(il, "rb").read() == open(d + "/l.il", "rb").read()
            rows.append(row)
            print(json.dumps(row))
            sys.stdout.flush()
    with open(os.path.join(out, "times.json"), "w") as f:
        json.dump(rows, f, indent=1)
    keys = [k for k in rows[0] if isinstance(rows[0][k], float)]
    print("\nstep               median    mean     max   (seconds, %d runs)" % len(rows))
    for k in keys:
        v = [r[k] for r in rows]
        print("%-18s %6.2f  %6.2f  %6.2f" % (k, statistics.median(v), statistics.mean(v), max(v)))
    print("transcripts equal (asm vs lto): %d/%d" % (sum(r["lto_same_transcript"] for r in rows), len(rows)))
    print("IL text identical (asm vs lto): %d/%d" % (sum(r["lto_il_identical"] for r in rows), len(rows)))


if __name__ == "__main__":
    main()
