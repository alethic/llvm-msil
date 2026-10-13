# -*- Python -*-
#
# lit configuration for the MSIL execution tests:
#
#   prog.c --clang --target=<native>-msil -S--> prog.il --ilasm--> prog.dll
#          --ilverify--> (no stack-shape errors) --dotnet exec--> transcript
#
# Each test is compared with a checked-in expected transcript (stdout plus an
# "[exit N]" line, see msil/utils/msil-run.py). When --param
# msil_native_oracle=1 is given, the same source is also built natively with
# the just-built clang for the host and must produce the same transcript.
#
# Nothing here is fatal when .NET is absent: the directories that need it
# mark themselves UNSUPPORTED, so check-all keeps working everywhere.

import glob
import json
import os
import platform
import re
import subprocess

import lit.formats
import lit.util
from lit.llvm import llvm_config
from lit.llvm.subst import ToolSubst

config.name = "MSIL"
# LLVM 23 deprecates the external shell; RUN lines use lit's internal shell
# on every host, so they must not rely on bash.
config.test_format = lit.formats.ShTest()
config.suffixes = [".c", ".il", ".ll", ".test"]
config.excludes = ["Inputs", "CMakeLists.txt", "README.md"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.msil_obj_root, "test")
# %{msil_c} expands to other substitutions, which expand again.
config.recursiveExpansionLimit = 10

llvm_config.use_default_substitutions()  # FileCheck, not, count, %python
llvm_config.use_clang(required=False)  # %clang, %clang_cc1, feature 'clang'

params = lit_config.params
features = config.available_features
utils_dir = os.path.join(config.msil_src_root, "utils")
inputs_dir = os.path.join(config.test_source_root, "Inputs")
system = platform.system()
is_windows = system == "Windows"
exe_ext = ".exe" if is_windows else ""


def note(msg):
    lit_config.note("msil: " + msg)


def quote(path):
    return '"%s"' % path


def version_key(path_or_version):
    """'10.0.1' sorts after '8.0.31'."""
    m = re.search(r"(\d+)\.(\d+)\.(\d+)", path_or_version)
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)


def first_file(candidates):
    for c in candidates:
        # X_OK matters on Linux: NuGet may extract ilasm without the x bit.
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return os.path.normpath(c)
    return None


def find_tool(name, site_value, extra=()):
    """Precedence: --param msil_<name>=PATH, $MSIL_<NAME>, CMake, PATH, extra.

    An explicitly empty override (--param msil_ilverify=) disables the tool.
    """
    override = params.get("msil_" + name)
    if override is None:
        override = os.environ.get("MSIL_" + name.upper())
    if override is not None:
        return first_file([override])
    return first_file([site_value, lit.util.which(name), *extra])


def host_rid():
    arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    return {"Windows": "win", "Linux": "linux", "Darwin": "osx"}.get(system, "") + "-" + arch


def default_msil_triple():
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64",
            "aarch64": "aarch64"}.get(platform.machine().lower(), "x86_64")
    if is_windows:
        return arch + "-pc-windows-msvc-msil"
    if system == "Darwin":
        return arch + "-apple-darwin-msil"
    return arch + "-unknown-linux-gnu-msil"


# ---------------------------------------------------------------- the target
if "MSIL" in config.targets_to_build:
    features.add("msil-registered-target")
for t in config.targets_to_build:
    features.add(t.lower() + "-registered-target")

msil_triple = params.get("msil_triple") or default_msil_triple()
native_triple = msil_triple[: -len("-msil")] if msil_triple.endswith("-msil") else msil_triple
features.add("msil-triple=" + msil_triple)
if "-windows-" in msil_triple:
    features.add("msil-triple-windows")
elif "-linux-" in msil_triple:
    features.add("msil-triple-linux")

# ---------------------------------------------------------------- ilasm
windir = os.environ.get("WINDIR", r"C:\Windows")
netfx_dir = os.path.join(windir, "Microsoft.NET", "Framework64", "v4.0.30319")
nuget_root = os.environ.get("NUGET_PACKAGES") or os.path.join(
    os.path.expanduser("~"), ".nuget", "packages")
rid = host_rid()
ilasm_extra = []
if is_windows:
    ilasm_extra.append(os.path.join(netfx_dir, "ilasm.exe"))
# runtime.<rid>.Microsoft.NETCore.ILAsm from NuGet (the only ilasm on Linux/macOS).
ilasm_extra += sorted(
    glob.glob(os.path.join(nuget_root, "runtime.%s.microsoft.netcore.ilasm" % rid,
                           "*", "runtimes", rid, "native", "ilasm" + exe_ext)),
    key=version_key, reverse=True)
ilasm = find_tool("ilasm", getattr(config, "msil_ilasm", ""), ilasm_extra)
if ilasm:
    features.add("msil-ilasm")
    note("ilasm: " + ilasm)
else:
    note("ilasm not found; execution tests are UNSUPPORTED")

