# -*- Python -*-
#
# lit configuration for the MSIL execution tests (msil/test, see README.md):
#
#   prog.c --clang --target=<triple>-msil -S--> prog.il --ilasm -dll--> prog.dll
#     --msil-verify (ILVerify, PEVerify)--> --jitall (force-JIT every method)-->
#     --msil-run (private directory, timeout, memory cap)--> transcript
#
# and the transcript (stdout plus a status line, see msil/utils/msil-run.py)
# must equal the checked-in <stem>[.<os>].expected, which update-expected.py
# generates from a native build. %{msil_c} and %{msil_ll} run that pipeline
# once per level of --param msil_opt_levels; optional legs add a native-oracle
# build (msil_native_oracle), a .NET Framework run (msil_run_netfx) and an
# ildasm -> ilasm round trip (msil_roundtrip); msil_wsl_ilasm assembles with
# the Linux ilasm inside WSL. msil/utils/ci/lit-legs.ps1 and lit-legs.sh run
# every leg of the milestone-1 gate (plan M10) with these parameters.
#
# Phase 2 (plan 5.12): %{msil_prog} builds a program of several translation
# units and archives with msil/utils/msil-prog.py, which compiles them and
# links through the clang driver and msil-ld, and runs it like %{msil_c}.
# From M20 msil-ld writes the program image itself (msil_link_output=pe) and
# %{msil_prog} compiles CIL objects with clang -c (msil-prog-pipeline=obj)
# under msil_pipeline=asm and obj, and bitcode with clang -flto -c under
# msil_pipeline=lto; msil_pipeline=obj makes %{msil_c} a one-source
# %{msil_prog} and %{msil_ll} 'llc -filetype=obj' plus the link, and lto
# makes %{msil_c} a one-source LTO %{msil_prog}; msil_equiv=1 adds the
# equivalence oracles O3, O4 and O6 of plan 5.12.6 (%msil_equiv,
# msil/utils/msil-equiv.py; see "linker" and "%{msil_prog}" below).
# msil_scale_tests=1 enables the tests at the real CLR limits (feature
# msil-scale-tests). From M15 the links read the driver's default
# libraries (the native libraries of plan 5.10.4; Linux triples through
# --sysroot=<msil_linux_sysroot>), a test adds its own native libraries to
# both builds with %{msil_native_libs} and ships run-time files with each
# run with %{msil_deps}, and msil_glibc_sysroot enables the optional hosted
# compile-only tests of msil/test/M2/09-hosted (plan Q16). From M16,
# %msil_canon prints the canonical listing of an assembly (plan 5.7.10,
# 5.12.3), which harness/canon-listing.test pins and later milestones
# compare: mdcanon in M16 (see "mdcanon" below), and from M17 the tree's
# llvm-cil-dump --canonical (see "llvm-cil-dump" below), which
# harness/canon-agree.test compares with mdcanon byte for byte. From M21,
# msil_references=net48, net8, net10 or a path links every MSIL program
# against reference assemblies (%{msil_ref_flags} in %{msil_ldflags}; the
# legs refs-net48, refs-net8 and refs-net10 of msil/utils/ci), and
# %msil_check_mscorlib_safe proves msil-ld's built-in [mscorlib] table on
# the runtimes (see "reference assemblies" below). From M22, a %{msil_prog}
# test builds shared libraries before its program with %{msil_shlibs}
# (msil-prog.py --shlibs: library assemblies through clang -shared, and the
# native oracle's DLLs or ELF shared objects), every run gets the build's
# shlib/ directory beside the program (--dep-dir; msil-verify.py also
# verifies the libraries), and %msil_csc compiles a C# program with the
# newest .NET SDK's csc (feature msil-csc, parameter msil_require_csc; see
# "C# compiler" below).
#
# From M23 (plan 5.12.1, Q31) objects are the default pipeline: with no
# parameter, msil_pipeline=obj and msil_link_output=pe, so %{msil_c},
# %{msil_ll} and %{msil_prog} compile CIL objects and run msil-ld's image;
# msil_pipeline=asm (-S plus ilasm) stays selectable as the asm oracle. The
# exit corpus (plan 5.12.9) is read from a cache that
# msil/utils/corpus/fetch.py fills outside the repository (msil_corpus_dir,
# features msil-corpus and msil-corpus-<name>, %{msil_corpus}; see "exit
# corpus" below); msil_also_run repeats jitall and the run of every
# composite on further .NET runtimes against the same expected file, and
# those of an explicit-RUN test with %{msil_also_run_check} and
# %{msil_also_run_image} (see "more runtimes" below); a test adds compile
# flags with %{msil_copts} to both builds of %{msil_prog} and %{msil_det}
# and to every build of %{msil_c} on every pipeline, O3's and O6's asm
# reference included (%{msil_ll} takes none, because llc has no C flags),
# and names another expected file with %{msil_expect} (default
# %S/%{s:stem}).
#
# The native oracle (plan 5.12.6 O1, 5.12.8) is the same program built for
# the native triple: on Windows by clang and link.exe, with the VS clang's
# compiler-rt builtins for the i128 helpers (msil_native_rtlib); on Linux by
# clang and the system linker; and with msil_runner=wsl by the E7 Linux
# oracle: clang compiles for x86_64-unknown-linux-gnu, ld.lld links with
# msil/utils/linux-oracle/start.c against the libraries that
# msil/utils/make-linux-sysroot.py copies out of the WSL distribution
# (msil_linux_sysroot), and the program runs under WSL. The WSL runner runs
# the programs on the private runtime of msil/utils/msil-wsl-dotnet.sh
# ($HOME/.dotnet-msil inside WSL, msil_wsl_dotnet).
#
# Nothing here is fatal when a tool is absent: directories that need it mark
# themselves UNSUPPORTED, so check-all keeps working everywhere. Exceptions:
# --param msil_require_verify=1 (every gate passes it, plan D38) makes a
# missing ILVerify, ildasm (msil-verify.py's neutralized pass), jitall,
# mdcanon, llvm-cil-dump (from M17), (on Windows) PEVerify, ilasm or .NET
# runtime for the selected runner a configuration error, even when an empty
# --param msil_<tool>= or $MSIL_<TOOL> disabled it on purpose, so a gate
# never passes with its verification or every harness test UNSUPPORTED;
# --param msil_require_exec=1 (the gate legs of msil/utils/ci pass it) does
# the same for msil-exec, which M1/ needs (clang with the MSIL target, ilasm
# and a .NET runtime; see M1/lit.local.cfg); --param msil_require_link=1 (and
# msil_pipeline=lto or obj) does the same for msil-ld and llvm-ar;
# --param msil_require_csc=1 does it for the SDK's csc of %msil_csc (M22);
# --param msil_require_corpus=1 (the corpus legs) does it for the exit
# corpus; and an explicitly requested runner, runtime, leg or tool that
# cannot be used is one too.

import glob
import hashlib
import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

import lit.formats
import lit.util
from lit.llvm import llvm_config

config.name = "MSIL"
# LLVM 23 deprecates the external shell, so RUN lines use lit's internal shell
# on every host and must not rely on bash.
config.test_format = lit.formats.ShTest()
config.suffixes = [".c", ".ll", ".test"]
config.excludes = ["Inputs", "CMakeLists.txt", "README.md"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.msil_obj_root, "test")
# %{msil_c} expands to other substitutions, which expand again.
config.recursiveExpansionLimit = 10

params = lit_config.params
features = config.available_features
utils_dir = os.path.join(config.msil_src_root, "utils")
inputs_dir = os.path.join(config.test_source_root, "Inputs")
is_windows = platform.system() == "Windows"
exe_ext = ".exe" if is_windows else ""
home = os.path.expanduser("~")


def note(msg):
    lit_config.note("msil: " + msg)


def fatal(msg):
    lit_config.fatal("msil: " + msg)


def slashes(path):
    return path.replace("\\", "/")


def quote(path):
    # Forward slashes work for every tool on Windows and keep backslashes away
    # from lit's internal shell.
    return '"%s"' % slashes(path)


def version_key(text):
    """'10.0.1' sorts after '8.0.31'."""
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", text)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def decode(data):
    # wsl.exe writes its own messages in UTF-16LE.
    text = (
        data.decode("utf-16-le", "replace")
        if b"\0" in data
        else data.decode("utf-8", "replace")
    )
    return text.replace("\r\n", "\n")


def run_quietly(cmd, timeout):
    """(returncode, output) of cmd, or (None, reason) if it cannot run."""
    try:
        res = subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except (OSError, subprocess.SubprocessError) as e:
        return None, str(e)
    return res.returncode, decode(res.stdout)


def bool_param(name, default="0"):
    value = params.get(name, default)
    try:
        return lit.util.pythonize_bool(value)
    except ValueError:
        fatal("--param %s=%s is not a boolean (use 0 or 1)" % (name, value))


def number_param(name, default, convert):
    value = params.get(name)
    if value is None:
        return default
    try:
        number = convert(value)
    except ValueError:
        number = 0
    if number <= 0:
        fatal("--param %s=%s must be a positive number" % (name, value))
    return number


def is_runnable(path):
    # X_OK matters on Linux: NuGet may extract ilasm without the x bit.
    return bool(path) and os.path.isfile(path) and os.access(path, os.X_OK)


def find_tool(name, preferred=(), fallback=()):
    """Precedence: --param msil_<name>=PATH, $MSIL_<NAME>, the CMake value,
    preferred, PATH, fallback. An explicitly empty override disables the tool;
    an explicit path that does not exist is an error."""
    override = params.get("msil_" + name)
    if override is None:
        override = os.environ.get("MSIL_" + name.upper())
    if override is not None:
        if override == "":
            note("%s disabled by an empty override" % name)
            return None
        found = override if is_runnable(override) else lit.util.which(override)
        if not found:
            fatal("msil_%s=%s: no such program" % (name, override))
        return os.path.normpath(found)
    site = getattr(config, "msil_" + name, "") or ""
    if site.endswith("-NOTFOUND"):
        site = ""
    for candidate in [site, *preferred, lit.util.which(name), *fallback]:
        if is_runnable(candidate):
            return os.path.normpath(candidate)
    return None


def list_runtimes(text):
    """Microsoft.NETCore.App majors in 'dotnet --list-runtimes' output:
    {major: (version, directory of that runtime)}."""
    runtimes = {}
    for m in re.finditer(r"^Microsoft\.NETCore\.App (\S+) \[(.+)\]\s*$", text, re.M):
        ver, base = m.group(1), m.group(2)
        major = version_key(ver)[0]
        if major not in runtimes or version_key(ver) > version_key(runtimes[major][0]):
            runtimes[major] = (
                ver,
                base + "/" + ver if base.startswith("/") else os.path.join(base, ver),
            )
    return runtimes


