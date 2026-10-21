#!/usr/bin/env python3
"""tests-p2 recon E4: build candidate Phase-2 exit-criteria programs as
multi-TU programs with static archives through the prototype msil-ld (LTO,
--lto-emit-asm) and ilasm, verify, force-JIT and run them, and compare the
transcript with a native build of the same sources.

usage: corpus.py [--triple windows|linux] [--level O2] OUTDIR [PROGRAM ...]
Run from a VS x64 developer environment (the native link needs LIB).
"""
import argparse, glob, json, os, re, shutil, subprocess, sys, time

S = "C:/llvm-msil-scratch/p2/tests-p2"
SRC = S + "/e4-corpus/src"
H = S + "/e4-corpus/harness"
CLANG = "D:/llvm-msil/build/bin/clang.exe"
VSB = "C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
AR = VSB + "/llvm-ar.exe"
MSIL_LD = S + "/tools/msil-ld.exe"
ILASM = "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
NETFX = "C:/Program Files (x86)/Microsoft SDKs/Windows/v10.0A/bin/NETFX 4.8 Tools/x64"
UTILS = S + "/e6-litproto/msil/utils"
DOTNET = "C:/Program Files/dotnet/dotnet.exe"
RT8 = S + "/e6-litproto/obj/test/msil-net8.runtimeconfig.json"
RT10 = S + "/e6-litproto/obj/test/msil-net10.runtimeconfig.json"
ILVERIFY = os.path.expanduser("~/.dotnet/tools/ilverify.exe").replace("\\", "/")
RTDIR = "C:/Program Files/dotnet/shared/Microsoft.NETCore.App/8.0.32"
PY = sys.executable
LIM = ["--timeout", "300", "--mem-limit-mb", "4096"]
LINUX_ORACLE = S + "/e7-linux-oracle"

SUP = [H + "/sup/" + f for f in ("sup_arena.c", "sup_malloc.c", "sup_memcmp.c", "sup_memset.c",
                                 "sup_memcpy.c", "sup_strlen.c", "sup_out.c")]
ZLIB = SRC + "/zlib/zlib-1.3.2"
# compress.c and uncompr.c need the default allocators (and NULL from a hosted
# header), which Z_SOLO removes.
ZLIB_SRCS = [ZLIB + "/" + f + ".c" for f in ("adler32", "crc32", "deflate", "infback",
             "inffast", "inflate", "inftrees", "trees", "zutil")]
MONO = SRC + "/monocypher/monocypher-4.0.2/src"
LZ4 = SRC + "/lz4/lz4-1.10.0/lib"
TINF = SRC + "/tinf/repo/src"
B2 = SRC + "/BLAKE2/repo"
KISS = SRC + "/kissfft/repo"
CM = SRC + "/coremark/repo"
LZ4_DEFS = ["-DLZ4_FREESTANDING=1", "-DLZ4_memcpy=__builtin_memcpy", "-DLZ4_memset=__builtin_memset",
            "-DLZ4_memmove=__builtin_memmove", "-DLZ4_FORCE_SW_BITCOUNT", "-DLZ4_USER_MEMORY_FUNCTIONS"]

# name: (archives {libname: [sources]}, objects [sources], include dirs, defines)
PROGRAMS = {
    "jsmn": ({}, [H + "/jsmn_main.c", H + "/jsmn_impl.c"], [SRC + "/jsmn/repo"], ["-DJSMN_PARENT_LINKS", "-DJSMN_STRICT"]),
    "tweetnacl": ({"nacl": [SRC + "/tweetnacl/tweetnacl.c"]}, [H + "/tweetnacl_main.c", H + "/tweetnacl_rand.c"],
                  [SRC + "/tweetnacl"], []),
    "monocypher": ({"monocypher": [MONO + "/monocypher.c", MONO + "/optional/monocypher-ed25519.c"]},
                   [H + "/monocypher_main.c"], [MONO, MONO + "/optional"], []),
    "blake2": ({"b2": [B2 + "/ref/" + f for f in ("blake2b-ref.c", "blake2s-ref.c", "blake2bp-ref.c",
                                                  "blake2sp-ref.c", "blake2xb-ref.c", "blake2xs-ref.c")]},
               [H + "/blake2_main.c"], [B2 + "/ref", B2 + "/testvectors"], []),
    "zlib": ({"z": ZLIB_SRCS}, [H + "/zlib_main.c"], [ZLIB], ["-DZ_SOLO"]),
    "tinf": ({"z": ZLIB_SRCS, "tinf": [TINF + "/" + f for f in ("tinflate.c", "tinfzlib.c", "tinfgzip.c",
                                                                "adler32.c", "crc32.c")]},
             [H + "/tinf_main.c"], [ZLIB, TINF], ["-DZ_SOLO", "-DNDEBUG"]),
    "lz4": ({"lz4": [LZ4 + "/lz4.c", LZ4 + "/lz4hc.c"]}, [H + "/lz4_main.c"], [LZ4], LZ4_DEFS),
    "kissfft": ({"kiss": [KISS + "/kiss_fft.c", KISS + "/kiss_fftr.c"]}, [H + "/kissfft_main.c"], [KISS],
                ["-DNDEBUG", "-Dkiss_fft_scalar=double", "-ffp-contract=off"]),
    "coremark": ({"coremark": [CM + "/" + f for f in ("core_list_join.c", "core_main.c", "core_matrix.c",
                                                      "core_state.c", "core_util.c")]},
                 [H + "/coremark/coremark_driver.c", H + "/coremark/core_portme.c"], [H + "/coremark", CM],
                 ["-DITERATIONS=200", "-Dmain=coremark_main"]),
}