# ---------------------------------------------------------------- dotnet
dotnet_extra = [os.path.join(os.environ.get("DOTNET_ROOT", ""), "dotnet" + exe_ext)]
dotnet_extra += ([r"C:\Program Files\dotnet\dotnet.exe"] if is_windows else
                 ["/usr/share/dotnet/dotnet", "/usr/lib/dotnet/dotnet",
                  "/usr/local/share/dotnet/dotnet",
                  os.path.join(os.path.expanduser("~"), ".dotnet", "dotnet")])
dotnet = find_tool("dotnet", getattr(config, "msil_dotnet", ""), dotnet_extra)

runtimes = {}  # major -> (version string, directory of that runtime)
if dotnet:
    try:
        listing = subprocess.run([dotnet, "--list-runtimes"], capture_output=True,
                                 text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        listing = ""
    for m in re.finditer(r"^Microsoft\.NETCore\.App (\S+) \[(.+)\]\s*$", listing, re.M):
        ver, base = m.group(1), m.group(2)
        major = version_key(ver)[0]
        if major not in runtimes or version_key(ver) > version_key(runtimes[major][0]):
            runtimes[major] = (ver, os.path.join(base, ver))
for major in runtimes:
    features.add("dotnet-runtime-%d" % major)

wanted = params.get("msil_dotnet_runtime")  # e.g. "8" or "10"
if wanted:
    runtime_major = int(wanted) if int(wanted) in runtimes else None
    if runtime_major is None:
        lit_config.warning("msil: .NET runtime %s requested but not installed" % wanted)
else:
    # Design.md 7.1: net8.0 is the default profile; otherwise take the newest.
    runtime_major = 8 if 8 in runtimes else (max(runtimes) if runtimes else None)

runtimeconfig = None
runtime_dir = None
if dotnet and runtime_major is not None:
    features.add("msil-dotnet")
    runtime_dir = runtimes[runtime_major][1]
    os.makedirs(config.test_exec_root, exist_ok=True)
    runtimeconfig = os.path.join(config.test_exec_root,
                                 "msil-net%d.runtimeconfig.json" % runtime_major)
    with open(runtimeconfig, "w") as f:
        json.dump({"runtimeOptions": {
            "tfm": "net%d.0" % runtime_major,
            "rollForward": "LatestPatch",
            "framework": {"name": "Microsoft.NETCore.App",
                          "version": "%d.0.0" % runtime_major}}}, f, indent=2)
    note("dotnet: %s (Microsoft.NETCore.App %s)" % (dotnet, runtimes[runtime_major][0]))
else:
    note("no usable .NET runtime; execution tests are UNSUPPORTED")

# ---------------------------------------------------------------- verifiers
ilverify = find_tool("ilverify", getattr(config, "msil_ilverify", ""), [
    os.path.join(os.path.expanduser("~"), ".dotnet", "tools", "ilverify" + exe_ext)])
if ilverify:
    try:  # dotnet-ilverify 10.x is itself a net10.0 app; make sure it starts.
        ok = subprocess.run([ilverify, "--version"], capture_output=True,
                            timeout=120).returncode == 0
    except (OSError, subprocess.SubprocessError):
        ok = False
    if not ok:
        note("ilverify at %s does not run; verification disabled" % ilverify)
        ilverify = None
if ilverify and runtime_dir:
    features.add("msil-ilverify")

peverify = None  # --param msil_peverify= (empty) turns it off
if is_windows:
    sdk = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    hits = glob.glob(os.path.join(sdk, "Microsoft SDKs", "Windows", "v*", "bin",
                                  "NETFX 4.* Tools", "x64", "PEVerify.exe"))
    peverify = find_tool("peverify", getattr(config, "msil_peverify", ""),
                         sorted(hits, reverse=True))
    if peverify:
        features.add("msil-peverify")

if is_windows and os.path.isfile(os.path.join(netfx_dir, "clr.dll")):
    features.add("msil-netfx")
    if lit.util.pythonize_bool(params.get("msil_run_netfx", "0")):
        features.add("msil-run-netfx")

# ---------------------------------------------------------------- native oracle
oracle = params.get("msil_native_oracle", "0")
if oracle not in ("0", "", "false", "False", "off"):
    host_target = {"x86_64": "X86", "aarch64": "AArch64"}.get(native_triple.split("-")[0])
    problems = []
    if not getattr(config, "clang", None):
        problems.append("clang not built")
    if host_target not in config.targets_to_build:
        problems.append("%s target not built" % host_target)
    if is_windows and not os.environ.get("LIB"):
        problems.append("LIB not set (run from a VS developer prompt)")
    if problems:
        msg = "msil: native oracle unavailable: " + ", ".join(problems)
        (lit_config.fatal if oracle == "required" else lit_config.warning)(msg)
    else:
        features.add("msil-native-oracle")

# All prerequisites for compiling and running C programs.
if {"clang", "msil-registered-target", "msil-ilasm", "msil-dotnet"} <= features:
    features.add("msil-exec")

# ---------------------------------------------------------------- environment
for var in ("DOTNET_ROOT", "DOTNET_ROOT_X64", "DOTNET_ROOT_ARM64", "NUGET_PACKAGES",
            "ProgramFiles", "ProgramFiles(x86)", "ProgramData", "LOCALAPPDATA",
            "APPDATA", "WINDIR", "LIBPATH"):
    if var in os.environ:
        config.environment[var] = os.environ[var]
config.environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
config.environment["DOTNET_NOLOGO"] = "1"

# ---------------------------------------------------------------- substitutions
python = quote(config.python_executable)
run_script = quote(os.path.join(utils_dir, "msil-run.py"))
verify_script = quote(os.path.join(utils_dir, "msil-verify.py"))

triple_os = ("windows" if "-windows-" in msil_triple else
             "darwin" if "-apple-" in msil_triple else "linux")
exec_cmd = "%s %s --expect-os %s" % (python, run_script, triple_os)
if dotnet and runtimeconfig:
    exec_cmd += " --dotnet %s --runtimeconfig %s" % (quote(dotnet), quote(runtimeconfig))
for kv in filter(None, params.get("msil_dotnet_env", "").split(";")):
    exec_cmd += " --env " + kv  # e.g. DOTNET_TieredCompilation=0

if "msil-ilverify" in features:
    verify_cmd = "%s %s --ilverify %s --runtime-dir %s --allow %s" % (
        python, verify_script, quote(ilverify), quote(runtime_dir),
        quote(os.path.join(utils_dir, "ilverify-allowed.txt")))
    if peverify:
        verify_cmd += " --peverify " + quote(peverify)
else:
    verify_cmd = "%s %s --skip" % (python, verify_script)

clang = getattr(config, "clang", None) or "clang-not-found"
tools = [
    ToolSubst("%msil_triple", command=msil_triple),
    ToolSubst("%clang_msil", command=quote(clang), extra_args=["--target=" + msil_triple]),
    ToolSubst("%clang_native", command=quote(clang), extra_args=["--target=" + native_triple]),
    ToolSubst("%ilasm", command=quote(ilasm or "ilasm-not-found"),
              extra_args=["-nologo", "-quiet"]),
    ToolSubst("%dotnet", command=quote(dotnet or "dotnet-not-found")),
    ToolSubst("%msil_exec", command=exec_cmd),
    ToolSubst("%msil_verify", command=verify_cmd),
]
llvm_config.add_tool_substitutions(tools)
config.substitutions.append(("%msil_inputs", inputs_dir))

# %{msil_c}: the whole compile -> assemble -> verify -> run -> compare pipeline,
# once per optimization level in --param msil_opt_levels (default O0,O2).
# Each level writes %t.<level>.{il,dll,out}. The native-oracle and .NET
# Framework legs are guarded by %if so the line stays valid when disabled.
opt_levels = [o.strip() for o in params.get("msil_opt_levels", "O0,O2").split(",") if o.strip()]


def c_pipeline(level):
    t = "%t." + level
    cmd = " && ".join([
        "%clang_msil %{msil_cflags} -" + level + " -S %s -o " + t + ".il",
        "%ilasm -dll -output=" + t + ".dll " + t + ".il",
        "%msil_verify " + t + ".dll",
        "%msil_exec --expect %S/%{s:stem} --out " + t + ".out " + t + ".dll",
    ])
    cmd += (" %if msil-native-oracle %{ && %clang_native %{msil_cflags} -" + level
            + " %s -o " + t + ".native.exe && %msil_exec --native --expect %S/%{s:stem}"
            + " --out " + t + ".native.out " + t + ".native.exe %}")
    cmd += (" %if msil-run-netfx %{ && %ilasm -exe -output=" + t + ".exe " + t + ".il"
            + " && %msil_exec --netfx --expect %S/%{s:stem} --out " + t + ".netfx.out "
            + t + ".exe %}")
    return cmd


config.substitutions.append(("%{msil_c}", " && ".join(c_pipeline(o) for o in opt_levels)))


# %{msil_ll}: the same for hand-written IR run through llc. llc does no IPO,
# so values passed to noinline functions stay opaque even at -O2; this is how
# the early gates get runtime coverage before globals and frames exist.
# The .ll files carry no target triple or datalayout.
def ll_pipeline(level):
    t = "%t." + level
    cmd = " && ".join([
        "%llc_msil -" + level + " %s -o " + t + ".il",
        "%ilasm -dll -output=" + t + ".dll " + t + ".il",
        "%msil_verify " + t + ".dll",
        "%msil_exec --expect %S/%{s:stem} --out " + t + ".out " + t + ".dll",
    ])
    cmd += (" %if msil-native-oracle %{ && %clang_native -Wno-override-module -" + level
            + " %s -o " + t + ".native.exe && %msil_exec --native --expect %S/%{s:stem}"
            + " --out " + t + ".native.out " + t + ".native.exe %}")
    return cmd


llc = lit.util.which("llc", config.llvm_tools_dir) or "llc-not-found"
llvm_config.add_tool_substitutions(
    [ToolSubst("%llc_msil", command=quote(llc), extra_args=["-mtriple=" + msil_triple])])
config.substitutions.append(("%{msil_ll}", " && ".join(ll_pipeline(o) for o in opt_levels)))
# Directories override this in lit.local.cfg (milestone 1: freestanding).
config.substitutions.append(("%{msil_cflags}", "-ffreestanding -I" + quote(inputs_dir)))
