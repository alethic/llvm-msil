#!/usr/bin/env python3
"""Run a program built by the MSIL test suite and normalize what it did.

The normalized transcript is the program's stdout with line endings folded to
LF, followed by one status line:

    [exit N]                          normal termination with exit code N
    [unhandled exception: T]          the CLR reported an unhandled exception T
    [signal N]                        killed by a signal (POSIX)
    [timeout]                         did not finish within --timeout seconds

The same transcript format is produced for the three ways a test runs a
program, so they can all be compared with one expected file:

    msil-run.py --dotnet D --runtimeconfig C -- prog.dll [args]   (modern .NET)
    msil-run.py --netfx -- prog.exe [args]                        (.NET Framework)
    msil-run.py --native -- prog.exe [args]                       (native oracle)

With --expect STEM the transcript is compared with STEM.<os>.expected if that
file exists, else STEM.expected. <os> (windows, linux or darwin) comes from
--expect-os, which lit sets from the target triple. A mismatch
prints a unified diff and exits 1. With --out FILE the transcript is written
there as well.
"""

import argparse
import difflib
import os
import platform
import re
import subprocess
import sys

IS_WINDOWS = platform.system() == "Windows"

# CoreCLR prints "Unhandled exception. System.Foo: ..." and .NET Framework
# prints "Unhandled Exception: System.Foo: ...".
UNHANDLED_RE = re.compile(r"Unhandled [Ee]xception[.:]\s*([\w.`+]+)")


def suppress_crash_dialogs():
    """Keep WER and 'program has stopped working' dialogs from hanging CI."""
    if not IS_WINDOWS:
        return
    import ctypes

    SEM_FAILCRITICALERRORS = 0x0001
    SEM_NOGPFAULTERRORBOX = 0x0002
    SEM_NOOPENFILEERRORBOX = 0x8000
    ctypes.windll.kernel32.SetErrorMode(
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX
    )


def build_command(args):
    prog = os.path.abspath(args.program)
    if args.native or args.netfx:
        if args.netfx and not IS_WINDOWS:
            sys.exit("msil-run: --netfx is only available on Windows")
        return [prog] + args.args
    if not args.dotnet or not args.runtimeconfig:
        sys.exit("msil-run: --dotnet and --runtimeconfig are required for .dll programs")
    return [args.dotnet, "exec", "--runtimeconfig", args.runtimeconfig, prog] + args.args


def child_env(args):
    env = dict(os.environ)
    env.setdefault("DOTNET_CLI_TELEMETRY_OPTOUT", "1")
    env.setdefault("DOTNET_NOLOGO", "1")
    env.setdefault("DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1")
    env.setdefault("DOTNET_DbgEnableMiniDump", "0")
    for kv in args.env:
        key, sep, value = kv.partition("=")
        if not sep:
            sys.exit("msil-run: --env expects KEY=VALUE, got %r" % kv)
        env[key] = value
    return env


def status_line(proc_rc, stderr_text):
    m = UNHANDLED_RE.search(stderr_text)
    if m:
        return "[unhandled exception: %s]" % m.group(1)
    if proc_rc < 0:  # POSIX: killed by a signal
        return "[signal %d]" % -proc_rc
    if IS_WINDOWS and proc_rc >= 0x80000000:
        # NTSTATUS-style crash codes (access violation, stack overflow, ...).
        return "[crash 0x%08X]" % proc_rc
    return "[exit %d]" % proc_rc


def transcript(stdout_bytes, status):
    text = stdout_bytes.decode("utf-8", errors="backslashreplace")
    text = text.replace("\r\n", "\n")
    if text and not text.endswith("\n"):
        text += "\n[no newline at end of output]\n"
    return text + status + "\n"


def host_os():
    return {"Windows": "windows", "Linux": "linux", "Darwin": "darwin"}.get(
        platform.system(), platform.system().lower()
    )


def expected_file(stem, osname):
    for cand in (stem + "." + osname + ".expected", stem + ".expected"):
        if os.path.isfile(cand):
            return cand
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--native", action="store_true", help="run a native executable")
    mode.add_argument("--netfx", action="store_true", help="run a .exe on .NET Framework")
    p.add_argument("--dotnet", help="path to the dotnet host")
    p.add_argument("--runtimeconfig", help="runtimeconfig.json for 'dotnet exec'")
    p.add_argument("--env", action="append", default=[], metavar="KEY=VALUE")
    p.add_argument("--stdin", help="file to feed on stdin (default: empty)")
    p.add_argument("--timeout", type=float, default=120.0)
    p.add_argument("--out", help="also write the transcript to this file")
    p.add_argument("--expect", metavar="STEM", help="compare with STEM[.<os>].expected")
    p.add_argument("--expect-os", default=host_os(),
                   help="<os> for STEM.<os>.expected: the OS of the target triple "
                        "(it decides sizeof(long) etc.), default: the host")
    p.add_argument("--show-stderr", action="store_true", help="echo the program's stderr")
    p.add_argument("program")
    p.add_argument("args", nargs="*")
    args = p.parse_args()

    suppress_crash_dialogs()
    cmd = build_command(args)
    stdin = open(args.stdin, "rb") if args.stdin else subprocess.DEVNULL
    try:
        proc = subprocess.run(
            cmd,
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_env(args),
            timeout=args.timeout,
        )
        stderr_text = proc.stderr.decode("utf-8", errors="backslashreplace")
        out = transcript(proc.stdout, status_line(proc.returncode, stderr_text))
    except subprocess.TimeoutExpired as e:
        stderr_text = (e.stderr or b"").decode("utf-8", errors="backslashreplace")
        out = transcript(e.stdout or b"", "[timeout]")
    finally:
        if stdin is not subprocess.DEVNULL:
            stdin.close()

    if args.show_stderr and stderr_text:
        sys.stderr.write(stderr_text)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
    if not args.expect:
        sys.stdout.write(out)
        return 0

    exp_path = expected_file(args.expect, args.expect_os)
    if exp_path is None:
        sys.stderr.write("msil-run: no expected file for %s\n" % args.expect)
        sys.stderr.write("---- actual ----\n" + out)
        return 1
    with open(exp_path, "r", encoding="utf-8", newline="") as f:
        expected = f.read().replace("\r\n", "\n")
    if expected == out:
        return 0
    sys.stderr.write("msil-run: output of %s does not match %s\n" % (args.program, exp_path))
    sys.stderr.writelines(
        difflib.unified_diff(
            expected.splitlines(True), out.splitlines(True), exp_path, "actual"
        )
    )
    if stderr_text:
        sys.stderr.write("---- program stderr ----\n" + stderr_text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
