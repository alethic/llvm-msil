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
# (on Windows) PEVerify, ilasm or .NET runtime for the selected runner a
# configuration error, even when an empty --param msil_<tool>= or
# $MSIL_<TOOL> disabled it on purpose, so a gate never passes with its
# verification or every harness test UNSUPPORTED; --param msil_require_exec=1
# (the gate legs of msil/utils/ci pass it) does the same for msil-exec, which
# M1/ needs (clang with the MSIL target, ilasm and a .NET runtime; see
# M1/lit.local.cfg); and an explicitly requested runner, runtime, leg or tool
# that cannot be used is one too.

import glob
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


def pinned_tools_version():
    """MsilILAsmVersion of msil/utils/tools/msil-tools.proj: the ilasm and
    ildasm version that the CI restores and the gate legs use (read the way
    msil/utils/ci/ci-common.ps1 and msil-ci.yml read it; the project file is
    the only place that sets it)."""
    proj = os.path.join(utils_dir, "tools", "msil-tools.proj")
    try:
        with open(proj, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        fatal("cannot read %s: %s" % (proj, e))
    m = re.search(r"<MsilILAsmVersion[^>]*>\s*([0-9.]+)\s*</MsilILAsmVersion>", text)
    if not m:
        fatal("cannot read MsilILAsmVersion from " + proj)
    return m.group(1)


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

# Everything needed to compile and run C programs.
if {"clang", "msil-registered-target", "msil-ilasm", "msil-dotnet"} <= features:
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

exec_cmd = "%s --expect-os %s%s" % (script("msil-run.py"), triple_os, limits)
if runner == "wsl":
    exec_cmd += " --runner wsl --wsl-distro %s --dotnet %s" % (
        wsl_distro,
        quote(wsl_dotnet),
    )
elif dotnet:
    exec_cmd += " --dotnet " + quote(dotnet)
if exec_runtimeconfig:
    exec_cmd += " --runtimeconfig " + quote(exec_runtimeconfig)
exec_cmd += env_args

if "msil-ilverify" in features:
    verify_cmd = "%s --ilverify %s --runtime-dir %s --allow %s%s" % (
        script("msil-verify.py"),
        quote(ilverify),
        quote(local_runtime_dir),
        quote(os.path.join(utils_dir, "ilverify-allowed.txt")),
        limits,
    )
    if peverify:
        verify_cmd += " --peverify " + quote(peverify)
    # The neutralized pass (msil-verify.py pass 2) reassembles with %ilasm's
    # assembler; msil-verify.py applies the limits itself.
    if ildasm and ilasm:
        verify_cmd += " --ildasm %s --ilasm %s" % (quote(ildasm), quote(ilasm))
else:
    verify_cmd = script("msil-verify.py") + " --skip"

jitall_cmd = "%s --jitall %s%s" % (
    script("msil-jitall.py"),
    quote(jitall_dir or "jitall-disabled"),
    limits,
)
if runner == "wsl":
    # In WSL on the runtime of the programs: the module initializer runs.
    jitall_cmd += " --runner wsl --wsl-distro %s --dotnet %s" % (
        wsl_distro,
        quote(wsl_dotnet),
    )
    if exec_runtimeconfig:
        jitall_cmd += " --runtimeconfig " + quote(exec_runtimeconfig)
else:
    if dotnet:
        jitall_cmd += " --dotnet " + quote(dotnet)
    if local_runtimeconfig:
        jitall_cmd += " --runtimeconfig " + quote(local_runtimeconfig)
jitall_cmd += env_args

# Without msil_roundtrip=1 nothing runs it (the composites test the feature),
# and msil-verify.py rejects --roundtrip without the tools.
roundtrip_cmd = script("msil-verify.py") + " --roundtrip"
if rt_ildasm:
    roundtrip_cmd += " --ildasm %s --ilasm %s%s" % (
        quote(rt_ildasm),
        quote(rt_ilasm),
        limits,
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
        f" && %msil_exec --native --expect %S/%{{s:stem}}"
        f" --out {t}.native.out {exe} %}}"
    )


def roundtrip_leg(t):
    """msil_roundtrip: the ildasm -> ilasm round trip of <t>.dll, then a run of
    the reassembled image against the same .expected file."""
    return (
        f" %if msil-roundtrip %{{ && %msil_roundtrip --output={t}.rt.dll {t}.dll"
        f" && %msil_exec --expect %S/%{{s:stem}} --out {t}.rt.out {t}.rt.dll %}}"
    )


def c_pipeline(level):
    """compile -> assemble -> verify -> force-JIT -> run and compare, at -<level>."""
    t = "%t." + level
    cmd = (
        f"%clang_msil %{{msil_cflags}} -{level} -S %s -o {t}.il"
        f" && %ilasm -dll -output={t}.dll {t}.il"
        f" && %msil_verify {t}.dll"
        f" %if msil-jitall %{{ && %msil_jitall {t}.dll %}}"
        f" && %msil_exec --expect %S/%{{s:stem}} --out {t}.out {t}.dll"
    )
    cmd += roundtrip_leg(t)
    cmd += native_leg(t, level, "%{msil_cflags}")
    cmd += (
        f" %if msil-run-netfx %{{ && %ilasm -exe -output={t}.exe {t}.il"
        f" && %msil_exec --netfx --expect %S/%{{s:stem}} --out {t}.netfx.out {t}.exe %}}"
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
        f" && %msil_exec --expect %S/%{{s:stem}} --out {t}.out {t}.dll"
    )
    cmd += roundtrip_leg(t)
    cmd += native_leg(t, level, "-Wno-override-module")
    cmd += (
        f" %if msil-run-netfx %{{ && %ilasm -exe -output={t}.exe {t}.il"
        f" && %msil_exec --netfx --expect %S/%{{s:stem}} --out {t}.netfx.out {t}.exe %}}"
    )
    return cmd


# llc takes -O0..-O3 only; -Os/-Oz levels are skipped for IR tests.
ll_levels = [o for o in opt_levels if o in ("O0", "O1", "O2", "O3")] or ["O2"]

substitutions = [
    # Composites first; they expand into the atoms below.
    ("%{msil_c}", " && ".join(c_pipeline(o) for o in opt_levels)),
    ("%{msil_ll}", " && ".join(ll_pipeline(o) for o in ll_levels)),
    *[
        ("%%{msil_c_%s}" % o, c_pipeline(o))
        for o in ("O0", "O1", "O2", "O3", "Os", "Oz")
    ],
    *[("%%{msil_ll_%s}" % o, ll_pipeline(o)) for o in ("O0", "O1", "O2", "O3")],
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
    ("%msil_inputs", slashes(inputs_dir)),
]
# lit treats each name as a regular expression; none of these names contains a
# regex metacharacter that matters or is a prefix of another one, so a
# directory can replace one by its plain name, for example
#   config.substitutions = [s for s in config.substitutions
#                           if s[0] != "%{msil_cflags}"]
config.substitutions.extend(substitutions)