def run(cmd, cwd=None, check=True, env=None):
    t = time.perf_counter()
    r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    dt = time.perf_counter() - t
    if check and r.returncode != 0:
        raise RuntimeError("exit %d: %s\n%s" % (r.returncode, " ".join(cmd),
                                                 r.stdout.decode("utf-8", "replace")[-4000:]))
    return dt, r


def fold(b):
    return re.sub(r"\r+\n", "\n", b.decode("utf-8", "replace"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--triple", default="windows", choices=["windows", "linux"])
    ap.add_argument("--level", default="O2")
    ap.add_argument("out")
    ap.add_argument("progs", nargs="*")
    a = ap.parse_args()
    native = "x86_64-pc-windows-msvc" if a.triple == "windows" else "x86_64-unknown-linux-gnu"
    msil = native + "-msil"
    base = ["-ffreestanding", "-isystem", H + "/shim", "-I" + H + "/sup", "-I" + H, "-" + a.level,
            "-Werror=return-type", "-Werror=implicit-function-declaration", "-w"]
    results = []
    for name in a.progs or list(PROGRAMS):
        archives, objs, incs, defs = PROGRAMS[name]
        out = os.path.join(a.out, a.triple + "-" + a.level, name)
        shutil.rmtree(out, ignore_errors=True)
        os.makedirs(out)
        row = {"prog": name, "triple": msil, "level": a.level}
        try:
            flags = base + ["-I" + i for i in incs] + defs
            ncomp = 0
            def compile_to(srcs, sub, target, extra):
                nonlocal ncomp
                os.makedirs(os.path.join(out, sub), exist_ok=True)
                outs, tsum = [], 0.0
                for s in srcs:
                    o = os.path.join(out, sub, os.path.splitext(os.path.basename(s))[0] + ".o")
                    dt, _ = run([CLANG, "--target=" + target] + flags + extra + ["-c", s, "-o", o])
                    tsum += dt
                    outs.append(o)
                    ncomp += 1
                return outs, tsum
            # MSIL: bitcode objects, archives, msil-ld LTO
            t_c = t_ar = 0.0
            libargs = ["-L", out]
            for lib, srcs in list(archives.items()) + [("sup", SUP)]:
                o, dt = compile_to(srcs, "msil-" + lib, msil, ["-flto"])
                t_c += dt
                try:
                    dt, _ = run([AR, "rcs", os.path.join(out, "lib" + lib + ".a")] + o)
                    t_ar += dt
                    libargs += ["-l" + lib]
                except RuntimeError as e:
                    # The VS llvm-ar 22.1.3 cannot index LLVM-24 bitcode that uses
                    # newer attribute kinds: the same lazy semantics through
                    # --start-lib/--end-lib instead (recorded per program).
                    row.setdefault("ar_fallback", []).append(lib)
                    libargs += ["--start-lib"] + o + ["--end-lib"]
            mo, dt = compile_to(objs, "msil-objs", msil, ["-flto"])
            t_c += dt
            row["tus"] = ncomp
            row["msil_compile_s"] = round(t_c, 2)
            row["archive_s"] = round(t_ar, 2)
            ld = [MSIL_LD, "--lto-emit-asm", "-o", out + "/p.il"] + mo + libargs + \
                 ["-mllvm", "-msil-assembly-name=" + name, "-Map", out + "/p.map", "--why-extract=" + out + "/why.txt"]
            if a.triple == "windows":
                ld += ["-L", "C:/Windows/System32", "-lucrtbase"]
            else:
                # stand-in for reading libc.so.6's dynamic symbols (not in the prototype)
                ld += ["-L", S + "/e4-corpus/linux-stub", "-llibc", "-llibm.so.6"]
            dt, r = run(ld)
            row["msil_ld_s"] = round(dt, 2)
            il = open(out + "/p.il", encoding="utf-8", errors="replace").read()
            row["il_bytes"] = len(il)
            row["methods"] = len(re.findall(r"^\.method ", il, re.M))
            m = re.search(r"data segment: (\d+) bytes", il)
            row["data_bytes"] = int(m.group(1)) if m else 0
            row["pinvokes"] = sorted(set(re.findall(r"pinvokeimpl\(\"[^\"]+\"[^)]*\)[^']*'([^']+)'", il)))
            dt, _ = run([ILASM, "-nologo", "-quiet", "-dll", "-pe64", "-x64", "-output=" + out + "/" + name + ".dll", out + "/p.il"])
            row["ilasm_s"] = round(dt, 2)
            dll = out + "/" + name + ".dll"
            dt, r = run([PY, UTILS + "/msil-verify.py", "--ilverify", ILVERIFY, "--runtime-dir", RTDIR,
                         "--allow", UTILS + "/ilverify-allowed.txt", *LIM, "--peverify", NETFX + "/PEVerify.exe",
                         "--ildasm", NETFX + "/ildasm.exe", "--ilasm", ILASM, dll], check=False)
            row["verify_s"] = round(dt, 2)
            row["verify"] = "ok" if r.returncode == 0 else "FAIL: " + fold(r.stdout)[-600:]
            if a.triple == "windows":
                dt, r = run([PY, UTILS + "/msil-jitall.py", "--jitall", S + "/tools/jitall", *LIM, "--dotnet", DOTNET,
                             "--runtimeconfig", RT8, dll], check=False)
                row["jitall_s"] = round(dt, 2)
                row["jitall"] = fold(r.stdout).strip()[-200:]
                dt, r = run([PY, UTILS + "/msil-run.py", *LIM, "--dotnet", DOTNET, "--runtimeconfig", RT8,
                             "--out", out + "/msil.out", dll], check=False)
                row["run_s"] = round(dt, 2)
                # also on .NET 10 and the .NET Framework
                dt10, _ = run([PY, UTILS + "/msil-run.py", *LIM, "--dotnet", DOTNET, "--runtimeconfig", RT10,
                               "--out", out + "/msil10.out", dll], check=False)
                run([ILASM, "-nologo", "-quiet", "-exe", "-pe64", "-x64", "-output=" + out + "/" + name + ".exe", out + "/p.il"])
                dtfx, _ = run([PY, UTILS + "/msil-run.py", *LIM, "--netfx", "--out", out + "/msilfx.out",
                               out + "/" + name + ".exe"], check=False)
                row["run10_s"], row["runfx_s"] = round(dt10, 2), round(dtfx, 2)
            else:
                dt, r = run([PY, UTILS + "/msil-run.py", *LIM, "--runner", "wsl", "--dotnet", "/usr/bin/dotnet",
                             "--runtimeconfig", RT10, "--out", out + "/msil.out", dll], check=False)
                row["run_s"] = round(dt, 2)
            # native oracle
            nobjs, dt = [], 0.0
            for lib, srcs in list(archives.items()) + [("sup", SUP), ("objs", objs)]:
                o, t = compile_to(srcs, "native-" + lib, native, [])  # members may share names
                nobjs += o
                dt += t
            if a.triple == "windows":
                dt2, _ = run([CLANG, "--target=" + native, "-fuse-ld=lld", "-o", out + "/native.exe"] + nobjs)
                row["native_build_s"] = round(dt + dt2, 2)
                dt, r = run([PY, UTILS + "/msil-run.py", *LIM, "--native", "--out", out + "/native.out",
                             out + "/native.exe"], check=False)
            else:
                dt2, _ = run([VSB + "/ld.lld.exe", "-o", out + "/native.elf", "-dynamic-linker",
                              "/lib64/ld-linux-x86-64.so.2", "--as-needed", LINUX_ORACLE + "/start.o"] + nobjs +
                             [LINUX_ORACLE + "/sysroot/libm.so.6", LINUX_ORACLE + "/sysroot/libc.so.6"])
                row["native_build_s"] = round(dt + dt2, 2)
                wsl_out = out.replace("\\", "/").replace("C:", "/mnt/c")
                dt, r = run(["wsl.exe", "-d", "Ubuntu", "--cd", wsl_out, "--", "./native.elf"], check=False)
                open(out + "/native.out", "w", newline="\n").write(fold(r.stdout) + "[exit %d]\n" % r.returncode)
            row["native_run_s"] = round(dt, 2)
            m_out = fold(open(out + "/msil.out", "rb").read())
            n_out = fold(open(out + "/native.out", "rb").read())
            row["status_line"] = m_out.strip().splitlines()[-1] if m_out.strip() else "(none)"
            row["match_native"] = m_out == n_out
            if a.triple == "windows":
                row["match_net10"] = fold(open(out + "/msil10.out", "rb").read()) == m_out
                row["match_netfx"] = fold(open(out + "/msilfx.out", "rb").read()) == m_out
            row["extracted"] = open(out + "/why.txt").read().count("\n") - 1
        except Exception as e:  # noqa
            row["error"] = str(e)[-1500:]
        results.append(row)
        print(json.dumps(row), flush=True)
    with open(os.path.join(a.out, "results-%s-%s.json" % (a.triple, a.level)), "w") as f:
        json.dump(results, f, indent=1)


if __name__ == "__main__":
    main()
