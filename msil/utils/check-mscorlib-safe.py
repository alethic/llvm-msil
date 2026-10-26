#!/usr/bin/env python3
# ===-- check-mscorlib-safe.py - Prove msil-ld's built-in mscorlib table --===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Prove that every entry of msil-ld's built-in [mscorlib] table resolves.

    check-mscorlib-safe.py --msil-ld PATH --out DIR [--table FILE]
        [--ilasm PATH] [--runtimes netfx,net8,net10,wsl-net10] [--dotnet PATH]
        [--wsl-distro NAME] [--wsl-dotnet PATH|private] [--timeout S]
        [--mem-limit-mb N] [-v]
    check-mscorlib-safe.py --self-test --out DIR [options as above]

Plan 5.11.7 (Q10, D10; M21). Without references msil-ld binds the BCL types
and members its output needs to mscorlib 4.0.0.0 (b77a5c561934e089) through
its built-in table, which holds the mscorlib-safe entries of
llvm/include/llvm/CLI/BCLMembers.def. An entry is safe when a reference to it
through [mscorlib] resolves on every runtime: .NET Framework 4.8, whose
mscorlib defines it, and .NET 8 and .NET 10, whose mscorlib facade forwards
the type to the assembly that defines it, where the member must then exist
(a forwarded type is not a member that exists: Math::FusedMultiplyAdd
resolves on modern .NET only; llvm/docs/MSIL/recon/p2/bcl-binding.md 2.4,
whose tool/mscorlib_safe.py followed the forwarders statically). This script
proves it by running code. An entry joins BCLMembers.def's table only with a
passing run (plan 5.11.7).

The table is the output of 'msil-ld --print-bcl-table' (plan 5.11.1), or
--table FILE in the same form: one entry per line, 'type ', 'method ' or
'field ' followed by its ILAsm reference through [mscorlib], for example

    type [mscorlib]System.ValueType
    method void [mscorlib]System.Threading.Interlocked::MemoryBarrier()

Blank lines are ignored; any other line, or an empty table, is a usage error.
DIR/table.txt receives the table that was checked.

For each runtime of --runtimes the script writes an ILAsm probe,
DIR/<runtime>/probe-<n>.il: one static method per entry that loads the
entry's token ('ldtoken <type>', 'ldtoken method <method>', 'ldtoken field
<field>'), marked noinlining, and an entry point that calls them in table
order, each call inside a try block whose handler wraps any exception in
System.Exception('check-mscorlib-safe: entry <i> failed: <entry>') and
rethrows it, and that returns 42 when every call returned. The JIT resolves
an ldtoken operand when it compiles the method, so a type the runtime cannot
load is a TypeLoadException and a member it does not define a
MissingMethodException (or MissingFieldException) at that call. --ilasm
assembles the probe ('-exe', and '-pe64 -x64' or '-pe64 -arm64' on 64-bit
hosts) into DIR/<runtime>/probe-<n>.exe, an EXE image, which runs on the
.NET Framework and under 'dotnet exec' alike, and msil/utils/msil-run.py runs
it under its timeout and memory cap:

  netfx        msil-run.py --netfx (Windows, .NET Framework 4.x installed)
  net<N>       msil-run.py --dotnet DOTNET --runtimeconfig
               DIR/net<N>.runtimeconfig.json (Microsoft.NETCore.App <N>.x,
               rollForward LatestPatch), on the host
  wsl-net<N>   the same inside WSL (msil-run.py --runner wsl; Windows hosts):
               --wsl-dotnet is 'private' (default: $HOME/.dotnet-msil/dotnet,
               the runtimes of msil/utils/msil-wsl-dotnet.sh) or an absolute
               path inside the distribution --wsl-distro (default Ubuntu)