def write_runtimeconfig(major):
    """A runtimeconfig.json for Microsoft.NETCore.App <major>.x in the exec root."""
    os.makedirs(config.test_exec_root, exist_ok=True)
    path = os.path.join(config.test_exec_root, "msil-net%d.runtimeconfig.json" % major)
    text = (
        json.dumps(
            {
                "runtimeOptions": {
                    "tfm": "net%d.0" % major,
                    "rollForward": "LatestPatch",
                    "framework": {
                        "name": "Microsoft.NETCore.App",
                        "version": "%d.0.0" % major,
                    },
                }
            },
            indent=2,
        )
        + "\n"
    )
    try:
        with open(path, encoding="utf-8") as f:
            current = f.read()
    except OSError:
        current = None
    if current != text:  # replace atomically: another lit run may be reading it
        tmp = "%s.%d.tmp" % (path, os.getpid())
        with open(tmp, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    return path


# ---------------------------------------------------------------- LLVM tools
# M0 builds no LLVM tool, so FileCheck may be missing; the internal shell has
# its own 'not'.
if lit.util.which("FileCheck", config.llvm_tools_dir):
    llvm_config.use_default_substitutions()  # FileCheck, count, not, %python
else:
    config.substitutions.append(("%python", '"%s"' % sys.executable))
    llvm_config.add_err_msg_substitutions()
    note("FileCheck is not built; FileCheck, count and not are not substituted")
llvm_config.use_clang(required=False)  # %clang; feature 'clang'

# ---------------------------------------------------------------- parameters
require_verify = bool_param("msil_require_verify")
run_netfx = bool_param("msil_run_netfx")
timeout = number_param("msil_timeout", 60.0, float)
mem_limit_mb = number_param("msil_mem_limit_mb", 4096, int)
runner = params.get("msil_runner", "local")
if runner not in ("local", "wsl"):
    fatal("--param msil_runner=%s: use local or wsl" % runner)
if runner == "wsl" and not is_windows:
    fatal("--param msil_runner=wsl needs a Windows host")
wsl_distro = params.get("msil_wsl_distro", "Ubuntu")

dotnet_env = []  # msil_dotnet_env='VAR=VALUE[;VAR=VALUE...]' (plan D38)
for kv in params.get("msil_dotnet_env", "").split(";"):
    kv = kv.strip()
    if not kv:
        continue
    key, sep, _ = kv.partition("=")
    if not sep or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", key) or '"' in kv:
        fatal("--param msil_dotnet_env: %r is not VAR=VALUE" % kv)
    dotnet_env.append(kv)

opt_levels = [
    o.strip() for o in params.get("msil_opt_levels", "O0,O2").split(",") if o.strip()
]
for o in opt_levels:
    if o not in ("O0", "O1", "O2", "O3", "Os", "Oz"):
        fatal("--param msil_opt_levels: unknown level %r (use O0,O1,O2,O3,Os,Oz)" % o)
if not opt_levels:
    fatal("--param msil_opt_levels is empty")

# msil_require_exec=1: msil/test/M1/lit.local.cfg makes a missing msil-exec a
# configuration error instead of UNSUPPORTED (the gate legs pass it).
config.msil_require_exec = bool_param("msil_require_exec")
roundtrip = bool_param("msil_roundtrip")  # see "round trip" below
wsl_ilasm_param = params.get("msil_wsl_ilasm")  # see "Linux ilasm in WSL"

# --param msil_pipeline=asm|lto|obj (plan 5.12.1, 5.12.2; feature
# msil-pipeline=<p>) decides how %{msil_c}, %{msil_ll} and their per-level
# forms build a test: obj (M20; the default from M23, Q31) as %{msil_prog}
# of the one source, clang -c and the link through msil-ld, with %{msil_ll}
# compiled by 'llc -filetype=obj'; asm (the default until M23, now the asm
# oracle) as in Phase 1, clang -S (or llc) and ilasm -dll; lto as
# %{msil_prog} of the one C source through clang -flto -c, while %{msil_ll}
# stays on llc text. %{msil_prog} (multi-TU programs, see "linker" below)
# has no text linker from M20: it is obj's under asm and obj and lto's under
# lto, which the feature msil-prog-pipeline=<p> names; a check that depends
# on how %{msil_prog} builds is guarded by msil-prog-pipeline=<p>, never by
# msil-pipeline=<p>, which decides only %{msil_c} and %{msil_ll}.
pipeline_param = params.get("msil_pipeline")
pipeline = pipeline_param or "obj"
if pipeline not in ("asm", "lto", "obj"):
    fatal("--param msil_pipeline=%s: use asm, lto or obj" % pipeline)
features.add("msil-pipeline=" + pipeline)
prog_pipeline_name = "lto" if pipeline == "lto" else "obj"  # M20 (M14: lto)
features.add("msil-prog-pipeline=" + prog_pipeline_name)
# --param msil_link_output=pe|il (feature msil-link-output=<x>, plan
# 5.12.1): pe (the default from M20) has msil-ld write the program image; il
# has it write ILAsm text (--lto-emit-asm) that ilasm -exe assembles, which
# only an LTO link can (obj, and with it %{msil_prog} under asm, is always
# pe).
link_output = params.get("msil_link_output", "pe")
if link_output not in ("pe", "il"):
    fatal("--param msil_link_output=%s: use pe or il" % link_output)
if link_output == "il" and prog_pipeline_name == "obj":
    fatal(
        "--param msil_link_output=il: under msil_pipeline=%s %%{msil_prog} links "
        "CIL objects, whose image msil-ld writes itself (plan 5.12.1); use pe, or "
        "msil_pipeline=lto" % pipeline
    )
features.add("msil-link-output=" + link_output)
# --param msil_equiv=1 (plan 5.12.2, 5.12.6; feature msil-equiv): the
# composites also run the equivalence oracles. Under obj, %{msil_c} and
# %{msil_ll} (one translation unit) build the -S text the Phase 1 way with
# the same output and assembly names, compare its ilasm image canonically
# with msil-ld's --no-gc-sections image (O3, %msil_equiv) and require the
# PEVerify /md messages of the image that runs to be among the ilasm image's
# (O6, msil-verify.py --md-baseline); under lto with msil_link_output=pe,
# %{msil_prog} compares msil-ld's --no-gc-sections image with ilasm's image
# of the same link's --lto-emit-asm text (O4). Under asm, or lto with il,
# there is nothing to compare, so it is a configuration error there.
equiv = bool_param("msil_equiv")
if equiv:
    if pipeline == "asm" or link_output != "pe":
        fatal(
            "--param msil_equiv=1 compares msil-ld's images with ilasm's: it needs "
            "msil_pipeline=obj (O3, O6) or msil_pipeline=lto with "
            "msil_link_output=pe (O4), not msil_pipeline=%s with msil_link_output=%s"
            % (pipeline, link_output)
        )
    features.add("msil-equiv")
# --param msil_scale_tests=1 (plan 5.12.2; feature msil-scale-tests): the
# tests at the real CLR limits (msil/test/M2/00-link/spill-limits.test),
# which only lit-legs.ps1's obj leg runs.
if bool_param("msil_scale_tests"):
    features.add("msil-scale-tests")
# msil_require_link=1 (every gate leg passes it): a missing msil-ld or llvm-ar
# is a configuration error, and msil/test/M2/lit.local.cfg makes missing
# execution support one, instead of an UNSUPPORTED M2.
config.msil_require_link = bool_param("msil_require_link")

# ---------------------------------------------------------------- the target
for t in config.targets_to_build:
    features.add(t.lower() + "-registered-target")  # msil-registered-target


def default_msil_triple():
    arch = {
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "arm64": "aarch64",
        "aarch64": "aarch64",
    }.get(platform.machine().lower(), "x86_64")
    if runner == "wsl":
        return "x86_64-unknown-linux-gnu-msil"  # what WSL runs (plan D6)
    if is_windows:
        return arch + "-pc-windows-msvc-msil"
    if platform.system() == "Darwin":
        return arch + "-apple-darwin-msil"
    return arch + "-unknown-linux-gnu-msil"


msil_triple = params.get("msil_triple") or default_msil_triple()
if not msil_triple.endswith("-msil"):
    fatal("--param msil_triple=%s does not end in -msil" % msil_triple)
native_triple = msil_triple[: -len("-msil")]
triple_parts = native_triple.split("-")
triple_arch = triple_parts[0]
features.add("msil-triple=" + msil_triple)
# These follow the effective triple, never the host: the WSL runner executes a
# linux triple on Windows, so a Windows-only P/Invoke test must REQUIRE
# msil-triple-windows, not system-windows.
if "windows" in triple_parts:
    features.add("msil-triple-windows")
    triple_os = "windows"
elif "linux" in triple_parts:
    features.add("msil-triple-linux")
    triple_os = "linux"
elif any(p.startswith(("darwin", "macos")) for p in triple_parts):
    triple_os = "darwin"
else:
    triple_os = triple_parts[2] if len(triple_parts) > 2 else "unknown"
if runner == "wsl" and triple_os != "linux":
    lit_config.warning("msil: msil_runner=wsl with the non-Linux triple " + msil_triple)
note("triple %s, runner %s" % (msil_triple, runner))

# ---------------------------------------------------------------- dotnet (host)
dotnet = find_tool(
    "dotnet",
    fallback=[
        os.path.join(os.environ.get("DOTNET_ROOT", ""), "dotnet" + exe_ext),
        *(
            [r"C:\Program Files\dotnet\dotnet.exe"]
            if is_windows
            else [
                "/usr/share/dotnet/dotnet",
                "/usr/lib/dotnet/dotnet",
                "/usr/local/share/dotnet/dotnet",
            ]
        ),
        os.path.join(home, ".dotnet", "dotnet" + exe_ext),
    ],
)
local_runtimes = {}
if dotnet:
    rc, listing = run_quietly([dotnet, "--list-runtimes"], 120)
    local_runtimes = list_runtimes(listing) if rc == 0 else {}
    note(
        "dotnet: %s (Microsoft.NETCore.App %s)"
        % (
            dotnet,
            ", ".join(
                v
                for v, _ in sorted(
                    local_runtimes.values(), key=lambda x: version_key(x[0])
                )
            )
            or "none",
        )
    )
else:
    note("dotnet not found")

# ---------------------------------------------------------------- ilasm
windir = os.environ.get("WINDIR") or os.environ.get("SystemRoot") or r"C:\Windows"
netfx_dir = os.path.join(windir, "Microsoft.NET", "Framework64", "v4.0.30319")
host_rid = {"Windows": "win", "Linux": "linux", "Darwin": "osx"}.get(
    platform.system(), ""
) + ("-arm64" if platform.machine().lower() in ("arm64", "aarch64") else "-x64")


_nuget_roots = None


def nuget_roots():
    """The NuGet global-packages folders to search, most specific first."""
    global _nuget_roots
    if _nuget_roots is None:
        _nuget_roots = [
            os.environ.get("NUGET_PACKAGES"),
            os.path.join(home, ".nuget", "packages"),
        ]
        if dotnet:
            # dotnet itself resolves the configured folder (D:/packages/NuGet
            # on the development host); this config never reads NuGet.Config.
            rc, out = run_quietly(
                [dotnet, "nuget", "locals", "global-packages", "--list"], 120
            )
            m = (
                re.search(r"^global-packages:\s*(.+?)\s*$", out or "", re.M)
                if rc == 0
                else None
            )
            if m:
                _nuget_roots.insert(1, m.group(1))
    unique = {}
    for r in filter(None, _nuget_roots):
        unique.setdefault(os.path.normcase(os.path.normpath(r)), r)
    return list(unique.values())


def nuget_tool_path(root, tool, rid, version):
    # NuGet lower-cases package ids and versions in the global-packages folder.
    return os.path.join(
        root,
        "runtime.%s.microsoft.netcore.%s" % (rid, tool),
        version,
        "runtimes",
        rid,
        "native",
        tool + (".exe" if rid.startswith("win-") else ""),
    )


def nuget_tools(tool, rid=host_rid):
    """runtime.<rid>.Microsoft.NETCore.<ILAsm|ILDAsm> (tool 'ilasm' or 'ildasm')
    from the NuGet global-packages folder (the only ilasm and ildasm on Linux
    and macOS), newest version first. rid defaults to the host's."""
    hits = []
    for root in nuget_roots():
        hits += glob.glob(nuget_tool_path(root, tool, rid, "*"))
    return [
        h for h in sorted(set(hits), key=version_key, reverse=True) if is_runnable(h)
    ]


def tools_proj_version(name):
    """The version property <name> of msil/utils/tools/msil-tools.proj (read
    the way msil/utils/ci/ci-common.ps1 and msil-ci.yml read it; the project
    file is the only place that sets it)."""
    proj = os.path.join(utils_dir, "tools", "msil-tools.proj")
    try:
        with open(proj, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        fatal("cannot read %s: %s" % (proj, e))
    m = re.search(r"<%s[^>]*>\s*([0-9.]+)\s*</%s>" % (name, name), text)
    if not m:
        fatal("cannot read %s from %s" % (name, proj))
    return m.group(1)


def pinned_tools_version():
    """MsilILAsmVersion of msil/utils/tools/msil-tools.proj: the ilasm and
    ildasm version that the CI restores and the gate legs use."""
    return tools_proj_version("MsilILAsmVersion")


def overridden(name):
    """True if --param msil_<name> or $MSIL_<NAME> is set (even to empty)."""
    return (
        params.get("msil_" + name) is not None or "MSIL_" + name.upper() in os.environ
    )


if params.get("msil_ilasm", "").lower() == "nuget":
    # --param msil_ilasm=nuget: the modern ilasm from NuGet instead of the
    # .NET Framework one (plan D31, the M10 assembler leg).
    found = nuget_tools("ilasm")
    if not found:
        fatal(
            "msil_ilasm=nuget: no runtime.%s.microsoft.netcore.ilasm package found"
            % host_rid
        )
    ilasm = os.path.normpath(found[0])
else:
    framework_ilasm = [os.path.join(netfx_dir, "ilasm.exe")] if is_windows else []
    ilasm = find_tool("ilasm", preferred=framework_ilasm)
    if not ilasm and not overridden("ilasm"):
        found = nuget_tools("ilasm")
        ilasm = os.path.normpath(found[0]) if found else None
if ilasm:
    features.add("msil-ilasm")
    note("ilasm: " + ilasm)
else:
    note("ilasm not found; execution tests are UNSUPPORTED")
ilasm_flags = {"x86_64": " -pe64 -x64", "aarch64": " -pe64 -arm64"}.get(triple_arch, "")
# For the stand-ins of the harness self-tests (harness/prog,
# harness/pipeline-obj), which assemble their programs themselves.
config.msil_ilasm = ilasm
config.msil_ilasm_flags = ilasm_flags.split()

# ---------------------------------------------------------------- execution runtime
# --param msil_wsl_dotnet (plan 5.12.8, Q39): the dotnet host inside WSL that
# runs the programs and jitall with msil_runner=wsl. 'private' is
# $HOME/.dotnet-msil/dotnet, the runtimes that msil/utils/msil-wsl-dotnet.sh
# installs without root, so an unattended upgrade of the distribution's
# packages never changes the runtime under test; an explicit 'private' that
# is not installed is a configuration error. An absolute WSL path names
# another host. Unset: 'private' when it is installed, else the dotnet on the
# distribution's PATH, with a note. Every WSL leg of lit-legs.ps1 passes
# msil_wsl_dotnet=private.
wsl_dotnet = None
wsl_dotnet_param = params.get("msil_wsl_dotnet")
if wsl_dotnet_param is not None:
    if runner != "wsl":
        fatal("--param msil_wsl_dotnet needs --param msil_runner=wsl")
    if wsl_dotnet_param != "private" and not wsl_dotnet_param.startswith("/"):
        fatal(
            "--param msil_wsl_dotnet=%s: use private or an absolute path inside WSL"
            % wsl_dotnet_param
        )
if runner == "wsl":
    wsl = lit.util.which("wsl.exe") or os.path.join(windir, "System32", "wsl.exe")
    # msil-run.py runs itself inside WSL (python3) to apply its limits there.
    # The probe picks the dotnet host ($1: private, auto or a path), says
    # which kind it is and lists its runtimes.
    probe = (
        "command -v python3 >/dev/null || { echo 'python3 not found'; exit 3; }; "
        'p="$HOME/.dotnet-msil/dotnet"; '
        'case "$1" in '
        'private) if [ -x "$p" ]; then d=$p; k=private; '
        'else echo "missing: $p"; exit 4; fi ;; '
        'auto) if [ -x "$p" ]; then d=$p; k=private; '
        "else d=$(command -v dotnet) || { echo 'dotnet not found on PATH'; exit 5; }; "
        "k=path; fi ;; "
        '*) d=$1; k=explicit; [ -x "$d" ] || { echo "not executable: $d"; exit 6; } ;; '
        "esac; "
        'echo "dotnet: $d"; echo "kind: $k"; "$d" --list-runtimes'
    )
    try:
        res = subprocess.run(
            [
                wsl,
                "-d",
                wsl_distro,
                "--exec",
                "sh",
                "-c",
                probe,
                "sh",
                wsl_dotnet_param or "auto",
            ],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=300,
        )
        rc, out, err = res.returncode, decode(res.stdout), decode(res.stderr)
    except (OSError, subprocess.SubprocessError) as e:
        rc, out, err = None, "", str(e)
    if rc == 4:
        fatal(
            "--param msil_wsl_dotnet=private, but the private .NET runtime is not "
            "installed in WSL %s (%s): run 'wsl -d %s --cd <repository> --exec bash "
            "msil/utils/msil-wsl-dotnet.sh' (plan 5.12.8)"
            % (wsl_distro, out.strip(), wsl_distro)
        )
    m = re.search(r"^dotnet: (/\S+)$", out, re.M)
    kind = re.search(r"^kind: (\w+)$", out, re.M)
    if rc != 0 or not m or not kind:
        fatal(
            "msil_runner=wsl, but probing the dotnet host in WSL %s "
            "(msil_wsl_dotnet=%s) failed (status %s):\n%s%s"
            % (wsl_distro, wsl_dotnet_param or "<unset>", rc, out, err)
        )
    wsl_dotnet = m.group(1)
    exec_runtimes = list_runtimes(out)
    features.add("msil-wsl")
    versions = (
        ", ".join(
            v
            for v, _ in sorted(exec_runtimes.values(), key=lambda x: version_key(x[0]))
        )
        or "none"
    )
    if kind.group(1) == "private":
        note(
            "WSL dotnet (private): %s (WSL %s, Microsoft.NETCore.App %s)"
            % (wsl_dotnet, wsl_distro, versions)
        )
    elif kind.group(1) == "path":
        note(
            "WSL dotnet: %s (WSL %s, Microsoft.NETCore.App %s), the distribution's "
            "own from PATH: $HOME/.dotnet-msil has no private runtime "
            "(msil/utils/msil-wsl-dotnet.sh installs it)"
            % (wsl_dotnet, wsl_distro, versions)
        )
    else:
        note(
            "WSL dotnet: %s (WSL %s, Microsoft.NETCore.App %s)"
            % (wsl_dotnet, wsl_distro, versions)
        )
    default_major = 10  # WSL runs .NET 10 (plan D22)
else:
    exec_runtimes = local_runtimes
    default_major = 8  # .NET 8 by default (plan D22)
for major in exec_runtimes:
    features.add("dotnet-runtime-%d" % major)

wanted = params.get("msil_dotnet_runtime")
if wanted:
    if not wanted.isdigit() or int(wanted) not in exec_runtimes:
        fatal(
            "--param msil_dotnet_runtime=%s: no Microsoft.NETCore.App %s.x for the %s runner"
            " (installed: %s)"
            % (
                wanted,
                wanted,
                runner,
                ", ".join(str(m) for m in sorted(exec_runtimes)) or "none",
            )
        )
    exec_major = int(wanted)
elif exec_runtimes:
    exec_major = default_major if default_major in exec_runtimes else max(exec_runtimes)
else:
    exec_major = None

exec_runtimeconfig = None
if exec_major is not None and (runner == "wsl" or dotnet):
    features.add("msil-dotnet")
    exec_runtimeconfig = write_runtimeconfig(exec_major)
    note("programs run on Microsoft.NETCore.App %s" % exec_runtimes[exec_major][0])
else:
    note("no usable .NET runtime; execution tests are UNSUPPORTED")

# ---------------------------------------------------------------- more runtimes
# --param msil_also_run=<entry>[,<entry>...] (plan 5.12.2, 5.12.10; M23):
# each entry is <major> or <major>+tc0. After the main run, every composite
# repeats jitall and the run of the same verified image once per entry, on
# Microsoft.NETCore.App <major>.x of the runner (its own runtimeconfig.json)
# and, for +tc0, with DOTNET_TieredCompilation=0 (the optimizing JIT, plan
# D38), against the same expected file, each into <t>.<L>.also-net<major>
# [-tc0].out. So one verified build serves several runtimes (the .NET 10 and
# TieredCompilation=0 runs of the net8 and wsl-net10 legs, plan 5.12.11).
# The leg's msil_dotnet_env applies to these runs too. Feature
# msil-also-run. An entry that is not <major> or <major>+tc0, or that comes
# twice, is a configuration error; so is a runtime that the runner lacks
# under msil_require_exec=1 (without it, a note, and the entry is left out).
# The .NET Framework run stays msil_run_netfx.
# A test with explicit RUN lines follows msil_also_run with two names
# (plan 5.12.3): it REDEFINEs the placeholder %{msil_also_run_image} (default
# msil-also-run-image-not-set, so a test that leaves it fails) to its image,
# a path or a placeholder of its own, and appends %{msil_also_run_check}
# after its main run's FileCheck. Per entry, in the parameter's order, that
# expands to jitall (under %if msil-jitall) and the run of the image with
# the entry's runtimeconfig.json and environment, as above, into
# <image>.also-net<major>[-tc0].out, then FileCheck --match-full-lines of
# that transcript with the test's own check lines; without msil_also_run it
# expands to nothing.
also_run = []  # [(major, tc0)]
for entry in params.get("msil_also_run", "").split(","):
    entry = entry.strip()
    if not entry:
        continue
    m = re.match(r"^(\d+)(\+tc0)?$", entry)
    if not m:
        fatal(
            "--param msil_also_run: %r is not <major> or <major>+tc0 (for example "
            "msil_also_run=10,8+tc0)" % entry
        )
    item = (int(m.group(1)), bool(m.group(2)))
    if item in also_run:
        fatal("--param msil_also_run names %s twice" % entry)
    if item[0] not in exec_runtimes:
        msg = (
            "--param msil_also_run=%s: no Microsoft.NETCore.App %d.x for the %s "
            "runner (installed: %s)"
            % (
                entry,
                item[0],
                runner,
                ", ".join(str(x) for x in sorted(exec_runtimes)) or "none",
            )
        )
        if config.msil_require_exec:
            fatal(msg + "; msil_require_exec=1 makes it a configuration error")
        note(msg + "; the entry is left out")
        continue
    also_run.append(item)
if also_run:
    features.add("msil-also-run")
    note(
        "also run on %s"
        % ", ".join(
            "Microsoft.NETCore.App %s%s"
            % (exec_runtimes[major][0], " with DOTNET_TieredCompilation=0" if tc0 else "")
            for major, tc0 in also_run
        )
    )

# ---------------------------------------------------------------- Linux ilasm in WSL
# --param msil_wsl_ilasm=PATH (with msil_runner=wsl; the Linux assembler leg
# of plan M10): %ilasm runs this Linux ilasm, the NuGet package
# runtime.linux-x64.Microsoft.NETCore.ILAsm that the Linux CI job uses, inside
# WSL through 'msil-run.py --tool --runner wsl', which translates the paths
# and runs an executable copy from a private Linux temporary directory. The
# copy is a precaution: on WSL's default DrvFs mount (no 'metadata' option)
# the ilasm runs in place, but a DrvFs mount with 'metadata' and a restrictive
# fmask (or a package extracted without the execute bit) makes the original
# non-executable. msil_wsl_ilasm=nuget takes the newest one in the NuGet
# global-packages folder; an empty value leaves %ilasm on the host.
# msil-verify.py's neutralized pass keeps reassembling with the host ilasm.
wsl_ilasm = None
if wsl_ilasm_param:
    if runner != "wsl":
        fatal("--param msil_wsl_ilasm needs --param msil_runner=wsl")
    if wsl_ilasm_param.lower() == "nuget":
        found = nuget_tools("ilasm", "linux-x64")
        if not found:
            fatal(
                "msil_wsl_ilasm=nuget: no runtime.linux-x64.microsoft.netcore.ilasm "
                "package found ('dotnet restore msil/utils/tools/msil-tools.proj "
                "-p:MsilToolsRid=linux-x64' fetches it)"
            )
        wsl_ilasm = os.path.normpath(found[0])
    else:
        wsl_ilasm = os.path.abspath(wsl_ilasm_param)
    if not os.path.isfile(wsl_ilasm):
        fatal("--param msil_wsl_ilasm=%s: no such file" % wsl_ilasm_param)
    # Fail here, not in every test, when it cannot assemble inside WSL (a
    # missing libstdc++, say).
    os.makedirs(config.test_exec_root, exist_ok=True)
    probe_dir = tempfile.mkdtemp(prefix="msil-wsl-ilasm-", dir=config.test_exec_root)
    try:
        probe_il = os.path.join(probe_dir, "probe.il")
        probe_dll = os.path.join(probe_dir, "probe.dll")
        with open(probe_il, "w", encoding="utf-8", newline="\n") as f:
            f.write(".assembly extern mscorlib {}\n.assembly probe {}\n")
        rc, out = run_quietly(
            [
                config.python_executable,
                os.path.join(utils_dir, "msil-run.py"),
                "--tool",
                "--runner",
                "wsl",
                "--wsl-distro",
                wsl_distro,
                "--timeout",
                "%g" % timeout,
                "--mem-limit-mb",
                str(mem_limit_mb),
                "--",
                wsl_ilasm,
                "-nologo",
                "-quiet",
                "-dll",
                *ilasm_flags.split(),
                "-output=" + probe_dll,
                probe_il,
            ],
            timeout + 300,
        )
        assembled = rc == 0 and os.path.isfile(probe_dll)
    finally:
        shutil.rmtree(probe_dir, ignore_errors=True)
    if not assembled:
        fatal(
            "--param msil_wsl_ilasm=%s cannot assemble inside WSL %s (status %s):\n%s"
            % (wsl_ilasm_param, wsl_distro, rc, out)
        )
    features.add("msil-wsl-ilasm")
    note("ilasm inside WSL %s: %s" % (wsl_distro, wsl_ilasm))

# ILVerify runs on the host and takes its reference assemblies from a host
# runtime, of the same major as the programs when it is installed. jitall runs
# where the programs run (see the jitall section): on the host with the local
# runner, inside WSL with msil_runner=wsl.
if local_runtimes:
    local_major = (
        exec_major
        if exec_major in local_runtimes
        else 8
        if 8 in local_runtimes
        else max(local_runtimes)
    )
    local_runtime_dir = local_runtimes[local_major][1]
    local_runtimeconfig = write_runtimeconfig(local_major)
else:
    local_major = local_runtime_dir = local_runtimeconfig = None

# ---------------------------------------------------------------- verifiers
ilverify = find_tool(
    "ilverify", fallback=[os.path.join(home, ".dotnet", "tools", "ilverify" + exe_ext)]
)
if ilverify:
    # dotnet-ilverify 10.x is itself a net10.0 app; make sure it starts.
    rc, out = run_quietly([ilverify, "--version"], 120)
    if rc != 0:
        note(
            "ilverify at %s does not run (%s); ILVerify disabled"
            % (ilverify, out.strip())
        )
        ilverify = None
if ilverify and local_runtime_dir:
    features.add("msil-ilverify")
    note("ilverify: %s (references from %s)" % (ilverify, local_runtime_dir))


def netfx_tools(name):
    """<name> from the Windows SDK's 'NETFX 4.x Tools' x64 directory."""
    sdk = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    hits = glob.glob(
        os.path.join(
            sdk,
            "Microsoft SDKs",
            "Windows",
            "v*",
            "bin",
            "NETFX 4.* Tools",
            "x64",
            name,
        )
    )
    return sorted(hits, key=version_key, reverse=True)


peverify = None
if is_windows:
    peverify = find_tool("peverify", fallback=netfx_tools("PEVerify.exe"))
    if peverify:
        features.add("msil-peverify")
        note("peverify: " + peverify)

# ildasm disassembles each assembly for the second pass of msil-verify.py,
# which verifies a copy without the allowed constructs (an allowed first error
# hides every later one from both verifiers); ilasm reassembles the copy. The
# .NET Framework ildasm sits next to PEVerify; the NuGet package
# runtime.<rid>.Microsoft.NETCore.ILDAsm is the only one on Linux.
ildasm = find_tool("ildasm", preferred=netfx_tools("ildasm.exe") if is_windows else [])
if not ildasm and not overridden("ildasm"):
    found = nuget_tools("ildasm")
    ildasm = os.path.normpath(found[0]) if found else None
if ildasm:
    features.add("msil-ildasm")
    note("ildasm: " + ildasm)
else:
    note("ildasm not found; msil-verify.py skips its neutralized pass")

# ---------------------------------------------------------------- round trip
# --param msil_roundtrip=1 (Design.md 5.5: the .il output must round-trip
# through ilasm and ildasm; the round-trip leg of plan M10): %{msil_c} and
# %{msil_ll} also disassemble each module, reassemble the listing, require the
# two listings to match (msil-verify.py --roundtrip) and run the reassembled
# image against the same .expected file. Both tools are the .NET ones from the
# NuGet global-packages folder (runtime.<rid>.Microsoft.NETCore.ILDAsm and
# .ILAsm) at the version that msil/utils/tools/msil-tools.proj pins
# (MsilILAsmVersion; the modern-ilasm and wsl-ilasm legs use the same one),
# never whatever newer version another project restored into a shared folder;
# a missing one is a configuration error that names the version. The
# .NET Framework ildasm prints infinite FP constants as 'inf', which neither
# the .NET Framework ilasm nor the .NET ilasm 10.0.12 reads back (recon
# cil-runtime.md 15).
rt_ildasm = rt_ilasm = None
if roundtrip:
    rt_version = pinned_tools_version()
    rt_tools = {}
    for tool in ("ildasm", "ilasm"):
        candidates = [
            nuget_tool_path(root, tool, host_rid, rt_version) for root in nuget_roots()
        ]
        rt_tools[tool] = next((c for c in candidates if is_runnable(c)), None)
    missing = [t for t in ("ildasm", "ilasm") if not rt_tools[t]]
    if missing:
        fatal(
            "--param msil_roundtrip=1 needs the .NET %s %s (MsilILAsmVersion of "
            "msil/utils/tools/msil-tools.proj) in the NuGet global-packages folder "
            "(searched %s; 'dotnet restore msil/utils/tools/msil-tools.proj "
            "-p:MsilToolsRid=%s' fetches runtime.%s.Microsoft.NETCore.ILDAsm and "
            ".ILAsm %s)"
            % (
                " and ".join(missing),
                rt_version,
                ", ".join(nuget_roots()) or "nothing",
                host_rid,
                host_rid,
                rt_version,
            )
        )
    rt_ildasm = os.path.normpath(rt_tools["ildasm"])
    rt_ilasm = os.path.normpath(rt_tools["ilasm"])
    features.add("msil-roundtrip")
    note("round trip (%s): %s, %s" % (rt_version, rt_ildasm, rt_ilasm))

if is_windows and os.path.isfile(os.path.join(netfx_dir, "clr.dll")):
    features.add("msil-netfx")
if run_netfx:
    if runner != "local":
        fatal("--param msil_run_netfx=1 needs msil_runner=local")
    if "msil-netfx" not in features:
        fatal("--param msil_run_netfx=1, but the .NET Framework 4.x is not installed")
    features.add("msil-run-netfx")

# ---------------------------------------------------------------- jitall
# build-jitall.py (M0.c) compiles msil/utils/jitall/jitall.cs offline on the
# host and does nothing when the output is up to date; --param msil_jitall=
# disables jitall. JIT-compiling a method of <Module> runs <Module>::.cctor
# (the data segment and C constructors) first, so jitall runs where the
# programs run: a linux-triple initializer calls libc and libm, which exist
# only inside WSL. With msil_runner=wsl, msil-jitall.py runs jitall.dll in WSL
# on the runtime the programs use.
jitall_param = params.get("msil_jitall")
jitall_dir = None
if jitall_param != "":
    jitall_dir = (
        os.path.abspath(jitall_param)
        if jitall_param
        else os.path.join(config.llvm_obj_root, "tools", "msil", "jitall")
    )
    build_jitall = os.path.join(utils_dir, "jitall", "build-jitall.py")
    if dotnet and os.path.isfile(build_jitall):
        rc, out = run_quietly(
            [sys.executable, build_jitall, "--out", jitall_dir, "--dotnet", dotnet], 600
        )
        if rc == 0:
            note(out.strip().replace("\n", "; "))
        else:
            lit_config.warning(
                "msil: %s failed (status %s):\n%s" % (build_jitall, rc, out)
            )
    jitall_runtime = (
        exec_runtimeconfig if runner == "wsl" else (dotnet and local_runtimeconfig)
    )
    if jitall_runtime and os.path.isfile(os.path.join(jitall_dir, "jitall.dll")):
        features.add("msil-jitall")
    else:
        note(
            "jitall is not available in %s (or no runtime for the %s runner)"
            % (jitall_dir, runner)
        )

# ---------------------------------------------------------------- mdcanon
# mdcanon (msil/utils/mdtools/mdcanon.cs, plan 5.7.11) prints canonical
# listing v1 (plan 5.7.10), the layout-independent text of an assembly's
# metadata and IL, over System.Reflection.Metadata; from M17 llvm-cil-dump
# --canonical prints the same text. --param msil_mdcanon (plan 5.12.2):
# unset, lit builds mdump and mdcanon into <build>/tools/msil/mdtools with
# msil/utils/mdtools/build-mdtools.py, offline and only when a source
# changed, as it builds jitall; a directory names a build of the tools that
# lit uses as it is (it must hold mdcanon.dll and its runtimeconfig.json);
# empty disables mdcanon. Feature msil-mdcanon. mdcanon only reads files, so
# it runs on the host's dotnet (any runtime from the runtimeconfig's major
# on, rollForward Major), also with msil_runner=wsl. Under
# msil_require_verify=1 a failed build is a configuration error, and so is
# an mdcanon that is disabled or cannot run (see below).
mdcanon_param = params.get("msil_mdcanon")
mdcanon_dir = None
mdcanon_why = None  # why the feature msil-mdcanon is missing
build_mdtools = os.path.join(utils_dir, "mdtools", "build-mdtools.py")
if mdcanon_param == "":
    mdcanon_why = "disabled by an empty --param msil_mdcanon="
elif mdcanon_param:
    mdcanon_dir = os.path.abspath(mdcanon_param)
else:
    mdcanon_dir = os.path.join(config.llvm_obj_root, "tools", "msil", "mdtools")
    if not dotnet:
        mdcanon_why = "no dotnet host to build and run it"
    elif not os.path.isfile(build_mdtools):
        mdcanon_why = "%s is missing" % slashes(build_mdtools)
    else:
        rc, out = run_quietly(
            [sys.executable, build_mdtools, "--out", mdcanon_dir, "--dotnet", dotnet],
            600,
        )
        if rc == 0:
            note(out.strip().replace("\n", "; "))
        else:
            msg = "msil: %s failed (status %s):\n%s" % (build_mdtools, rc, out)
            if require_verify:
                lit_config.fatal(
                    msg + "\nmsil_require_verify=1 makes a failed mdtools build a "
                    "configuration error (plan 5.12.2)"
                )
            lit_config.warning(msg)
            # What the directory holds may predate the sources: not used.
            mdcanon_why = "build-mdtools.py failed (see the warning above)"
mdcanon_dll = mdcanon_config = None
if mdcanon_dir and not mdcanon_why:
    mdcanon_dll = os.path.join(mdcanon_dir, "mdcanon.dll")
    mdcanon_config = os.path.join(mdcanon_dir, "mdcanon.runtimeconfig.json")
    lacking = [p for p in (mdcanon_dll, mdcanon_config) if not os.path.isfile(p)]
    if lacking and mdcanon_param:
        fatal(
            "--param msil_mdcanon=%s has no %s; build the tools there with "
            "'python %s --out %s'"
            % (
                mdcanon_param,
                " or ".join(os.path.basename(p) for p in lacking),
                slashes(build_mdtools),
                slashes(mdcanon_dir),
            )
        )
    if lacking:
        mdcanon_why = "%s was not built" % slashes(mdcanon_dll)
    elif not dotnet:
        mdcanon_why = "no dotnet host to run it"
    else:
        # The runtimeconfig names Microsoft.NETCore.App <major>.0.0 with
        # rollForward Major: any host runtime of that major or newer runs it.
        try:
            with open(mdcanon_config, encoding="utf-8") as f:
                framework = json.load(f)["runtimeOptions"]["framework"]
            need = version_key(framework["version"])[0]
        except (OSError, ValueError, KeyError, TypeError) as e:
            fatal("cannot read %s: %s" % (slashes(mdcanon_config), e))
        if any(major >= need for major in local_runtimes):
            features.add("msil-mdcanon")
            note("mdcanon: %s (host dotnet %s)" % (slashes(mdcanon_dll), dotnet))
        else:
            mdcanon_why = (
                "the host dotnet %s has no Microsoft.NETCore.App %d.x or newer "
                "for it" % (dotnet, need)
            )
if mdcanon_why:
    note("mdcanon is not available: " + mdcanon_why)

# ---------------------------------------------------------------- llvm-cil-dump
# llvm-cil-dump (llvm/tools/llvm-cil-dump, plan 5.7.10; M17) prints canonical
# listing v1 with --canonical, the same bytes as mdcanon, and an image's
# headers, tables and link-info record. --param msil_cil_dump (or
# $MSIL_CIL_DUMP) names it (plan 5.12.2); by default it is the tree's
# <build>/bin/llvm-cil-dump, never one from PATH; empty disables it. Feature
# msil-cil-dump, substitution %msil_cil_dump; with the feature, %msil_canon is
# 'llvm-cil-dump --canonical' instead of mdcanon (see %msil_mdcanon below).
# A named path that does not exist is a configuration error. From M17 it is
# the lister of %msil_canon, so under msil_require_verify=1 a disabled or
# missing llvm-cil-dump is a configuration error too, as mdcanon is (plan
# section 12, '5.12.2 and M17.g clarified'); without that parameter
# %msil_canon falls back to mdcanon. It only reads files, so it runs on the
# host, also with msil_runner=wsl.
cil_dump_param = params.get("msil_cil_dump")
if cil_dump_param is None:
    cil_dump_param = os.environ.get("MSIL_CIL_DUMP")
msil_cil_dump = None
cil_dump_why = None  # why the feature msil-cil-dump is missing
if cil_dump_param == "":
    cil_dump_why = "disabled by an empty --param msil_cil_dump= or $MSIL_CIL_DUMP"
elif cil_dump_param is not None:
    found = (
        cil_dump_param
        if is_runnable(cil_dump_param)
        else lit.util.which(cil_dump_param)
    )
    if found:
        msil_cil_dump = os.path.normpath(found)
    else:
        cil_dump_why = "msil_cil_dump=%s: no such program" % cil_dump_param
        if not require_verify:
            fatal(cil_dump_why)
else:
    candidate = os.path.join(config.llvm_tools_dir, "llvm-cil-dump" + exe_ext)
    if is_runnable(candidate):
        msil_cil_dump = os.path.normpath(candidate)
    else:
        cil_dump_why = "%s was not built" % slashes(candidate)
if msil_cil_dump:
    features.add("msil-cil-dump")
    note("llvm-cil-dump: %s" % msil_cil_dump)
elif not require_verify:
    note("llvm-cil-dump is not available: %s; %%msil_canon is mdcanon" % cil_dump_why)
# The equivalence oracles O3 and O4 compare %msil_canon listings.
if equiv and not ({"msil-cil-dump", "msil-mdcanon"} & features):
    fatal(
        "--param msil_equiv=1 compares %%msil_canon listings, but neither "
        "llvm-cil-dump (%s) nor mdcanon (%s) is available" % (cil_dump_why, mdcanon_why)
    )

if require_verify:
    # Name the tool and what fixes it. An explicitly empty override is still
    # an error here: a gate must not pass with a step switched off.
    def why(name):
        value = params.get("msil_" + name, os.environ.get("MSIL_" + name.upper()))
        return (
            " (disabled by an empty --param msil_%s= or $MSIL_%s)"
            % (name, name.upper())
            if value == ""
            else ""
        )

    missing = []
    if "msil-ilverify" not in features:
        missing.append(
            "ILVerify%s: install the dotnet tool dotnet-ilverify (it needs the .NET 10 "
            "runtime and a host .NET runtime for references) or pass "
            "--param msil_ilverify=PATH (or set $MSIL_ILVERIFY)" % why("ilverify")
        )
    if not ildasm:
        missing.append(
            "ildasm%s, for the neutralized pass of msil-verify.py: the Windows SDK's "
            "NETFX 4.8 Tools or the NuGet package runtime.%s.Microsoft.NETCore.ILDAsm; "
            "pass --param msil_ildasm=PATH (or set $MSIL_ILDASM)"
            % (why("ildasm"), host_rid)
        )
    if "msil-jitall" not in features:
        missing.append(
            "jitall (%s/jitall.dll, run %s)%s: build it with 'python %s' or pass "
            "--param msil_jitall=DIR"
            % (
                slashes(jitall_dir or "<disabled>"),
                "in WSL" if runner == "wsl" else "on the host",
                " (disabled by an empty --param msil_jitall=)"
                if jitall_param == ""
                else "",
                slashes(os.path.join(utils_dir, "jitall", "build-jitall.py")),
            )
        )
    if "msil-mdcanon" not in features:
        missing.append(
            "mdcanon%s: %s; build it with 'python %s' (a .NET SDK and the "
            "Microsoft.NETCore.App.Ref 8.x targeting pack) or pass "
            "--param msil_mdcanon=DIR"
            % (
                " (%s/mdcanon.dll, run on the host)" % slashes(mdcanon_dir)
                if mdcanon_dir
                else "",
                mdcanon_why,
                slashes(build_mdtools),
            )
        )
    if "msil-cil-dump" not in features:
        missing.append(
            "llvm-cil-dump, the lister of %%msil_canon from M17: %s; build the "
            "llvm-cil-dump target or pass --param msil_cil_dump=PATH (or set "
            "$MSIL_CIL_DUMP)" % cil_dump_why
        )
    if is_windows and "msil-peverify" not in features:
        missing.append(
            "PEVerify%s (Windows SDK, NETFX 4.8 Tools): pass --param "
            "msil_peverify=PATH (or set $MSIL_PEVERIFY)" % why("peverify")
        )
    if not ilasm:
        missing.append(
            "ilasm%s: the .NET Framework ilasm on Windows or the NuGet package "
            "runtime.%s.Microsoft.NETCore.ILAsm; pass --param msil_ilasm=PATH (or "
            "set $MSIL_ILASM)" % (why("ilasm"), host_rid)
        )
    if "msil-dotnet" not in features:
        if runner == "wsl":
            missing.append(
                ".NET runtime: the dotnet host %s in WSL distribution %s has no "
                "Microsoft.NETCore.App (run msil/utils/msil-wsl-dotnet.sh there "
                "for the private runtimes, or pick another distribution with "
                "--param msil_wsl_distro=NAME)" % (wsl_dotnet, wsl_distro)
            )
        else:
            missing.append(
                ".NET runtime%s: %s; install .NET 8, or pass --param "
                "msil_dotnet=PATH (or set $MSIL_DOTNET)"
                % (
                    why("dotnet"),
                    "dotnet %s has no Microsoft.NETCore.App runtime" % dotnet
                    if dotnet
                    else "no dotnet host found",
                )
            )
    if missing:
        fatal(
            "msil_require_verify=1, but tools are missing:\n  " + "\n  ".join(missing)
        )

# ---------------------------------------------------------------- native toolchains
# The Visual Studio LLVM (clang-cl, lld-link, ld.lld and the compiler-rt
# builtins), which msil-env.ps1 and the CI's msvc-dev-cmd put in the
# environment through %VCINSTALLDIR%.
vc_dir = os.environ.get("VCINSTALLDIR") if is_windows else None
vs_llvm = os.path.join(vc_dir, "Tools", "Llvm", "x64") if vc_dir else None


def find_linker(name):
    """%ld_lld and %lld_link (plan 5.12.3): the tree's own lld (M13 builds it),
    else PATH, else the Visual Studio LLVM."""
    candidates = [os.path.join(config.llvm_tools_dir, name + exe_ext)]
    candidates.append(lit.util.which(name))
    if vs_llvm:
        candidates.append(os.path.join(vs_llvm, "bin", name + exe_ext))
    for candidate in candidates:
        if is_runnable(candidate):
            return os.path.normpath(candidate)
    return None


ld_lld = find_linker("ld.lld")
lld_link = find_linker("lld-link")
# Features for tests that build their own native libraries (plan 5.12.5):
# %ld_lld -shared, %lld_link -dll.
if ld_lld:
    features.add("msil-ld-lld")
if lld_link:
    features.add("msil-lld-link")
clang_path = getattr(config, "clang", None)

# --param msil_native_rtlib=auto|PATH (plan 5.12.8): the compiler-rt builtins
# library that the Windows native oracle links, for the i128 helpers
# (__udivti3, __fixdfti, ...) that a native MSVC build calls and that no MSVC
# library has (the fork builds no compiler-rt). 'auto' (the default) takes
# the newest <llvm>/lib/clang/<version>/lib/windows/
# clang_rt.builtins-<arch>.lib of the Visual Studio LLVM (%VCINSTALLDIR%
# Tools\Llvm\x64), else of the LLVM whose clang-cl is on PATH; an empty value
# or 'none' disables it. It applies to MSVC triples on a Windows host only
# (MinGW and Linux link libgcc; E7 links libgcc_s.so.1). Missing under
# msil_native_oracle=required, that native oracle is a configuration error.
native_triple_env = triple_parts[3] if len(triple_parts) > 3 else "msvc"
msvc_native = triple_os == "windows" and native_triple_env.startswith("msvc")
rtlib_param = params.get("msil_native_rtlib", "auto")
native_rtlib = None
rtlib_name = "clang_rt.builtins-%s.lib" % triple_arch
rtlib_why = None
if rtlib_param.lower() in ("", "none"):
    rtlib_why = "disabled by --param msil_native_rtlib=%s" % rtlib_param
elif rtlib_param.lower() == "auto":
    if is_windows and msvc_native:
        roots = [vs_llvm] if vs_llvm else []
        for tool in ("clang-cl", "lld-link"):
            found = lit.util.which(tool)
            if found:
                roots.append(os.path.dirname(os.path.dirname(found)))
        hits = []
        for root in roots:
            hits += glob.glob(
                os.path.join(root, "lib", "clang", "*", "lib", "windows", rtlib_name)
            )

        def clang_dir_key(path):
            # <llvm>/lib/clang/<version>/lib/windows/<lib>: '22' or '17.0.1'
            ver = os.path.basename(
                os.path.dirname(os.path.dirname(os.path.dirname(path)))
            )
            return tuple(int(x) for x in re.findall(r"\d+", ver))

        # The first root wins (Visual Studio's), then the newest version.
        for root in roots:
            mine = [
                h
                for h in hits
                if os.path.normcase(h).startswith(os.path.normcase(root))
            ]
            if mine:
                native_rtlib = os.path.normpath(max(mine, key=clang_dir_key))
                break
        if not native_rtlib:
            rtlib_why = (
                "no %s under %s (msil_native_rtlib=auto; install the Visual Studio "
                "'C++ Clang tools' or pass --param msil_native_rtlib=PATH)"
                % (
                    rtlib_name,
                    " or ".join(
                        slashes(os.path.join(r, "lib", "clang", "*", "lib", "windows"))
                        for r in roots
                    )
                    or "%VCINSTALLDIR%Tools/Llvm/x64 (VCINSTALLDIR is not set)",
                )
            )
else:
    if not os.path.isfile(rtlib_param):
        fatal("--param msil_native_rtlib=%s: no such file" % rtlib_param)
    native_rtlib = os.path.normpath(os.path.abspath(rtlib_param))
if native_rtlib:
    features.add("msil-native-rtlib")
    note("native rtlib: " + native_rtlib)

# --param msil_linux_sysroot=DIR (plan 5.12.2, 5.12.8): the Linux libraries
# of the E7 oracle (and, from M15, msil-ld's --sysroot for Linux links), in
# the layout of msil/utils/make-linux-sysroot.py. Default $MSIL_LINUX_SYSROOT,
# else C:\llvm-msil-scratch\linux-sysroot on Windows when it exists, else '/'
# on Linux; an empty value disables it. An explicit value (parameter or
# variable) without libc.so.6 and libm.so.6 is a configuration error, so a
# leg that names the sysroot cannot quietly lose its oracle. Feature
# msil-linux-sysroot. Feature msil-linux-oracle (E7) also needs
# libgcc_s.so.1 there, ld.lld (%ld_lld), clang with the X86 target and
# start.o, which is compiled here once per lit run into
# <exec root>/linux-oracle/.
LINUX_LIBDIR = ("usr", "lib", "x86_64-linux-gnu")
linux_sysroot = params.get("msil_linux_sysroot")
sysroot_explicit = linux_sysroot is not None
if linux_sysroot is None and "MSIL_LINUX_SYSROOT" in os.environ:
    linux_sysroot = os.environ["MSIL_LINUX_SYSROOT"]
    sysroot_explicit = True
if linux_sysroot is None:
    default_sysroot = r"C:\llvm-msil-scratch\linux-sysroot" if is_windows else "/"
    linux_sysroot = default_sysroot if os.path.isdir(default_sysroot) else ""
linux_libdir = None
if linux_sysroot:
    linux_sysroot = os.path.abspath(linux_sysroot)
    libdir = os.path.join(linux_sysroot, *LINUX_LIBDIR)
    lacking = [
        n
        for n in ("libc.so.6", "libm.so.6")
        if not os.path.isfile(os.path.join(libdir, n))
    ]
    if lacking:
        if sysroot_explicit:
            fatal(
                "msil_linux_sysroot=%s has no %s in usr/lib/x86_64-linux-gnu; "
                "run 'python msil/utils/make-linux-sysroot.py --out %s'"
                % (slashes(linux_sysroot), " or ".join(lacking), slashes(linux_sysroot))
            )
        note("linux sysroot %s is incomplete; ignored" % slashes(linux_sysroot))
    else:
        linux_libdir = libdir
        features.add("msil-linux-sysroot")
        note("linux sysroot: " + slashes(linux_sysroot))

# %{msil_ldflags} (plan 5.12.3, M15): the harness's flags of the MSIL link.
# The link reads the driver's default libraries of the triple (plan 5.10.4),
# which msil-ld resolves from the native libraries: on a Windows triple from
# the import libraries of LIB (msil-env.ps1 or a VS developer prompt), on a
# Linux triple from the sysroot msil_linux_sysroot (msil-ld's --sysroot, the
# libraries the E7 oracle links). Without that sysroot a Linux-triple link
# has no --sysroot, and msil-ld names each library it cannot find.
if triple_os == "linux":
    msil_ldflags = "--sysroot=" + quote(linux_sysroot) if linux_libdir else ""
    if not linux_libdir:
        note(
            "Linux-triple links get no --sysroot: no msil_linux_sysroot "
            "(msil/utils/make-linux-sysroot.py)"
        )
else:
    msil_ldflags = ""

# --param msil_glibc_sysroot=DIR (plan 5.12.2, 5.12.8, Q16): a glibc sysroot
# with headers (usr/include/stdio.h), as msil/utils/make-glibc-sysroot.sh
# makes it, for the optional hosted compile-only tests of
# msil/test/M2/09-hosted (feature msil-glibc-sysroot, substitution
# %{msil_glibc_sysroot}). Default $MSIL_GLIBC_SYSROOT, else
# C:\llvm-msil-scratch\glibc-sysroot on Windows when it holds
# usr/include/stdio.h; an empty value disables it. An explicit value
# (parameter or variable) without usr/include/stdio.h is a configuration
# error, so the hosted leg, which names the sysroot, cannot pass by skipping.
# Feature msil-ucrt-headers: a directory of INCLUDE holds corecrt.h (the
# UCRT headers of a VS developer prompt; lit passes INCLUDE on).
GLIBC_STDIO = ("usr", "include", "stdio.h")
glibc_sysroot = params.get("msil_glibc_sysroot")
glibc_explicit = glibc_sysroot is not None
if glibc_sysroot is None and "MSIL_GLIBC_SYSROOT" in os.environ:
    glibc_sysroot = os.environ["MSIL_GLIBC_SYSROOT"]
    glibc_explicit = True
if glibc_sysroot is None:
    default_glibc = r"C:\llvm-msil-scratch\glibc-sysroot" if is_windows else ""
    glibc_sysroot = (
        default_glibc
        if default_glibc and os.path.isfile(os.path.join(default_glibc, *GLIBC_STDIO))
        else ""
    )
if glibc_sysroot:
    glibc_sysroot = os.path.abspath(glibc_sysroot)
    if not os.path.isfile(os.path.join(glibc_sysroot, *GLIBC_STDIO)):
        if glibc_explicit:
            fatal(
                "msil_glibc_sysroot=%s has no usr/include/stdio.h; make it with "
                "'bash msil/utils/make-glibc-sysroot.sh <dir>' inside WSL "
                "(plan 5.12.8)" % slashes(glibc_sysroot)
            )
        glibc_sysroot = ""
    else:
        features.add("msil-glibc-sysroot")
        note("glibc sysroot: " + slashes(glibc_sysroot))
if any(
    d and os.path.isfile(os.path.join(d, "corecrt.h"))
    for d in os.environ.get("INCLUDE", "").split(os.pathsep)
):
    features.add("msil-ucrt-headers")
    note("UCRT headers found through INCLUDE")

e7_why = None
e7_start = None
if not linux_libdir:
    e7_why = "no Linux sysroot (msil_linux_sysroot; msil/utils/make-linux-sysroot.py)"
elif not os.path.isfile(os.path.join(linux_libdir, "libgcc_s.so.1")):
    e7_why = "%s has no libgcc_s.so.1 (rerun make-linux-sysroot.py)" % slashes(
        linux_libdir
    )
elif not ld_lld:
    e7_why = "no ld.lld (<build>/bin, PATH or %VCINSTALLDIR%Tools/Llvm/x64/bin)"
elif not clang_path:
    e7_why = "clang is not built"
elif "X86" not in config.targets_to_build:
    e7_why = "the X86 target is not built"
else:
    # start.o, once per lit run. Another lit run may be linking with the
    # current copy: replace it only when the new one differs.
    start_dir = os.path.join(config.test_exec_root, "linux-oracle")
    os.makedirs(start_dir, exist_ok=True)
    e7_start = os.path.join(start_dir, "start.o")
    tmp = "%s.%d.tmp" % (e7_start, os.getpid())
    rc, out = run_quietly(
        [
            clang_path,
            "--target=x86_64-unknown-linux-gnu",
            "-ffreestanding",
            "-fno-pic",
            "-O2",
            "-c",
            os.path.join(utils_dir, "linux-oracle", "start.c"),
            "-o",
            tmp,
        ],
        300,
    )
    if rc != 0 or not os.path.isfile(tmp):
        e7_why = "cannot compile msil/utils/linux-oracle/start.c (status %s): %s" % (
            rc,
            out.strip(),
        )
        e7_start = None
    else:
        try:
            with open(tmp, "rb") as f:
                new = f.read()
            try:
                with open(e7_start, "rb") as f:
                    same = f.read() == new
            except OSError:
                same = False
            if not same:
                os.replace(tmp, e7_start)
        except OSError as e:
            e7_why = "cannot write %s: %s" % (e7_start, e)
            e7_start = None
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)
if e7_start:
    features.add("msil-linux-oracle")
    note(
        "E7 Linux oracle available: %s, sysroot %s"
        % (slashes(ld_lld), slashes(linux_sysroot))
    )

# ---------------------------------------------------------------- native oracle
# --param msil_native_oracle=1|required: the composites also build the test
# for the native triple and compare its transcript with the same .expected
# file. With msil_runner=local that is clang for the native triple (and
# msil_native_rtlib on MSVC triples); with msil_runner=wsl it is the E7 Linux
# oracle (feature msil-linux-oracle). 'required' turns every reason it cannot
# run into a configuration error.
oracle = params.get("msil_native_oracle", "0")
oracle_required = oracle.lower() == "required"
e7_oracle = False
if oracle_required or bool_param("msil_native_oracle"):
    host_target = {"x86_64": "X86", "aarch64": "AArch64"}.get(triple_arch)
    problems = []
    if not clang_path:
        problems.append("clang is not built")
    if host_target not in config.targets_to_build:
        problems.append("the %s target is not built" % host_target)
    if runner == "wsl":
        if native_triple != "x86_64-unknown-linux-gnu":
            problems.append(
                "the E7 Linux oracle of msil_runner=wsl builds "
                "x86_64-unknown-linux-gnu only, not %s" % native_triple
            )
        if "msil-linux-oracle" not in features:
            problems.append("the E7 Linux oracle of msil_runner=wsl needs " + e7_why)
    else:
        if is_windows and not os.environ.get("LIB"):
            problems.append("LIB is not set (run from a VS x64 developer prompt)")
        if not is_windows and not lit.util.which("ld"):
            problems.append("no system linker (ld)")
        if is_windows and msvc_native and not native_rtlib:
            problems.append("the Windows native oracle needs compiler-rt: " + rtlib_why)
    if problems:
        msg = "msil: native oracle unavailable: " + "; ".join(problems)
        if oracle_required:
            lit_config.fatal(msg)
        lit_config.warning(msg)
    else:
        features.add("msil-native-oracle")
        if runner == "wsl":
            e7_oracle = True
            note(
                "native oracle: E7 (%s, sysroot %s)"
                % (slashes(ld_lld), slashes(linux_sysroot))
            )
        elif native_rtlib and msvc_native:
            note("native oracle: %s with %s" % (native_triple, slashes(native_rtlib)))
        else:
            note("native oracle: " + native_triple)


# ---------------------------------------------------------------- linker
# msil-ld and the archiver of the multi-TU programs (plan 5.12.2, 5.12.3):
# --param msil_ld, msil_ar and msil_nm (or $MSIL_LD, $MSIL_AR, $MSIL_NM) name
# the tools; by default they are the tree's <build>/bin/msil-ld (lld's msil
# flavor), llvm-ar and llvm-nm, never ones from PATH: the Visual Studio
# llvm-ar cannot index the tree's bitcode. An empty value disables a tool.
# Features msil-ld and msil-ar. An explicit msil_pipeline=lto or obj, whose
# %{msil_c} links through them, and msil_require_link=1 make a missing one a
# configuration error; under the default obj pipeline (no msil_pipeline) a
# missing one only takes away msil-exec, so the execution tests are
# UNSUPPORTED and a tree without lld still runs the rest.
def find_tree_tool(name, exe):
    override = params.get("msil_" + name)
    if override is None:
        override = os.environ.get("MSIL_" + name.upper())
    if override is not None:
        if override == "":
            note("%s disabled by an empty override" % exe)
            return None
        found = override if is_runnable(override) else lit.util.which(override)
        if not found:
            fatal("msil_%s=%s: no such program" % (name, override))
        return os.path.normpath(found)
    candidate = os.path.join(config.llvm_tools_dir, exe + exe_ext)
    return os.path.normpath(candidate) if is_runnable(candidate) else None


msil_ld = find_tree_tool("ld", "msil-ld")
msil_ar = find_tree_tool("ar", "llvm-ar")
msil_nm = find_tree_tool("nm", "llvm-nm")
for tool, path, feature in (
    ("msil-ld", msil_ld, "msil-ld"),
    ("llvm-ar", msil_ar, "msil-ar"),
    ("llvm-nm", msil_nm, None),
):
    if path:
        note("%s: %s" % (tool, path))
        if feature:
            features.add(feature)
    else:
        note("%s not found" % tool)
link_missing = [
    "%s (--param msil_%s=PATH; build the %s target)" % (tool, name, target)
    for tool, name, target, path in (
        ("msil-ld", "ld", "lld", msil_ld),
        ("llvm-ar", "ar", "llvm-ar", msil_ar),
    )
    if not path
]
if link_missing and pipeline_param in ("lto", "obj"):
    fatal(
        "--param msil_pipeline=%s links every C test through msil-ld, but "
        "these are missing: %s" % (pipeline, "; ".join(link_missing))
    )
if link_missing and config.msil_require_link:
    fatal("msil_require_link=1, but these are missing: " + "; ".join(link_missing))
if link_missing and pipeline in ("lto", "obj"):
    note(
        "the default pipeline %s links every C test through msil-ld, but these "
        "are missing: %s; execution tests are UNSUPPORTED (pass --param "
        "msil_pipeline=asm for -S and ilasm)" % (pipeline, "; ".join(link_missing))
    )
note(
    "pipeline %s (%%{msil_prog}: %s, link output %s%s)"
    % (
        pipeline,
        prog_pipeline_name,
        link_output,
        ", equivalence oracles %s" % ("O3, O6" if pipeline == "obj" else "O4")
        if equiv
        else "",
    )
)

# ---------------------------------------------------------------- exit corpus
# The exit corpus (plan 5.12.9, M23): msil/utils/corpus/fetch.py downloads
# the pinned sources of msil/utils/corpus/manifest.json into a cache outside
# the repository, as <cache>/<name>-<ref>/, and after verifying the entries
# it selected writes <cache>/msil-corpus.stamp, JSON {"manifest_sha256":
# <sha256 of manifest.json's bytes>, "trees": {<name>: <tree_sha256>}}.
# --param msil_corpus_dir=DIR (plan 5.12.2) names the cache: default
# $MSIL_CORPUS_CACHE, else fetch.py's default cache
# (C:\llvm-msil-scratch\corpus-cache on Windows, ~/.cache/msil-corpus
# elsewhere); an empty value disables the corpus. Feature msil-corpus only
# when the stamp matches the checked-in manifest (its sha256), and
# msil-corpus-<name> only for each tree the stamp lists with the manifest's
# tree_sha256 whose directory exists; %{msil_corpus} is the cache directory
# ('/' separators, unquoted, so that a test may write it inside
# %{msil_archives} and %{msil_srcs}; a cache path with a blank is not used).
# A missing or stale stamp is a note, and msil/test/Programs is UNSUPPORTED.
# --param msil_require_corpus=1 (the corpus legs) makes it a configuration
# error that names 'fetch.py --cache <dir>', and so is an entry that the
# stamp lacks, unless --param msil_corpus_skip=<name>[,<name>...] names it:
# entries left out on purpose (lit-legs passes $MSIL_CORPUS_SKIP; the CI
# leaves CoreMark and c-testsuite out, plan 5.12.12), whose tests are then
# UNSUPPORTED with a note. A name in msil_corpus_skip that the manifest does
# not have is a configuration error.
require_corpus = bool_param("msil_require_corpus")
corpus_manifest = os.path.join(utils_dir, "corpus", "manifest.json")
corpus_skip = []
for _name in params.get("msil_corpus_skip", "").split(","):
    if _name.strip() and _name.strip() not in corpus_skip:
        corpus_skip.append(_name.strip())
corpus_dir_param = params.get("msil_corpus_dir")
if corpus_dir_param is None:
    corpus_dir_param = os.environ.get("MSIL_CORPUS_CACHE") or (
        r"C:\llvm-msil-scratch\corpus-cache"
        if is_windows
        else os.path.join(home, ".cache", "msil-corpus")
    )
corpus_dir = os.path.abspath(corpus_dir_param) if corpus_dir_param else None
corpus_why = None  # why the feature msil-corpus is missing
corpus_entries = {}  # name -> (ref, tree_sha256) of the manifest
try:
    with open(corpus_manifest, "rb") as f:
        _manifest_bytes = f.read()
    for _entry in json.loads(_manifest_bytes.decode("utf-8"))["programs"]:
        corpus_entries[_entry["name"]] = (_entry["ref"], _entry["tree_sha256"])
except (OSError, ValueError, KeyError, TypeError) as e:
    _manifest_bytes = None
    corpus_why = "cannot read %s: %s" % (slashes(corpus_manifest), e)
_unknown_skip = [n for n in corpus_skip if corpus_entries and n not in corpus_entries]
if _unknown_skip:
    fatal(
        "--param msil_corpus_skip: %s %s not in %s (entries: %s)"
        % (
            ", ".join(_unknown_skip),
            "is" if len(_unknown_skip) == 1 else "are",
            slashes(corpus_manifest),
            ", ".join(corpus_entries),
        )
    )
corpus_trees = []  # the names whose msil-corpus-<name> holds
fetch_hint = "python msil/utils/corpus/fetch.py --cache %s" % slashes(
    corpus_dir or "<dir>"
)
if corpus_why is None and not corpus_dir:
    corpus_why = "disabled by an empty --param msil_corpus_dir="
elif corpus_why is None and re.search(r"\s", corpus_dir):
    corpus_why = "the cache %s contains a blank, which the tests cannot quote" % (
        slashes(corpus_dir)
    )
elif corpus_why is None:
    stamp_path = os.path.join(corpus_dir, "msil-corpus.stamp")
    try:
        with open(stamp_path, encoding="utf-8") as f:
            stamp = json.load(f)
        stamp_sha = stamp["manifest_sha256"]
        stamp_trees = dict(stamp["trees"])
    except FileNotFoundError:
        corpus_why = "%s has no msil-corpus.stamp" % slashes(corpus_dir)
    except (OSError, ValueError, KeyError, TypeError) as e:
        corpus_why = "cannot read %s: %s" % (slashes(stamp_path), e)
    else:
        if stamp_sha != hashlib.sha256(_manifest_bytes).hexdigest():
            corpus_why = (
                "%s is stale: it was written for another msil/utils/corpus/"
                "manifest.json" % slashes(stamp_path)
            )
        else:
            for _name, (_ref, _tree) in corpus_entries.items():
                if stamp_trees.get(_name) == _tree and os.path.isdir(
                    os.path.join(corpus_dir, "%s-%s" % (_name, _ref))
                ):
                    corpus_trees.append(_name)
if corpus_why is None:
    features.add("msil-corpus")
    for _name in corpus_trees:
        features.add("msil-corpus-" + _name)
    note(
        "exit corpus: %s (%s)"
        % (slashes(corpus_dir), ", ".join(corpus_trees) or "no tree")
    )
    lacking = [n for n in corpus_entries if n not in corpus_trees]
    lacking_unskipped = [n for n in lacking if n not in corpus_skip]
    if lacking_unskipped and require_corpus:
        fatal(
            "msil_require_corpus=1, but the stamp of %s lacks %s; run '%s' "
            "(or name the entries in --param msil_corpus_skip)"
            % (slashes(corpus_dir), ", ".join(lacking_unskipped), fetch_hint)
        )
    for _name in lacking:
        note(
            "exit corpus entry %s %s; its tests are UNSUPPORTED"
            % (
                _name,
                "is left out on purpose (msil_corpus_skip)"
                if _name in corpus_skip
                else "is not in the stamp (run '%s')" % fetch_hint,
            )
        )
elif require_corpus:
    fatal(
        "msil_require_corpus=1, but the exit corpus is not usable: %s; run '%s'"
        % (corpus_why, fetch_hint)
    )
else:
    note(
        "exit corpus not available (msil/test/Programs is UNSUPPORTED): %s%s"
        % (
            corpus_why,
            "; run '%s'" % fetch_hint if corpus_dir and _manifest_bytes else "",
        )
    )

# ---------------------------------------------------------------- reference assemblies
# From M21 msil-ld binds every BCL type and member of its output against
# reference assemblies (--reference=<file|dir>, plan 5.11); without one it
# uses its built-in [mscorlib] table (5.11.7). The reference packs of the
# reference legs (plan 5.11.10, 5.12.3):
#   net48         the .NET Framework 4.8 reference directory: $MSIL_REF_NET48,
#                 else %ProgramFiles(x86)%\Reference Assemblies\Microsoft\
#                 Framework\.NETFramework\v4.8 on Windows, else
#                 build/.NETFramework/v4.8 of the NuGet package
#                 microsoft.netframework.referenceassemblies.net48 at the
#                 version MsilNet48RefAssembliesVersion of
#                 msil/utils/tools/msil-tools.proj in a NuGet global-packages
#                 folder (the Linux CI job restores it and exports
#                 MSIL_REF_NET48);
#   net8, net10   ref/net<N>.0 of the newest Microsoft.NETCore.App.Ref <N>.x
#                 targeting pack of a dotnet root (the host's directory,
#                 $DOTNET_ROOT, the default installation directories);
#                 $MSIL_REF_NET8 and $MSIL_REF_NET10 name another directory.
# A directory that one of those variables names explicitly and that lacks the
# pack's core library (mscorlib.dll, System.Runtime.dll) is a configuration
# error. Each pack found gives the feature msil-refpack=<name> and
# %{msil_ref_dir_<name>}, for tests that need a pack whatever the leg (the
# substitution names a placeholder when the pack is missing, so such a test
# needs 'REQUIRES: msil-refpack=<name>'). Nothing here downloads.
#
# --param msil_references=none|net48|net8|net10|<path> (default none; plan
# 5.12.2; feature msil-ref=<name>, msil-ref=path for a path) selects the
# references of every MSIL link: %{msil_ref_flags} is -Wl,--reference=<dir>
# (empty for none) and is part of %{msil_ldflags}, so it reaches the MSIL
# side of every link (%{msil_prog}, and %{msil_c} and %{msil_ll} under lto and
# obj) and never the native oracle; a test with references of its own
# REDEFINEs %{msil_ref_flags}. A path naming a .NET pack (System.Runtime.dll,
# or a directory holding it, such as
# <dotnet>/packs/Microsoft.NETCore.App.Ref/<v>/ref/net<N>.0) is handled as
# net<N>, N being the major version of that System.Runtime's Assembly row
# (5 or more: from .NET 5 on it is the .NET major; the .NET Framework's
# facade and .NET Core 3's are 4.x, and such a path is any other path); a
# System.Runtime.dll whose version cannot be read is a configuration error.
# A value other than none under msil_pipeline=asm is a configuration error
# (asm has no link, plan 5.12.1); so are a named pack that is not found, a
# path that does not exist, a .NET Framework run (msil_run_netfx=1) of
# programs bound to .NET <N> references (net8, net10 or a .NET pack's path),
# which do not load there, a runtime older than .NET <N>, and msil_equiv=1
# under msil_pipeline=lto with such references: O4 compares the link's
# --lto-emit-asm text, which is never bound (plan 5.11.8), with an image
# whose data-import helper calls NativeLibrary. With references, O3 and O4
# also allow the 'scopes' entry of msil/utils/equiv-allowed.txt (plan
# 5.12.6). With .NET <N> references, %msil_verify runs ILVerify against the
# pack and skips PEVerify ('--skip-peverify "bound to .NET <N> references"'):
# PEVerify cannot load such an image (bcl-binding 3.5).
REF_PACKS = ("net48", "net8", "net10")
REF_CORE = {"net48": "mscorlib.dll", "net8": "System.Runtime.dll"}
REF_CORE["net10"] = REF_CORE["net8"]


def explicit_ref_pack(name):
    """$MSIL_REF_<NAME>, checked; None when it is not set."""
    var = "MSIL_REF_" + name.upper()
    value = os.environ.get(var)
    if not value:
        return None
    path = os.path.abspath(value)
    if not os.path.isfile(os.path.join(path, REF_CORE[name])):
        fatal(
            "$%s=%s has no %s, so it is not the %s reference pack"
            % (var, slashes(value), REF_CORE[name], name)
        )
    return path


def dotnet_roots():
    """The dotnet installation directories that may hold targeting packs."""
    roots = [os.path.dirname(os.path.realpath(dotnet))] if dotnet else []
    roots += [os.environ.get(v) for v in ("DOTNET_ROOT", "DOTNET_ROOT_X64")]
    roots += (
        [r"C:\Program Files\dotnet"]
        if is_windows
        else ["/usr/share/dotnet", "/usr/lib/dotnet", "/usr/local/share/dotnet"]
    )
    roots.append(os.path.join(home, ".dotnet"))
    unique = {}
    for r in filter(None, roots):
        unique.setdefault(os.path.normcase(os.path.normpath(r)), r)
    return [r for r in unique.values() if os.path.isdir(r)]


def find_ref_pack(name):
    """The directory of the reference pack name (REF_PACKS), or None."""
    explicit = explicit_ref_pack(name)
    if explicit:
        return explicit
    core = REF_CORE[name]
    if name == "net48":
        candidates = []
        if is_windows:
            candidates.append(
                os.path.join(
                    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                    "Reference Assemblies",
                    "Microsoft",
                    "Framework",
                    ".NETFramework",
                    "v4.8",
                )
            )
        version = tools_proj_version("MsilNet48RefAssembliesVersion")
        candidates += [
            os.path.join(
                root,
                "microsoft.netframework.referenceassemblies.net48",
                version,
                "build",
                ".NETFramework",
                "v4.8",
            )
            for root in nuget_roots()
        ]
        return next(
            (c for c in candidates if os.path.isfile(os.path.join(c, core))), None
        )
    major = int(name[3:])
    hits = []
    for root in dotnet_roots():
        hits += glob.glob(
            os.path.join(
                root,
                "packs",
                "Microsoft.NETCore.App.Ref",
                "%d.*" % major,
                "ref",
                "net%d.0" % major,
            )
        )
    hits = [h for h in hits if os.path.isfile(os.path.join(h, core))]
    # The newest <major>.x; the version is the directory above ref/.
    return (
        max(
            hits,
            key=lambda h: version_key(
                os.path.basename(os.path.dirname(os.path.dirname(h)))
            ),
        )
        if hits
        else None
    )


ref_packs = {}
for name in REF_PACKS:
    found = find_ref_pack(name)
    if found:
        ref_packs[name] = os.path.normpath(found)
        features.add("msil-refpack=" + name)
note(
    "reference packs: %s"
    % (
        "; ".join(
            "%s %s" % (n, slashes(ref_packs[n])) for n in REF_PACKS if n in ref_packs
        )
        or "none found"
    )
)

references = params.get("msil_references", "none")
ref_dir = None
if references != "none":
    if references in REF_PACKS:
        if references not in ref_packs:
            fatal(
                "--param msil_references=%s, but the %s reference pack is not found "
                "(see 'reference assemblies' in msil/test/lit.cfg.py: %s)"
                % (
                    references,
                    references,
                    "$MSIL_REF_NET48, the .NET Framework 4.8 reference directory or "
                    "the NuGet package microsoft.netframework.referenceassemblies.net48 "
                    "('dotnet restore msil/utils/tools/msil-tools.proj' fetches it)"
                    if references == "net48"
                    else "$MSIL_REF_%s or a Microsoft.NETCore.App.Ref %s.x targeting "
                    "pack, which the .NET %s SDK installs"
                    % (references.upper(), references[3:], references[3:]),
                )
            )
        ref_dir = ref_packs[references]
    elif references and os.path.exists(references):
        ref_dir = os.path.normpath(os.path.abspath(references))
    else:
        fatal(
            "--param msil_references=%s: use none, net48, net8, net10 or the path of "
            "a reference assembly or of a directory of them" % references
        )
    if pipeline == "asm":
        fatal(
            "--param msil_references=%s needs a link, which msil_pipeline=asm does "
            "not have (plan 5.12.2); use msil_pipeline=obj or lto" % references
        )
ref_name = references if references in ("none",) + REF_PACKS else "path"
features.add("msil-ref=" + ref_name)


def system_runtime_major(path):
    """The major version of the Assembly row of path, a System.Runtime.dll
    (msil/utils/msil-verify.py reads it); a configuration error if it cannot
    be read."""
    saved = sys.dont_write_bytecode  # no __pycache__ in msil/utils
    sys.dont_write_bytecode = True
    try:
        spec = importlib.util.spec_from_file_location(
            "msil_verify", os.path.join(utils_dir, "msil-verify.py")
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        name, version = module.assembly_row(path)
        if name.lower() != "system.runtime":
            raise ValueError("its Assembly row names %s, not System.Runtime" % name)
        return version[0]
    except (OSError, ValueError) as e:
        fatal(
            "--param msil_references=%s: cannot read the version of %s, so the "
            ".NET pack's major is unknown: %s"
            % (references, slashes(path), slashes(str(e)))
        )
    finally:
        sys.dont_write_bytecode = saved


# The .NET major of the references whose images only .NET <major> or newer
# loads: net8, net10, and a path naming a .NET pack (see above); ILVerify
# takes such images' references from ref_runtime_dir, the pack's directory.
ref_net_major = int(references[3:]) if references in ("net8", "net10") else None
ref_runtime_dir = ref_dir
if ref_name == "path":
    if os.path.isdir(ref_dir):
        runtime_dll = os.path.join(ref_dir, "System.Runtime.dll")
    else:
        is_runtime = os.path.basename(ref_dir).lower() == "system.runtime.dll"
        runtime_dll = ref_dir if is_runtime else None
        ref_runtime_dir = os.path.dirname(ref_dir)
    if runtime_dll and os.path.isfile(runtime_dll):
        major = system_runtime_major(runtime_dll)
        if major >= 5:
            ref_net_major = major
if ref_net_major is not None:
    if run_netfx:
        fatal(
            "--param msil_run_netfx=1 with msil_references=%s: programs bound to "
            ".NET %d reference assemblies do not load on the .NET Framework (plan "
            "5.11.10)" % (references, ref_net_major)
        )
    if exec_major is not None and exec_major < ref_net_major:
        fatal(
            "--param msil_references=%s binds the programs to .NET %d reference "
            "assemblies, which Microsoft.NETCore.App %d.x does not load; pass "
            "--param msil_dotnet_runtime=%d"
            % (references, ref_net_major, exec_major, ref_net_major)
        )
    if pipeline == "lto" and equiv:
        fatal(
            "--param msil_equiv=1 with msil_pipeline=lto and msil_references=%s: "
            "O4 compares the link's --lto-emit-asm text, which is never bound to "
            "the references (plan 5.11.8), with an image bound to .NET %d "
            "reference assemblies, whose data-import helper calls NativeLibrary; "
            "use msil_pipeline=obj, or msil_references=none or net48"
            % (references, ref_net_major)
        )
if ref_dir:
    note(
        "references: %s (%s)%s"
        % (
            references if ref_name != "path" else "path",
            slashes(ref_dir),
            ", a .NET %d pack" % ref_net_major
            if ref_name == "path" and ref_net_major is not None
            else "",
        )
    )

# ---------------------------------------------------------------- C# compiler
# %msil_csc (plan 5.12.3, M22) is msil/utils/jitall/build-jitall.py
# --csharp: it compiles one C# file offline with the Roslyn csc of the newest
# installed .NET SDK, for net8 (against the newest Microsoft.NETCore.App.Ref
# 8.x pack) or net48 (against the .NET Framework 4.8 reference assemblies
# and their Facades); the C# consumer of msil/test/M2/04-shared and
# harness/csc.test use it. Feature msil-csc when that csc is found ('dotnet
# --list-sdks', as build-jitall.py looks for it); --param msil_require_csc=1
# makes a missing one a configuration error. The script finds the packs as
# this file does (see "reference assemblies"): $MSIL_REF_NET8 and
# $MSIL_REF_NET48 when they are set (lit passes them on), else the newest
# 8.x pack of the SDK's dotnet root and the .NET Framework 4.8 reference
# directory (on Linux the NuGet package that msil-tools.proj pins).
require_csc = bool_param("msil_require_csc")
build_jitall_py = os.path.join(utils_dir, "jitall", "build-jitall.py")
csc_why = None
if not dotnet:
    csc_why = "no dotnet host found"
else:
    saved = sys.dont_write_bytecode  # no __pycache__ in msil/utils
    sys.dont_write_bytecode = True
    try:
        spec = importlib.util.spec_from_file_location("build_jitall", build_jitall_py)
        build_jitall_mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build_jitall_mod)
        csc_sdk, _, csc_dll = build_jitall_mod.find_csc(dotnet)
    except (OSError, ImportError) as e:
        csc_why = "cannot load %s: %s" % (slashes(build_jitall_py), e)
    except Exception as e:  # build-jitall.py's BuildError
        csc_why = str(e)
    finally:
        sys.dont_write_bytecode = saved
if csc_why is None:
    features.add("msil-csc")
    note("csc: %s (SDK %s)" % (slashes(csc_dll), csc_sdk))
elif require_csc:
    fatal(
        "msil_require_csc=1, but the .NET SDK's csc is missing: %s; install a .NET "
        "SDK (it ships Roslyn/bincore/csc.dll) or pass --param msil_dotnet=PATH"
        % csc_why
    )
else:
    note("csc is not available (%%msil_csc): %s" % csc_why)

# Everything needed to compile and run C programs: under obj and lto also
# msil-ld and llvm-ar, through which %{msil_c} links.
if {"clang", "msil-registered-target", "msil-ilasm", "msil-dotnet"} <= features and (
    pipeline == "asm" or not link_missing
):
    features.add("msil-exec")

# ---------------------------------------------------------------- environment
# lit passes only a few variables through; dotnet, the verifiers and WSL need
# these.
for var in (
    "DOTNET_ROOT",
    "DOTNET_ROOT_X64",
    "DOTNET_ROOT_ARM64",
    "DOTNET_ROOT(x86)",
    "NUGET_PACKAGES",
    "ProgramFiles",
    "ProgramFiles(x86)",
    "ProgramW6432",
    "ProgramData",
    "LOCALAPPDATA",
    "APPDATA",
    "WINDIR",
    "LIBPATH",
    "MSIL_BUILD_DIR",
    "MSIL_LINUX_SYSROOT",
    "MSIL_GLIBC_SYSROOT",
    # The reference packs of %msil_csc (build-jitall.py --csharp, M22).
    "MSIL_REF_NET8",
    "MSIL_REF_NET48",
):
    if var in os.environ:
        config.environment[var] = os.environ[var]
config.environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
config.environment["DOTNET_NOLOGO"] = "1"
config.environment["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1"

# ---------------------------------------------------------------- substitutions
python = quote(config.python_executable)
limits = " --timeout %g --mem-limit-mb %d" % (timeout, mem_limit_mb)
env_args = "".join(' --env "%s"' % kv for kv in dotnet_env)


def script(name):
    return "%s %s" % (python, quote(os.path.join(utils_dir, name)))


# Every child process runs under the timeout and memory cap of msil-run.py;
# ilasm through its --tool mode. Callers add -dll or -exe and -output=.
ilasm_cmd = "%s --tool%s -- %s -nologo -quiet%s" % (
    script("msil-run.py"),
    limits,
    quote(ilasm or "ilasm-not-found"),
    ilasm_flags,
)
if wsl_ilasm:
    # msil_wsl_ilasm: the Linux ilasm, inside WSL, under the same limits.
    ilasm_cmd = "%s --tool --runner wsl --wsl-distro %s%s -- %s -nologo -quiet%s" % (
        script("msil-run.py"),
        wsl_distro,
        limits,
        quote(wsl_ilasm),
        ilasm_flags,
    )

def make_exec_cmd(runtimeconfig, env):
    """%msil_exec with the runtimeconfig.json <runtimeconfig> and the
    --env arguments <env> (text)."""
    cmd = "%s --expect-os %s%s" % (script("msil-run.py"), triple_os, limits)
    if runner == "wsl":
        cmd += " --runner wsl --wsl-distro %s --dotnet %s" % (
            wsl_distro,
            quote(wsl_dotnet),
        )
    elif dotnet:
        cmd += " --dotnet " + quote(dotnet)
    if runtimeconfig:
        cmd += " --runtimeconfig " + quote(runtimeconfig)
    return cmd + env


exec_cmd = make_exec_cmd(exec_runtimeconfig, env_args)

if "msil-ilverify" in features:
    # An image bound to .NET <N> references (msil_references=net8, net10 or
    # the path of a .NET pack) is verified against the pack it was bound to,
    # without PEVerify, which cannot load it (plan 5.11.10, bcl-binding 3.5).
    verify_cmd = "%s --ilverify %s --runtime-dir %s --allow %s%s" % (
        script("msil-verify.py"),
        quote(ilverify),
        quote(ref_runtime_dir if ref_net_major is not None else local_runtime_dir),
        quote(os.path.join(utils_dir, "ilverify-allowed.txt")),
        limits,
    )
    if ref_net_major is not None:
        verify_cmd += ' --skip-peverify "bound to .NET %d references"' % ref_net_major
    elif peverify:
        verify_cmd += " --peverify " + quote(peverify)
    # The neutralized pass (msil-verify.py pass 2) reassembles with %ilasm's
    # assembler; msil-verify.py applies the limits itself.
    if ildasm and ilasm:
        verify_cmd += " --ildasm %s --ilasm %s" % (quote(ildasm), quote(ilasm))
else:
    verify_cmd = script("msil-verify.py") + " --skip"

def make_jitall_cmd(runtimeconfig, env):
    """%msil_jitall with the runtimeconfig.json <runtimeconfig> and the --env
    arguments <env> (text)."""
    cmd = "%s --jitall %s%s" % (
        script("msil-jitall.py"),
        quote(jitall_dir or "jitall-disabled"),
        limits,
    )
    if runner == "wsl":
        # In WSL on the runtime of the programs: the module initializer runs.
        cmd += " --runner wsl --wsl-distro %s --dotnet %s" % (
            wsl_distro,
            quote(wsl_dotnet),
        )
    elif dotnet:
        cmd += " --dotnet " + quote(dotnet)
    if runtimeconfig:
        cmd += " --runtimeconfig " + quote(runtimeconfig)
    return cmd + env


jitall_cmd = make_jitall_cmd(
    exec_runtimeconfig if runner == "wsl" else local_runtimeconfig, env_args
)

# msil_also_run (see "more runtimes"): per entry, the tag of its transcript
# and the jitall and run commands with that runtime's runtimeconfig.json
# (jitall runs where the programs run, so both take the runner's runtime)
# and, for +tc0, DOTNET_TieredCompilation=0 after the leg's msil_dotnet_env.
also_run_cmds = []
for _major, _tc0 in also_run:
    _rc = write_runtimeconfig(_major)
    _env = env_args + (' --env "DOTNET_TieredCompilation=0"' if _tc0 else "")
    also_run_cmds.append(
        (
            "also-net%d%s" % (_major, "-tc0" if _tc0 else ""),
            make_jitall_cmd(_rc, _env),
            make_exec_cmd(_rc, _env),
        )
    )

# %{msil_also_run_check} (see "more runtimes"; plan 5.12.3): the runs of
# msil_also_run for a test with explicit RUN lines.
also_run_check = "".join(
    f" %if msil-jitall %{{ && {jit} %{{msil_also_run_image}} %}}"
    f" && {run} --out %{{msil_also_run_image}}.{tag}.out %{{msil_also_run_image}}"
    f" && FileCheck --input-file=%{{msil_also_run_image}}.{tag}.out"
    " --match-full-lines %s"
    for tag, jit, run in also_run_cmds
)

# %msil_mdcanon (plan 5.12.3): mdcanon on the host's dotnet, under the limits
# of every other tool; callers add the options and files. %msil_cil_dump
# (M17): the tree's llvm-cil-dump under the same limits. %msil_canon is the
# canonical listing a test compares: '%msil_cil_dump --canonical' when the
# feature msil-cil-dump is present (M17 on), else the %msil_mdcanon command.
# Without the tool's feature a command names a placeholder, so a test that
# runs it without 'REQUIRES: msil-mdcanon' (or msil-cil-dump) fails instead
# of passing.
have_mdcanon = "msil-mdcanon" in features
mdcanon_cmd = "%s --tool%s -- %s exec --runtimeconfig %s %s" % (
    script("msil-run.py"),
    limits,
    quote(dotnet if have_mdcanon else "dotnet-not-found"),
    quote(mdcanon_config if have_mdcanon else "mdcanon-not-found.runtimeconfig.json"),
    quote(mdcanon_dll if have_mdcanon else "mdcanon-not-found.dll"),
)
cil_dump_cmd = "%s --tool%s -- %s" % (
    script("msil-run.py"),
    limits,
    quote(msil_cil_dump or "llvm-cil-dump-not-found"),
)
canon_cmd = (
    cil_dump_cmd + " --canonical" if "msil-cil-dump" in features else mdcanon_cmd
)

# Without msil_roundtrip=1 nothing runs it (the composites test the feature),
# and msil-verify.py rejects --roundtrip without the tools.
roundtrip_cmd = script("msil-verify.py") + " --roundtrip"
if rt_ildasm:
    roundtrip_cmd += " --ildasm %s --ilasm %s%s" % (
        quote(rt_ildasm),
        quote(rt_ilasm),
        limits,
    )

# %msil_check_mscorlib_safe (plan 5.11.7, 5.12.3; M21): the runtimes of this
# run that the probe of msil/utils/check-mscorlib-safe.py runs on (its
# --runtimes). With the local runner: the .NET Framework where it is
# installed (feature msil-netfx), the host's .NET 8 and .NET 10, and the
# programs' own runtime net<major> (msil_dotnet_runtime or the default one),
# so the list is never empty where msil-exec holds; with the WSL runner: the
# programs' runtime inside WSL (wsl-net<major>, on msil_wsl_dotnet's host).
# The list is empty only without a .NET runtime for the programs, where
# msil-exec does not hold and msil/test/M2's gate makes the tests that run
# the script UNSUPPORTED; the command then has no --runtimes (never 'none',
# which the script rejects) and names the script's default. The gate runs
# the script with that default (Framework 4.8, .NET 8, .NET 10 and WSL
# .NET 10) besides.
if runner == "wsl":
    safe_runtimes = ["wsl-net%d" % exec_major] if exec_major else []
else:
    safe_runtimes = ["netfx"] if "msil-netfx" in features else []
    safe_majors = {m for m in (8, 10) if m in local_runtimes}
    if exec_major is not None:
        safe_majors.add(exec_major)
    safe_runtimes += ["net%d" % m for m in sorted(safe_majors)]
mscorlib_safe_cmd = "%s --msil-ld %s --ilasm %s%s%s" % (
    script("check-mscorlib-safe.py"),
    quote(msil_ld or "msil-ld-not-found"),
    quote(ilasm or "ilasm-not-found"),
    " --runtimes " + ",".join(safe_runtimes) if safe_runtimes else "",
    limits,
)
if dotnet:
    mscorlib_safe_cmd += " --dotnet " + quote(dotnet)
if runner == "wsl":
    mscorlib_safe_cmd += " --wsl-distro %s --wsl-dotnet %s" % (
        wsl_distro,
        quote(wsl_dotnet),
    )

clang = clang_path or "clang-not-found"
llc = lit.util.which("llc", config.llvm_tools_dir) or "llc-not-found"

# %{msil_native_ldflags}: the libraries of the native link (the native-oracle
# legs), placed after the source because GNU ld resolves a library only for
# the inputs before it. It is -lm when the native triple is not Windows: the C
# math functions are Phase-1 imports (D14), glibc keeps them in libm (not
# libc), and the clang driver does not add -lm for C. For MSVC triples it is
# the compiler-rt builtins (msil_native_rtlib), written as
# '-l<path>.lib', which the MSVC driver passes to the linker unchanged and
# which, being an option, leaves a single source argument (the stand-in
# compilers of harness/pipeline and update-expected.py's self-tests count
# them). It is empty for MinGW (msvcrt holds the math functions, libgcc the
# helpers) and when the builtins are disabled; the composites then leave it
# out. update-expected.py builds its native command line by the same rule.
if triple_os != "windows":
    native_ldflags = "-lm"
elif msvc_native and native_rtlib:
    native_ldflags = quote("-l" + native_rtlib)
else:
    native_ldflags = ""
native_link = " %{msil_native_ldflags}" if native_ldflags else ""

# The E7 Linux oracle (msil_runner=wsl, plan 5.12.8): compile for
# x86_64-unknown-linux-gnu without PIC, link start.o, the test and the
# sysroot's libm, libc and libgcc_s (--as-needed keeps libgcc_s.so.1 out of
# programs that do not call it) with ld.lld, and run the program under WSL
# through msil-run.py --native --runner wsl. linux-oracle.test uses the same
# substitutions with the local runner.
linux_cc = "%s --target=x86_64-unknown-linux-gnu -fno-pic" % quote(clang)
linux_ld = "%s -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed %s" % (
    quote(ld_lld or "ld.lld-not-found"),
    quote(e7_start or "linux-oracle-start.o-not-built"),
)
linux_libs = " ".join(
    quote(os.path.join(linux_libdir or "linux-sysroot-not-found", n))
    for n in ("libm.so.6", "libc.so.6", "libgcc_s.so.1")
)
# %{msil_e7_args}: the E7 configuration of this lit run as update-expected.py
# options, so a test that runs update-expected.py --os linux uses the clang,
# the sysroot (after msil_linux_sysroot > $MSIL_LINUX_SYSROOT > the default;
# a sysroot lit ignored counts as absent), the WSL distribution and the
# ld.lld that decided feature msil-linux-oracle, not the script's own
# defaults ($MSIL_BUILD_DIR, $MSIL_LINUX_SYSROOT, PATH).
e7_args = "--clang %s --linux-sysroot %s --wsl-distro %s --ld-lld %s" % (
    quote(clang),
    quote(linux_sysroot if linux_libdir else "linux-sysroot-not-found"),
    wsl_distro,
    quote(ld_lld or "ld.lld-not-found"),
)


def native_leg(t, level, compile_flags):
    """The native-oracle leg of a composite: <compile_flags> are the C or IR
    flags before -<level>."""
    if e7_oracle:
        build = (
            f"%{{msil_linux_cc}} {compile_flags} -{level} -c %s -o {t}.native.o"
            f" && %{{msil_linux_ld}} {t}.native.o %{{msil_linux_libs}}"
            f" -o {t}.native.elf"
        )
        exe = f"{t}.native.elf"
    else:
        build = (
            f"%clang_native {compile_flags} -{level} %s{native_link} -o {t}.native.exe"
        )
        exe = f"{t}.native.exe"
    return (
        f" %if msil-native-oracle %{{ && {build}"
        f" && %msil_exec --native --expect %{{msil_expect}}"
        f" --out {t}.native.out {exe} %}}"
    )


def roundtrip_leg(t, image=None, dep=""):
    """msil_roundtrip: the ildasm -> ilasm round trip of <t>.dll (or of the
    program image of %{msil_prog}, written back as <t>.rt.exe), then a run of
    the reassembled image against the same .expected file. dep: the
    dependency options of the run (dep_args)."""
    rt = f"{t}.rt.exe" if image else f"{t}.rt.dll"
    image = image or f"{t}.dll"
    return (
        f" %if msil-roundtrip %{{ && %msil_roundtrip --output={rt} {image}"
        f" && %msil_exec --expect %{{msil_expect}} --out {t}.rt.out{dep} {rt} %}}"
    )


def also_run_legs(t, image, dep=""):
    """msil_also_run: after the main run, jitall and the run of <image> once
    per entry (see "more runtimes"), against the same expected file, into
    <t>.also-net<major>[-tc0].out. dep: the dependency options of the run
    (dep_args)."""
    text = ""
    for tag, jit, run in also_run_cmds:
        text += (
            f" %if msil-jitall %{{ && {jit}{dep} {image} %}}"
            f" && {run} --expect %{{msil_expect}} --out {t}.{tag}.out{dep} {image}"
        )
    return text


def c_pipeline(level):
    """compile -> assemble -> verify -> force-JIT -> run and compare, at -<level>."""
    t = "%t." + level
    cmd = (
        f"%clang_msil %{{msil_cflags}} %{{msil_copts}} -{level} -S %s -o {t}.il"
        f" && %ilasm -dll -output={t}.dll {t}.il"
        f" && %msil_verify {t}.dll"
        f" %if msil-jitall %{{ && %msil_jitall {t}.dll %}}"
        f" && %msil_exec --expect %{{msil_expect}} --out {t}.out {t}.dll"
    )
    cmd += also_run_legs(t, f"{t}.dll")
    cmd += roundtrip_leg(t)
    cmd += native_leg(t, level, "%{msil_cflags} %{msil_copts}")
    cmd += (
        f" %if msil-run-netfx %{{ && %ilasm -exe -output={t}.exe {t}.il"
        f" && %msil_exec --netfx --expect %{{msil_expect}} --out {t}.netfx.out {t}.exe %}}"
    )
    return cmd


def ll_pipeline(level):
    """The same for hand-written IR run through llc (no target triple or
    datalayout in the file). llc does no IPO, so values passed to noinline
    functions stay opaque even at -O2."""
    t = "%t." + level
    cmd = (
        f"%llc_msil -{level} %s -o {t}.il"
        f" && %ilasm -dll -output={t}.dll {t}.il"
        f" && %msil_verify {t}.dll"
        f" %if msil-jitall %{{ && %msil_jitall {t}.dll %}}"
        f" && %msil_exec --expect %{{msil_expect}} --out {t}.out {t}.dll"
    )
    cmd += also_run_legs(t, f"{t}.dll")
    cmd += roundtrip_leg(t)
    cmd += native_leg(t, level, "-Wno-override-module")
    cmd += (
        f" %if msil-run-netfx %{{ && %ilasm -exe -output={t}.exe {t}.il"
        f" && %msil_exec --netfx --expect %{{msil_expect}} --out {t}.netfx.out {t}.exe %}}"
    )
    return cmd


# llc takes -O0..-O3 only; -Os/-Oz levels are skipped for IR tests.
ll_levels = [o for o in opt_levels if o in ("O0", "O1", "O2", "O3")] or ["O2"]

# ---------------------------------------------------------------- %{msil_prog}
# Multi-TU programs (plan 5.12.3-5.12.5): msil/utils/msil-prog.py builds the
# program (%msil_prog_build), and the composite verifies, force-JITs and runs
# it as %{msil_c} does. msil-prog.py compiles CIL objects with clang -c under
# msil-prog-pipeline=obj (msil_pipeline=asm and obj) and bitcode with
# clang -flto -c under lto, and msil-ld writes the image (msil_link_output=pe;
# with il, an LTO link writes --lto-emit-asm text that ilasm -exe
# assembles). A test lists its sources and libraries in placeholders
# that its REDEFINE: lines set:
#   %{msil_srcs}        the C sources, compiled with -c (default %s)
#   %{msil_stock_srcs}  sources that the tree's clang compiles for the native
#                       triple with -flto -c (stock-clang bitcode, plan Q12)
#   %{msil_archives}    NAME=SRC[,SRC...][;NAME=...]: one archive libNAME.a
#                       per NAME, linked as -L<dir> -lNAME (a SRC written
#                       stock:SRC is compiled as a stock source)
#   %{msil_libs}        further arguments of the MSIL link (not the oracle's)
#   %{msil_native_libs} link arguments of both builds, the MSIL link and the
#                       native oracle's (msil-prog.py --native-lib; M15): the
#                       native libraries of the program, for example
#                       -L%t -lnative for a test-built native.dll (with its
#                       import library native.lib) or libnative.so
#   %{msil_deps}        files that every run needs beside the program (a
#                       test-built native library, a managed assembly):
#                       msil-verify.py, msil-jitall.py and every msil-run.py
#                       run (the MSIL program, the native oracle, the .NET
#                       Framework run and the round trip) get them as
#                       '--dep %{msil_deps} --' before the program (M15);
#                       the value may use lit's %if (native.dll on Windows
#                       triples, libnative.so on Linux ones) and wildcards,
#                       which the internal shell expands
#   %{msil_shlibs}      [?]NAME=SRC[,SRC...][;...]: shared libraries that
#                       msil-prog.py --shlibs builds before the program, each
#                       linked against those before it, for the MSIL build
#                       (clang -shared into <workdir>/shlib/NAME.dll; ?NAME
#                       an interface, -Wl,--interface-only) and the native
#                       oracle (DLLs, or the E7 recipe's ELF shared objects);
#                       a cycle is ?b=b.c;a=a.c;b=b.c (M22). It is part of
#                       %msil_prog_build, so every build of the test gets it
#   %{msil_copts}       extra compile flags of both builds, after
#                       %{msil_cflags} (M23; part of %msil_prog_build too)
#   %{msil_expect}      the --expect stem of every run (M23; default
#                       %S/%{s:stem})
# and a directory may redefine %{msil_cflags} and %{msil_ldflags}, the MSIL
# link's flags (from M15 empty on Windows triples and
# --sysroot=<msil_linux_sysroot> on Linux triples, so the link reads the
# driver's default libraries of plan 5.10.4; M14 had -nodefaultlibs). Each
# level builds in a fresh %t.<L>.d into %t.<L>.d/prog.exe (an EXE,
# so the same image runs under 'dotnet exec' and on the .NET Framework, plan
# 5.12.1), then %msil_verify, %msil_jitall and %msil_exec --expect
# %{msil_expect} (default %S/%{s:stem}, M23), under msil_also_run jitall and
# the run once more per entry (M23), and the %if legs: the round trip of the
# image, the native
# oracle (msil-prog.py --mode native, the same sources and archives built
# for the native triple; E7 with msil_runner=wsl) and the .NET Framework run.
# Every one of them gets the build's shlib/ directory, which msil-prog.py
# always makes (empty without %{msil_shlibs}), as '--dep-dir
# %t.<L>.d/shlib' (the native runs '%t.<L>.native.d/shlib'), so each run
# finds the libraries beside the program; %msil_verify also gets
# --also-verify-deps, which verifies each library too (M22).
# %{msil_prog_fail_<L>} is 'not' plus the build at one level, for named
# diagnostics: pipe it with 2>&1 into FileCheck, with
# --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit", under
# %if msil-prog-pipeline=<p>. %{msil_det} builds twice at -O2 and compares
# (msil-prog.py --det: the objects, archives and msil-ld's image, or under
# il the --lto-emit-asm text). The placeholders sit inside single quotes on the
# command line (the archive list has ';'), so their values may not contain a
# single quote, and one value may not contain ',' or ';' in a path;
# %{msil_srcs}, %{msil_stock_srcs} and %{msil_deps} are lists of words on
# the command line (quote a path with a blank).
# %msil_prog_build is everything but the level, the directories and the
# inputs: the template with @TOOLS@ replaced by the tool arguments, sorted by
# name. harness/prog/lit.local.cfg rebuilds it from config.msil_prog_template
# and config.msil_prog_tool_args with stand-ins (data, not a function: lit
# pickles the configuration for its worker processes).
prog_template = (
    "%s --triple %s --pipeline %s --link-output %s @TOOLS@"
    " --cflags='%%{msil_cflags} %%{msil_copts}' --ldflags='%%{msil_ldflags}'"
    " --native-ldflags='%%{msil_native_ldflags}' --shlibs='%%{msil_shlibs}'"
    " --timeout %g --mem-limit-mb %d"
    % (
        script("msil-prog.py"),
        msil_triple,
        prog_pipeline_name,
        link_output,
        timeout * 10,
        mem_limit_mb,
    )
)


def make_prog_build(tool_args):
    return prog_template.replace(
        "@TOOLS@", " ".join(tool_args[k] for k in sorted(tool_args))
    )


prog_tool_args = {
    "ar": "--ar=" + quote(msil_ar or "llvm-ar-not-found"),
    "clang": "--clang=" + quote(clang),
    "ilasm": "--ilasm='%s'" % ilasm_cmd,
    "ld": "--ld=" + quote(msil_ld or "msil-ld-not-found"),
}
if e7_oracle:
    # msil-prog.py's native mode builds the E7 Linux oracle (plan 5.12.8).
    prog_tool_args["ld-lld"] = "--ld-lld=" + quote(ld_lld)
    prog_tool_args["linux-sysroot"] = "--linux-sysroot=" + quote(linux_sysroot)
    prog_tool_args["start"] = "--start=" + quote(e7_start)
config.msil_prog_tool_args = prog_tool_args
config.msil_prog_template = prog_template


def prog_inputs(srcs, stock, archives, libs, native_libs, native=False):
    """The input options of msil-prog.py (a None part is left out)."""
    text = f" --src {srcs}"
    if stock is not None:
        text += f" --stock-src {stock}"
    if archives is not None:
        text += f" --archives='{archives}'"
    if libs is not None and not native:
        text += f" --lib='{libs}'"
    if native_libs is not None:
        text += f" --native-lib='{native_libs}'"
    return text


PROG_PLACEHOLDERS = (
    "%{msil_srcs}",
    "%{msil_stock_srcs}",
    "%{msil_archives}",
    "%{msil_libs}",
    "%{msil_native_libs}",
)


def dep_args(deps, workdir=None, verify=False):
    """The dependency options of a run, placed right before the program:
    '--dep-dir <workdir>/shlib' (the libraries of %{msil_shlibs}; with
    verify also --also-verify-deps, which verifies them), then every file of
    deps after one --dep (msil-run.py, msil-verify.py and msil-jitall.py take
    one or more files after it), then '--', which ends the list (an empty
    list is allowed). Empty when deps is None."""
    if deps is None:
        return ""
    text = ""
    if workdir:
        text = f" --dep-dir {workdir}/shlib" + (" --also-verify-deps" if verify else "")
    return text + f" --dep {deps} --"


# The equivalence oracles (plan 5.12.6; msil_equiv=1): the entries of
# msil/utils/equiv-allowed.txt that O3 and O4 allow ('scopes' joins both in
# M21's reference legs, msil_references other than none: -S and
# --lto-emit-asm text print [mscorlib], the link binds to the references;
# O4 never meets .NET <N> references, with which msil_equiv=1 under lto is a
# configuration error, see "reference assemblies").
equiv_allowed = os.path.join(utils_dir, "equiv-allowed.txt")
REF_ALLOWED = ("scopes",) if ref_dir else ()
O3_ALLOWED = (
    "assembly-hash",
    "ilasm-params",
    "ilasm-initlocals",
    "snan-quiet",
    "pinvoke-module",
    "pinvoke-access",
) + REF_ALLOWED
O4_ALLOWED = (
    "assembly-hash",
    "ilasm-params",
    "ilasm-initlocals",
    "snan-quiet",
) + REF_ALLOWED


def allow_args(names):
    return "".join(" --allow " + n for n in names)


def prog_build(level, inputs, fail=False, single=None):
    """The MSIL build of %{msil_prog} at -<level>: (command, program).
    single: 'c' or 'll' for the one-source program of %{msil_c} or
    %{msil_ll}, None for %{msil_prog}."""
    t = "%t." + level
    exe = f"{t}.d/prog.exe"
    cmd = f"%msil_prog_build --level {level} --workdir {t}.d --out {exe}"
    if single == "ll":
        cmd += " --llc=%{msil_llc}"
    if equiv and not fail:
        # The equivalence outputs (msil-prog.py step 5): under obj the
        # --no-gc-sections image of a one-source program (O3), under lto
        # (with pe) that image and the link's --lto-emit-asm text (O4).
        eq = f" --equiv-out {t}.d/equiv/prog.exe --equiv-skip {quote(equiv_allowed)} --test %s"
        if prog_pipeline_name == "lto":
            cmd += f" --also-il {t}.d/equiv/prog.il" + eq
        elif single:
            cmd += eq
    cmd += prog_inputs(*inputs)
    return ("not " + cmd if fail else cmd), exe


def asm_reference(level, single):
    """O3's and O6's reference (msil_equiv=1 under obj): the one source
    compiled the Phase 1 way, -S (or llc text) with the assembly name of
    msil-ld's image, and ilasm's EXE image <t>.d/asm/prog.exe, whose module
    name prog.exe is the msil-ld image's too."""
    t = "%t." + level
    ref = f"{t}.d/asm"
    if single == "c":
        text = (
            f"%clang_msil %{{msil_cflags}} %{{msil_copts}} -{level} -S %s"
            f" -mllvm -msil-assembly-name=prog -o {ref}/prog.il"
        )
    else:
        text = f"%llc_msil -{level} %s -msil-assembly-name=prog -o {ref}/prog.il"
    return (
        f" && mkdir -p {ref} && {text}"
        f" && %ilasm -exe -output={ref}/prog.exe {ref}/prog.il"
    )


def prog_pipeline(level, inputs=PROG_PLACEHOLDERS, deps="%{msil_deps}", single=None):
    """%{msil_prog} at -<level>: build, verify, force-JIT, run and compare,
    then the legs. deps: the files every run gets (dep_args), or None.
    single: 'c' or 'll' for %{msil_c} or %{msil_ll} as one-source programs
    (see prog_build)."""
    t = "%t." + level
    build, exe = prog_build(level, inputs, single=single)
    # Every run of the program gets the build's shlib/ (M22).
    dep = dep_args(deps, f"{t}.d")
    verify_dep = dep_args(deps, f"{t}.d", verify=True)
    native_dep = dep_args(deps, f"{t}.native.d")
    o3 = equiv and prog_pipeline_name == "obj" and single
    o4 = equiv and prog_pipeline_name == "lto"
    # The test's triple and level select the qualified skip lines of
    # equiv-allowed.txt, as msil-prog.py's --triple and --level do for
    # --equiv-skip (plan section 12, 'Plan 5.12.6 amended (skip lines)').
    skip_key = f"--test %s --triple {msil_triple} --level {level}"
    md = ""
    if o3:
        build += asm_reference(level, single)
        # O6, unless a 'skip O6' line of equiv-allowed.txt names the test
        # (plan section 12, 'M21.c extended').
        md = (
            f" --equiv-skip {quote(equiv_allowed)} {skip_key}"
            f" --md-baseline {t}.d/asm/prog.exe"
        )
    cmd = (
        f"{build} && %msil_verify{md}{verify_dep} {exe}"
        f" %if msil-jitall %{{ && %msil_jitall{dep} {exe} %}}"
        f" && %msil_exec --expect %{{msil_expect}} --out {t}.out{dep} {exe}"
    )
    cmd += also_run_legs(t, exe, dep)
    cmd += roundtrip_leg(t, exe, dep)
    if o3:
        cmd += (
            f" && %msil_equiv --oracle O3 {skip_key}{allow_args(O3_ALLOWED)}"
            f" {t}.d/equiv/prog.exe {t}.d/asm/prog.exe"
        )
    if o4:
        cmd += (
            f" && %msil_equiv --oracle O4 {skip_key}{allow_args(O4_ALLOWED)}"
            f" --ilasm '%ilasm' {t}.d/equiv/prog.exe {t}.d/equiv/prog.il"
        )
    if single == "ll":
        # The native oracle of an IR test is Phase 1's (no msil-prog.py).
        cmd += native_leg(t, level, "-Wno-override-module")
    else:
        native = f"{t}.native.d/prog.{'elf' if e7_oracle else 'exe'}"
        cmd += (
            f" %if msil-native-oracle %{{ && %msil_prog_build --mode native"
            f" --level {level} --workdir {t}.native.d --out {native}"
            + prog_inputs(*inputs, native=True)
            + f" && %msil_exec --native --expect %{{msil_expect}}"
            f" --out {t}.native.out{native_dep} {native} %}}"
        )
    cmd += (
        f" %if msil-run-netfx %{{ && %msil_exec --netfx --expect %{{msil_expect}}"
        f" --out {t}.netfx.out{dep} {exe} %}}"
    )
    return cmd


ONE_SOURCE = ("%s", None, None, None, None)


def c_composite(level):
    """%{msil_c_<level>}: Phase 1's pipeline under msil_pipeline=asm, the
    one-source %{msil_prog} under lto and obj (without the placeholders of
    %{msil_prog}: no archives, libraries or dependencies)."""
    if pipeline in ("lto", "obj"):
        return prog_pipeline(level, ONE_SOURCE, deps=None, single="c")
    return c_pipeline(level)


def ll_composite(level):
    """%{msil_ll_<level>}: llc text and ilasm -dll under asm and lto (plan
    5.12.1), 'llc -filetype=obj' and the link under obj."""
    if pipeline == "obj":
        return prog_pipeline(level, ONE_SOURCE, deps=None, single="ll")
    return ll_pipeline(level)


ALL_LEVELS = ("O0", "O1", "O2", "O3", "Os", "Oz")

substitutions = [
    # Composites first; they expand into the atoms below.
    ("%{msil_c}", " && ".join(c_composite(o) for o in opt_levels)),
    ("%{msil_ll}", " && ".join(ll_composite(o) for o in ll_levels)),
    *[("%%{msil_c_%s}" % o, c_composite(o)) for o in ALL_LEVELS],
    *[("%%{msil_ll_%s}" % o, ll_composite(o)) for o in ("O0", "O1", "O2", "O3")],
    ("%{msil_prog}", " && ".join(prog_pipeline(o) for o in opt_levels)),
    *[("%%{msil_prog_%s}" % o, prog_pipeline(o)) for o in ALL_LEVELS],
    *[
        ("%%{msil_prog_fail_%s}" % o, prog_build(o, PROG_PLACEHOLDERS, fail=True)[0])
        for o in ALL_LEVELS
    ],
    (
        "%{msil_det}",
        "%msil_prog_build --det --level O2 --workdir %t.det.d"
        + prog_inputs(*PROG_PLACEHOLDERS),
    ),
    ("%msil_prog_build", make_prog_build(prog_tool_args)),
    # llc for %{msil_ll} under obj (msil-prog.py --llc), which adds -mtriple.
    ("%{msil_llc}", quote(llc)),
    # The equivalence oracles O3 and O4 (plan 5.12.3, 5.12.6): msil-equiv.py
    # with %msil_canon as the lister (in single quotes: it holds
    # double-quoted paths) and the allow-list; callers add --oracle, --test
    # (with --triple and --level for qualified skip lines), the --allow names
    # and the two images.
    (
        "%msil_equiv",
        "%s --canon '%s' --allowed %s"
        % (script("msil-equiv.py"), canon_cmd, quote(equiv_allowed)),
    ),
    # The placeholders of %{msil_prog}, set by REDEFINE: lines (plan 5.12.3).
    ("%{msil_srcs}", "%s"),
    ("%{msil_stock_srcs}", ""),
    ("%{msil_archives}", ""),
    ("%{msil_libs}", ""),
    ("%{msil_native_libs}", ""),
    ("%{msil_deps}", ""),
    # The shared libraries of a %{msil_prog} test (M22; msil-prog.py
    # --shlibs, which %msil_prog_build passes).
    ("%{msil_shlibs}", ""),
    # Extra compile flags, after %{msil_cflags} (plan 5.12.3, M23): for both
    # builds (the MSIL build and the native oracle's) of %{msil_prog} and
    # %{msil_det}, and for every build of %{msil_c} on every pipeline: the
    # one-source program under obj and lto, the -S compile and the native
    # leg under asm, and O3's and O6's asm reference (msil_equiv=1).
    # %{msil_ll} takes none, because llc has no C flags (its native leg
    # compiles IR). msil-prog.py's --cflags is '%{msil_cflags}
    # %{msil_copts}', so a value may not contain a single quote.
    ("%{msil_copts}", ""),
    # The --expect stem of every run of every composite (plan 5.12.3, M23):
    # <stem>[.<os>].expected; a directory or test that shares one expected
    # file REDEFINEs it (c-testsuite's %S/exit0).
    ("%{msil_expect}", "%S/%{s:stem}"),
    # msil_also_run for explicit RUN lines (see "more runtimes"; plan 5.12.3,
    # M23): the check comes first, so the image placeholder in its expansion
    # takes the test's REDEFINE value in the same pass; a value that is a
    # placeholder of the test's own expands on a later pass
    # (recursiveExpansionLimit).
    ("%{msil_also_run_check}", also_run_check),
    ("%{msil_also_run_image}", "msil-also-run-image-not-set"),
    # The exit corpus' cache (see "exit corpus"; plan 5.12.3, M23).
    ("%{msil_corpus}", slashes(corpus_dir) if corpus_dir else "msil-corpus-not-found"),
    # The MSIL link's harness flags (plan 5.12.3, M15; see msil_ldflags):
    # --sysroot=<msil_linux_sysroot> on Linux triples, so the link reads the
    # default libraries of plan 5.10.4, and from M21 %{msil_ref_flags}, the
    # references of msil_references (see "reference assemblies"). Under
    # msil_references=none the expansion is M15's (on a Linux triple with a
    # sysroot followed by a blank, which msil-prog.py and the shell drop).
    (
        "%{msil_ldflags}",
        (msil_ldflags + " " if msil_ldflags else "") + "%{msil_ref_flags}",
    ),
    # -Wl,--reference=<dir> per msil_references, else empty (plan 5.12.3).
    # It stays a placeholder inside %{msil_ldflags}, so a test REDEFINEs it.
    (
        "%{msil_ref_flags}",
        "-Wl,--reference=" + quote(ref_dir) if ref_dir else "",
    ),
    # The reference packs for tests that need one whatever the leg
    # (REQUIRES: msil-refpack=<name>; plan 5.12.3).
    *[
        (
            "%%{msil_ref_dir_%s}" % n,
            quote(ref_packs[n]) if n in ref_packs else "msil-ref-dir-%s-not-found" % n,
        )
        for n in REF_PACKS
    ],
    # The glibc sysroot of the hosted tests (msil_glibc_sysroot).
    (
        "%{msil_glibc_sysroot}",
        quote(glibc_sysroot) if glibc_sysroot else "glibc-sysroot-not-found",
    ),
    # msil-ld under msil-run.py's limits, and the tree's llvm-ar and llvm-nm.
    (
        "%msil_ld",
        "%s --tool%s -- %s"
        % (script("msil-run.py"), limits, quote(msil_ld or "msil-ld-not-found")),
    ),
    ("%msil_ar", quote(msil_ar or "llvm-ar-not-found")),
    ("%msil_nm", quote(msil_nm or "llvm-nm-not-found")),
    # check-mscorlib-safe.py (plan 5.11.7, 5.12.3; M21): every entry of
    # msil-ld --print-bcl-table touched through [mscorlib] by a probe that
    # this configuration's ilasm assembles and that runs on the runtimes of
    # this run (see mscorlib_safe_cmd); callers add --out and options.
    ("%msil_check_mscorlib_safe", mscorlib_safe_cmd),
    # build-jitall.py --csharp SRC --out EXE [--reference DLL]...
    # [--refs net8|net48] [--unsafe] with this configuration's dotnet (plan
    # 5.12.3, M22; feature msil-csc, see "C# compiler").
    (
        "%msil_csc",
        "%s%s --csharp"
        % (
            script("jitall/build-jitall.py"),
            " --dotnet " + quote(dotnet) if dotnet else "",
        ),
    ),
    # -std=gnu17 pins the C dialect against a change of clang's default
    # (C23 rejects the K&R definitions of 11-programs/kr-legacy.c; backlog
    # 3.4). -ffreestanding turns off main's implicit 'return 0'
    # (FunctionDecl::isMain in clang/lib/AST/Decl.cpp), so falling off main
    # must be an error; MSVCRT triples keep the implicit return
    # (isMSVCRTEntryPoint), natively and for MSIL alike. Directories may
    # replace this (hosted C in phase 3). update-expected.py uses the same
    # flags.
    (
        "%{msil_cflags}",
        "-std=gnu17 -ffreestanding -Werror=return-type -I" + quote(inputs_dir),
    ),
    # -lm for non-Windows native triples, the compiler-rt builtins for MSVC
    # triples, else empty (see native_ldflags).
    ("%{msil_native_ldflags}", native_ldflags),
    # The E7 Linux oracle (see linux_cc): compiler, linker with start.o, the
    # sysroot's libraries, and the WSL run of the result.
    ("%{msil_linux_cc}", linux_cc),
    # update-expected.py --os linux with the same E7 configuration.
    ("%{msil_e7_args}", e7_args),
    ("%{msil_linux_ld}", linux_ld),
    ("%{msil_linux_libs}", linux_libs),
    (
        "%{msil_linux_exec}",
        "%%msil_exec --native --runner wsl --wsl-distro %s --expect-os linux"
        % wsl_distro,
    ),
    # ld.lld and lld-link (plan 5.12.3): the tree's, else PATH, else the
    # Visual Studio LLVM's (before M13 builds lld).
    ("%ld_lld", quote(ld_lld or "ld.lld-not-found")),
    ("%lld_link", quote(lld_link or "lld-link-not-found")),
    ("%msil_triple", msil_triple),
    ("%clang_msil", "%s --target=%s" % (quote(clang), msil_triple)),
    ("%clang_native", "%s --target=%s" % (quote(clang), native_triple)),
    ("%llc_msil", "%s -mtriple=%s" % (quote(llc), msil_triple)),
    ("%ilasm", ilasm_cmd),
    ("%dotnet", quote(dotnet or "dotnet-not-found")),
    ("%msil_exec", exec_cmd),
    ("%msil_verify", verify_cmd),
    ("%msil_jitall", jitall_cmd),
    ("%msil_roundtrip", roundtrip_cmd),
    ("%msil_mdcanon", mdcanon_cmd),
    ("%msil_cil_dump", cil_dump_cmd),
    ("%msil_canon", canon_cmd),
    ("%msil_inputs", slashes(inputs_dir)),
]
# lit treats each name as a regular expression; none of these names contains a
# regex metacharacter that matters or is a prefix of another one, so a
# directory can replace one by its plain name, for example
#   config.substitutions = [s for s in config.substitutions
#                           if s[0] != "%{msil_cflags}"]
config.substitutions.extend(substitutions)