The default is netfx,net8,net10,wsl-net10, the four runtimes of plan 5.11.7.
A runtime that is not installed is a configuration error, never a skip. A
run passes when its transcript ends in '[exit 42]'. When an entry fails, the
script reports the runtime, the entry, the exception's type and message,
removes that entry and runs the probe again, so one run of the script names
every failing entry. Anything else (a probe that does not load, a timeout)
fails the runtime with msil-run.py's transcript.

--self-test checks the probe itself, without msil-ld: a malformed table is
rejected, and a table of Phase 1's six entities (System.ValueType and the
five members of plan D10) with 'method float32
[mscorlib]System.MathF::Sqrt(float32)' as its second entry runs on every
runtime of --runtimes: the six must resolve everywhere, and MathF::Sqrt must
fail on the .NET Framework, whose mscorlib has no MathF, with a
TypeLoadException that names it (on .NET 8 and .NET 10 the mscorlib facade
forwards MathF, so it resolves there; that is reported, not required).
Without netfx in --runtimes the MathF half is noted as not checked.

Exit status: 0 when every entry resolves on every runtime (or the self-test
passes); 1 when one does not; 2 on a usage or configuration error.

Python standard library only.
"""

import argparse
import importlib.util
import json
import os
import platform
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IS_WINDOWS = os.name == "nt"
DEFAULT_RUNTIMES = "netfx,net8,net10,wsl-net10"
RUNTIME_RE = re.compile(r"^(?:netfx|net(?P<net>\d+)|wsl-net(?P<wsl>\d+))$")
ENTRY_RE = re.compile(r"^(?P<kind>type|method|field) (?P<ref>(?=.*\[mscorlib\])\S.*)$")
# The handler's message, and the exception it wraps as the CLRs print it
# ('... ---> System.TypeLoadException: Could not load type ...').
FAILED_RE = re.compile(r"check-mscorlib-safe: entry (?P<index>\d+) failed: ")
INNER_RE = re.compile(r"--->\s*(?P<type>[\w.`+]+):\s*(?P<message>[^\r\n]*)")
PASS_STATUS = "[exit 42]"
TOOL_TIMEOUT = 600

# Phase 1's six entities (llvm/docs/MSIL/recon/p2/bcl-binding.md 1.1, plan
# D10) and the entry that the .NET Framework's mscorlib lacks.
PHASE1_SIX = (
    "type [mscorlib]System.ValueType",
    "method native int [mscorlib]System.Runtime.InteropServices.Marshal"
    "::AllocHGlobal(native int)",
    "method native int [mscorlib]System.Runtime.InteropServices.Marshal"
    "::StringToHGlobalAnsi(string)",
    "method string[] [mscorlib]System.Environment::GetCommandLineArgs()",
    "method void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64)",
    "method void [mscorlib]System.Threading.Interlocked::MemoryBarrier()",
)
MATHF_SQRT = "method float32 [mscorlib]System.MathF::Sqrt(float32)"


class UsageError(Exception):
    pass


def load_msil_run():
    # No __pycache__ next to the scripts in the source tree.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        "msil_run", os.path.join(HERE, "msil-run.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msil_run = load_msil_run()


def say(text, stream=sys.stdout):
    stream.write("check-mscorlib-safe: %s\n" % text)
    stream.flush()


def run(cmd, timeout=TOOL_TIMEOUT, cwd=None):
    """(exit status, output text) of cmd; raises UsageError when it cannot
    start or does not finish."""
    try:
        res = subprocess.run(
            cmd,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        raise UsageError("%s did not finish within %d s" % (cmd[0], timeout))
    except OSError as e:
        raise UsageError("cannot run %s: %s" % (cmd[0], e))
    out = res.stdout
    text = (
        out.decode("utf-16-le", "replace")
        if b"\0" in out
        else out.decode("utf-8", "replace")
    )
    return res.returncode, text.replace("\r\n", "\n")


# ---------------------------------------------------------------- the table


def parse_table(text, source):
    """The entries of a table text: [(kind, reference)]."""
    entries = []
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        m = ENTRY_RE.match(line)
        if not m:
            raise UsageError(
                "%s:%d: %r is not an entry: expected 'type ', 'method ' or 'field ' "
                "followed by a reference through [mscorlib]" % (source, n, line)
            )
        entries.append((m.group("kind"), m.group("ref")))
    if not entries:
        raise UsageError("%s has no entry" % source)
    return entries


def read_table(args):
    """(entries, where they came from)."""
    if args.table:
        try:
            with open(args.table, encoding="utf-8") as f:
                text = f.read()
        except OSError as e:
            raise UsageError("cannot read --table %s: %s" % (args.table, e))
        return parse_table(text, args.table), args.table
    rc, out = run([args.msil_ld, "--print-bcl-table"])
    if rc != 0:
        raise UsageError(
            "'%s --print-bcl-table' failed (exit status %s):\n%s"
            % (args.msil_ld, msil_run.describe_exit(rc), out)
        )
    source = "%s --print-bcl-table" % os.path.basename(args.msil_ld)
    return parse_table(out, source), source


def entry_text(entry):
    return "%s %s" % entry


# ---------------------------------------------------------------- the probe


def il_string(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def probe_text(numbered):
    """The ILAsm probe for numbered = [(index, (kind, reference))]."""
    out = [
        "// check-mscorlib-safe.py: every entry below through [mscorlib]; the",
        "// entry point returns 42 when each token resolved (plan 5.11.7).",
        ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) "
        ".ver 4:0:0:0 }",
        ".assembly 'mscorlib-safe-probe' { .ver 0:0:0:0 }",
        ".module 'mscorlib-safe-probe.exe'",
        "",
    ]
    for index, (kind, ref) in numbered:
        operand = ref if kind == "type" else "%s %s" % (kind, ref)
        out += [
            "// entry %d: %s %s" % (index, kind, ref),
            ".method public static void 'entry%d'() cil managed noinlining" % index,
            "{",
            "  .maxstack 1",
            "  ldtoken %s" % operand,
            "  pop",
            "  ret",
            "}",
            "",
        ]
    out += [
        ".method public static int32 'Main'() cil managed",
        "{",
        "  .entrypoint",
        "  .maxstack 2",
        "  .locals init (class [mscorlib]System.Exception V_0)",
    ]
    for index, entry in numbered:
        out += [
            "  .try",
            "  {",
            "    call void 'entry%d'()" % index,
            "    leave L%d" % index,
            "  }",
            "  catch [mscorlib]System.Exception",
            "  {",
            "    stloc.0",
            "    ldstr %s"
            % il_string(
                "check-mscorlib-safe: entry %d failed: %s" % (index, entry_text(entry))
            ),
            "    ldloc.0",
            "    newobj instance void [mscorlib]System.Exception::.ctor(string, "
            "class [mscorlib]System.Exception)",
            "    throw",
            "  }",
            "L%d:" % index,
        ]
    out += ["  ldc.i4.s 42", "  ret", "}", ""]
    return "\n".join(out)


def ilasm_flags():
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return ["-pe64", "-x64"]
    if machine in ("arm64", "aarch64"):
        return ["-pe64", "-arm64"]
    return []


def assemble(args, text, il, exe):
    """Write the probe text to il and assemble it into exe."""
    with open(il, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if os.path.exists(exe):
        os.remove(exe)
    rc, out = run(
        [args.ilasm, "-nologo", "-quiet", "-exe", *ilasm_flags(), "-output=" + exe, il]
    )
    if rc != 0 or not os.path.isfile(exe):
        raise UsageError(
            "ilasm could not assemble the probe %s (exit status %s):\n%s"
            % (il, msil_run.describe_exit(rc), out)
        )


# ---------------------------------------------------------------- runtimes


class Runtime:
    """One runtime of --runtimes: its name and the msil-run.py options that
    run a program on it."""

    def __init__(self, name, mode, describe):
        self.name = name
        self.mode = mode
        self.describe = describe


def write_runtimeconfig(out_dir, major):
    path = os.path.join(out_dir, "net%d.runtimeconfig.json" % major)
    config = {
        "runtimeOptions": {
            "tfm": "net%d.0" % major,
            "rollForward": "LatestPatch",
            "framework": {"name": "Microsoft.NETCore.App", "version": "%d.0.0" % major},
        }
    }
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(config, indent=2) + "\n")
    return path


def newest_runtimes(listing):
    """{major: version} of the Microsoft.NETCore.App lines of
    'dotnet --list-runtimes'."""
    found = {}
    for m in re.finditer(r"^Microsoft\.NETCore\.App (\d+)\.(\S+) \[", listing, re.M):
        major, version = int(m.group(1)), "%s.%s" % (m.group(1), m.group(2))
        key = tuple(int(x) for x in re.findall(r"\d+", version)[:3])
        if major not in found or key > found[major][0]:
            found[major] = (key, version)
    return {major: v for major, (_, v) in found.items()}


def find_dotnet(explicit):
    if explicit:
        found = explicit if os.path.isfile(explicit) else shutil.which(explicit)
        if not found:
            raise UsageError("--dotnet %s: no such program" % explicit)
        return found
    candidates = [os.environ.get("MSIL_DOTNET"), shutil.which("dotnet")]
    root = os.environ.get("DOTNET_ROOT")
    if root:
        candidates.append(os.path.join(root, "dotnet.exe" if IS_WINDOWS else "dotnet"))
    candidates.append(
        r"C:\Program Files\dotnet\dotnet.exe"
        if IS_WINDOWS
        else "/usr/share/dotnet/dotnet"
    )
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    raise UsageError("no dotnet host found (pass --dotnet PATH)")


WSL_PROBE = (
    "command -v python3 >/dev/null || { echo 'python3 not found'; exit 3; }; "
    'p="$HOME/.dotnet-msil/dotnet"; '
    'case "$1" in '
    'private) if [ -x "$p" ]; then d=$p; else echo "missing: $p"; exit 4; fi ;; '
    '*) d=$1; [ -x "$d" ] || { echo "not executable: $d"; exit 6; } ;; '
    "esac; "
    'echo "dotnet: $d"; "$d" --list-runtimes'
)


def wsl_dotnet(args):
    """(the dotnet host inside WSL, its runtimes), probed once."""
    if args.wsl_cache is None:
        if not IS_WINDOWS:
            raise UsageError("the wsl-net<N> runtimes need a Windows host with WSL")
        if args.wsl_dotnet != "private" and not args.wsl_dotnet.startswith("/"):
            raise UsageError(
                "--wsl-dotnet %s: use private or an absolute path inside WSL"
                % args.wsl_dotnet
            )
        rc, out = run(
            [
                msil_run.wsl_exe(),
                "-d",
                args.wsl_distro,
                "--exec",
                "sh",
                "-c",
                WSL_PROBE,
                "sh",
                args.wsl_dotnet,
            ],
            timeout=300,
        )
        m = re.search(r"^dotnet: (/\S+)$", out, re.M)
        if rc != 0 or not m:
            raise UsageError(
                "probing the dotnet host %s in WSL %s failed (exit status %s):\n%s%s"
                % (
                    args.wsl_dotnet,
                    args.wsl_distro,
                    msil_run.describe_exit(rc),
                    out,
                    "(msil/utils/msil-wsl-dotnet.sh installs the private runtimes)\n"
                    if rc == 4
                    else "",
                )
            )
        args.wsl_cache = (m.group(1), newest_runtimes(out))
    return args.wsl_cache


def make_runtimes(args):
    """The Runtime objects of --runtimes, checked; raises UsageError."""
    names = [r.strip() for r in args.runtimes.split(",") if r.strip()]
    if not names:
        raise UsageError("--runtimes names no runtime")
    if len(set(names)) != len(names):
        raise UsageError("--runtimes %s names a runtime twice" % args.runtimes)
    runtimes = []
    host = None
    for name in names:
        m = RUNTIME_RE.match(name)
        if not m:
            raise UsageError(
                "--runtimes: unknown runtime %r (use netfx, net<N> or wsl-net<N>)"
                % name
            )
        if name == "netfx":
            windir = os.environ.get("WINDIR") or os.environ.get("SystemRoot") or ""
            clr = os.path.join(windir, "Microsoft.NET", "Framework64", "v4.0.30319")
            if not IS_WINDOWS or not os.path.isfile(os.path.join(clr, "clr.dll")):
                raise UsageError(
                    "--runtimes netfx: the .NET Framework 4.x is not installed here"
                )
            runtimes.append(Runtime(name, ["--netfx"], ".NET Framework 4.x"))
            continue
        major = int(m.group("net") or m.group("wsl"))
        config = write_runtimeconfig(args.out, major)
        if m.group("net"):
            if host is None:
                args.dotnet = find_dotnet(args.dotnet)
                rc, out = run([args.dotnet, "--list-runtimes"], timeout=120)
                host = newest_runtimes(out) if rc == 0 else {}
            if major not in host:
                raise UsageError(
                    "--runtimes %s: %s has no Microsoft.NETCore.App %d.x (it has %s)"
                    % (
                        name,
                        args.dotnet,
                        major,
                        ", ".join(host[k] for k in sorted(host)) or "none",
                    )
                )
            mode = ["--dotnet", args.dotnet, "--runtimeconfig", config]
            runtimes.append(
                Runtime(name, mode, "Microsoft.NETCore.App %s" % host[major])
            )
        else:
            dotnet, wsl = wsl_dotnet(args)
            if major not in wsl:
                raise UsageError(
                    "--runtimes %s: %s in WSL %s has no Microsoft.NETCore.App %d.x "
                    "(it has %s)"
                    % (
                        name,
                        dotnet,
                        args.wsl_distro,
                        major,
                        ", ".join(wsl[k] for k in sorted(wsl)) or "none",
                    )
                )
            mode = ["--runner", "wsl", "--wsl-distro", args.wsl_distro]
            mode += ["--dotnet", dotnet, "--runtimeconfig", config]
            runtimes.append(
                Runtime(
                    name,
                    mode,
                    "Microsoft.NETCore.App %s in WSL %s"
                    % (wsl[major], args.wsl_distro),
                )
            )
    return runtimes


# ---------------------------------------------------------------- runs


def run_probe(args, runtime, exe, transcript):
    """Run the probe on runtime; (status line, msil-run.py's output)."""
    cmd = [sys.executable, os.path.join(HERE, "msil-run.py"), *runtime.mode]
    cmd += ["--timeout", "%g" % args.timeout, "--mem-limit-mb", str(args.mem_limit_mb)]
    cmd += ["--show-stderr", "--out", transcript, "--", exe]
    rc, out = run(cmd, timeout=args.timeout + 300)
    if rc == 2 or not os.path.isfile(transcript):
        raise UsageError(
            "msil-run.py could not run the probe on %s (exit status %s):\n%s"
            % (runtime.name, msil_run.describe_exit(rc), out)
        )
    with open(transcript, encoding="utf-8") as f:
        lines = f.read().rstrip("\n").split("\n")
    return lines[-1], out


def check_runtime(args, runtime, entries):
    """Run the probe on runtime until every entry left resolves. Returns
    {index: (exception type, message)} of the failing entries, and a
    failure text when the probe failed in a way no entry explains (else
    None)."""
    rdir = os.path.join(args.out, runtime.name)
    os.makedirs(rdir, exist_ok=True)
    left = list(enumerate(entries, 1))
    failed = {}
    for n in range(1, len(entries) + 2):
        if not left:
            return failed, None
        il = os.path.join(rdir, "probe-%d.il" % n)
        exe = os.path.join(rdir, "probe-%d.exe" % n)
        assemble(args, probe_text(left), il, exe)
        status, out = run_probe(
            args, runtime, exe, os.path.join(rdir, "probe-%d.out" % n)
        )
        if args.verbose:
            say("%s: run %d (%d entries): %s" % (runtime.name, n, len(left), status))
        if status == PASS_STATUS:
            return failed, None
        m = FAILED_RE.search(out)
        index = int(m.group("index")) if m else None
        if index is None or index not in dict(left):
            return failed, "the probe %s ended with %s:\n%s" % (exe, status, out)
        inner = INNER_RE.search(out, m.end())
        failed[index] = (
            (inner.group("type"), inner.group("message").strip())
            if inner
            else ("an exception", "see %s" % exe)
        )
        left = [(i, e) for i, e in left if i != index]
    return failed, "the probe kept failing (internal error)"


def check(args, entries, runtimes, prefix="FAIL: ", stream=sys.stderr):
    """Check every runtime; returns the number of failures (entries and
    probes). Each failing entry is reported on stream after prefix (the
    self-test reports its expected failure as a finding)."""
    failures = 0
    for runtime in runtimes:
        failed, broken = check_runtime(args, runtime, entries)
        for index in sorted(failed):
            exc, message = failed[index]
            say(
                "%s%s: entry %d '%s': %s: %s"
                % (
                    prefix,
                    runtime.name,
                    index,
                    entry_text(entries[index - 1]),
                    exc,
                    message,
                ),
                stream,
            )
        if broken:
            say("FAIL: %s: %s" % (runtime.name, broken), sys.stderr)
        failures += len(failed) + (1 if broken else 0)
        if broken:
            result = "the probe failed (see above)"
        else:
            result = "%d of %d entries resolve" % (
                len(entries) - len(failed),
                len(entries),
            )
        if failed:
            result += "; failing: " + ", ".join(
                "entry %d (%s)" % (i, failed[i][0]) for i in sorted(failed)
            )
        say("%s (%s): %s" % (runtime.name, runtime.describe, result))
        runtime.failed, runtime.broken = failed, broken
    return failures


def self_test(args, runtimes):
    """The --self-test of the module docstring; returns the exit status."""
    problems = []
    for bad, why in (
        ("call [mscorlib]System.Object\n", "an unknown kind"),
        ("type System.Object\n", "no [mscorlib] scope"),
        ("\n\n", "no entry"),
    ):
        try:
            parse_table(bad, "<self-test table>")
            problems.append("a table with %s was accepted" % why)
        except UsageError:
            pass
    entries = parse_table(
        "\n".join(PHASE1_SIX[:1] + (MATHF_SQRT,) + PHASE1_SIX[1:]), "<self-test>"
    )
    mathf = 2
    args.out = os.path.join(args.out, "self-test")
    os.makedirs(args.out, exist_ok=True)
    check(args, entries, runtimes, "self-test: failure: ", sys.stdout)
    for r in runtimes:
        if r.broken:
            problems.append("%s: %s" % (r.name, r.broken))
            continue
        six = [i for i in r.failed if i != mathf]
        if six:
            problems.append(
                "%s: Phase 1 entities failed: %s"
                % (r.name, ", ".join("entry %d" % i for i in six))
            )
        if r.name == "netfx":
            exc = r.failed.get(mathf)
            if not exc:
                problems.append("netfx: MathF::Sqrt resolved, it must fail")
            elif exc[0] != "System.TypeLoadException" or "MathF" not in exc[1]:
                problems.append(
                    "netfx: MathF::Sqrt failed with %s: %s, not a TypeLoadException "
                    "that names MathF" % exc
                )
        else:
            say(
                "self-test: %s: MathF::Sqrt %s (reported, not required)"
                % (r.name, "fails" if mathf in r.failed else "resolves")
            )
    if not any(r.name == "netfx" for r in runtimes):
        say(
            "self-test: note: MathF::Sqrt's failure is checked on netfx, which "
            "--runtimes does not name"
        )
    for p in problems:
        say("self-test: FAIL: %s" % p, sys.stderr)
    if problems:
        return 1
    say(
        "self-test passed: the six Phase 1 entities resolve on %s%s"
        % (
            ", ".join(r.name for r in runtimes),
            "; MathF::Sqrt fails on netfx with System.TypeLoadException"
            if any(r.name == "netfx" for r in runtimes)
            else "",
        )
    )
    return 0


# ---------------------------------------------------------------- main


def default_ilasm():
    if IS_WINDOWS:
        windir = (
            os.environ.get("WINDIR") or os.environ.get("SystemRoot") or r"C:\Windows"
        )
        framework = os.path.join(
            windir, "Microsoft.NET", "Framework64", "v4.0.30319", "ilasm.exe"
        )
        if os.path.isfile(framework):
            return framework
    return os.environ.get("MSIL_ILASM") or shutil.which("ilasm")


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="check-mscorlib-safe.py",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    p.add_argument("--msil-ld", help="msil-ld, for --print-bcl-table")
    p.add_argument("--out", required=True, help="directory for the probes and logs")
    p.add_argument("--table", help="the table as --print-bcl-table prints it")
    p.add_argument(
        "--ilasm",
        help="ilasm (default: the .NET Framework's on Windows, else $MSIL_ILASM "
        "or ilasm on PATH)",
    )
    p.add_argument(
        "--runtimes",
        default=DEFAULT_RUNTIMES,
        help="comma list of netfx, net<N> and wsl-net<N> (default %s)"
        % DEFAULT_RUNTIMES,
    )
    p.add_argument("--dotnet", help="the host's dotnet (default: PATH, DOTNET_ROOT)")
    p.add_argument("--wsl-distro", default="Ubuntu", help="WSL distribution (Ubuntu)")
    p.add_argument(
        "--wsl-dotnet",
        default="private",
        help="the dotnet host inside WSL: private ($HOME/.dotnet-msil/dotnet) or "
        "an absolute path",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=msil_run.DEFAULT_TIMEOUT,
        help="seconds per probe run (default %g)" % msil_run.DEFAULT_TIMEOUT,
    )
    p.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap per probe run in MiB (default %d)"
        % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    p.add_argument(
        "--self-test",
        action="store_true",
        help="check the probe: Phase 1's six entities resolve, MathF::Sqrt fails "
        "on the .NET Framework",
    )
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)
    if not (args.msil_ld or args.table or args.self_test):
        p.error("--msil-ld is required (or --table, or --self-test)")
    if args.self_test and args.table:
        p.error("--self-test uses its own table; drop --table")
    if args.timeout <= 0 or args.mem_limit_mb <= 0:
        p.error("--timeout and --mem-limit-mb must be positive")
    args.wsl_cache = None
    return args


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        args.out = os.path.abspath(args.out)
        os.makedirs(args.out, exist_ok=True)
        args.ilasm = args.ilasm or default_ilasm()
        if not args.ilasm or not (
            os.path.isfile(args.ilasm) or shutil.which(args.ilasm)
        ):
            raise UsageError("no ilasm (pass --ilasm PATH)")
        runtimes = make_runtimes(args)
        if args.self_test:
            return self_test(args, runtimes)
        entries, source = read_table(args)
        with open(os.path.join(args.out, "table.txt"), "w", encoding="utf-8") as f:
            f.write("".join(entry_text(e) + "\n" for e in entries))
        say("%d entries from %s" % (len(entries), source))
        failures = check(args, entries, runtimes)
    except UsageError as e:
        say("error: %s" % e, sys.stderr)
        return 2
    except OSError as e:
        say("error: %s" % e, sys.stderr)
        return 2
    if failures:
        say("%d failure(s); see above and %s" % (failures, args.out), sys.stderr)
        return 1
    say(
        "every entry resolves through [mscorlib] on %s"
        % ", ".join(r.name for r in runtimes)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
