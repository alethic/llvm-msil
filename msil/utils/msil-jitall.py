#!/usr/bin/env python3
# ===-- msil-jitall.py - Force-JIT every method of an assembly ------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Force-JIT every method of an assembly under a timeout and a memory cap.

    msil-jitall.py [--jitall DIR] [--dotnet PATH] [--runtimeconfig FILE]
                   [--runner {local,wsl}] [--wsl-distro D]
                   [--timeout S] [--mem-limit-mb N] [--env VAR=VALUE]...
                   [--filter TEXT] [--dep FILE...]... [--] prog.dll

Runs 'dotnet exec --runtimeconfig <cfg> <DIR>/jitall.dll prog.dll' from a
private temporary directory that holds a copy of prog.dll, so that assemblies
with the same name never meet (llvm/docs/MSIL/recon/asmprinter.md 0.9), and
a copy of each --dep FILE (plan 5.12.4, M15): the assemblies prog.dll
references, which jitall loads from that directory to JIT the methods that
use them, and the native libraries the module initializer calls. --dep takes
one or more files, may be repeated, and takes every argument up to the next
option, so the lit composites pass '--dep %{msil_deps} --' before prog.dll; a
missing file, or two of one name, is a configuration error. jitall
JIT-compiles every method that has an IL body and calls none of them. Its
output is copied to stdout with LF line endings: one 'JIT FAIL <type>::<method>:
<exception>' line per rejected method, then 'jitall: N ok, M failed'.

Compiling a method of <Module> makes the runtime run <Module>::.cctor, the
module initializer (data segment and C constructors), first, so that code does
run under jitall and calls the target's imports. jitall therefore has to run
where the program would run: a module for a Linux triple whose initializer
calls puts from libc needs --runner wsl on a Windows host. A failing
initializer makes every method of <Module> fail with a
TypeInitializationException, which hides the methods' own JIT errors.

--runner wsl runs this script in WSL ('wsl.exe -d <distro> --exec env
VAR=VALUE... python3 msil-jitall.py ... prog.dll' with the paths translated to
/mnt/<drive>/), so the timeout, the memory cap and the private directory apply
inside Linux, where jitall really runs. --dotnet is then the dotnet of the
distribution (default 'dotnet'); --jitall, --runtimeconfig and prog.dll stay
Windows paths. A failure of wsl.exe itself is a configuration error.

The JIT can run away on bad IL (the .NET 10 JIT grew past 40 GB on a join with
different stack depths, recon/cil-runtime.md e15), so jitall runs under the
wall-clock timeout (--timeout, default 60 s) and the memory cap on committed
memory (--mem-limit-mb, default 4096) of msil-run.py: a Windows Job Object, or
RLIMIT_DATA on POSIX. When a limit is hit, the line '[timeout]' or
'[memory limit]' follows the output.

--jitall defaults to <build>/tools/msil/jitall (<build> is $MSIL_BUILD_DIR,
else <repo>/build), where msil/utils/jitall/build-jitall.py puts the tool.
--runtimeconfig defaults to jitall.runtimeconfig.json there (.NET 8, rolling
forward to a newer major only when 8 is missing); pass another runtime's
runtimeconfig to JIT with that runtime. --env VAR=VALUE (repeatable) sets an
environment variable for jitall, for example DOTNET_TieredCompilation=0 to JIT
with full optimization.

Exit status: 0 if every method JITs, 1 otherwise (including a timeout, the
memory cap, a crash of jitall or a missing summary line), 2 on a usage or
configuration error (including a dotnet host that cannot start jitall at all,
recognized by msil-run.py's host_startup_failure(); an out-of-memory start-up
failure is the memory cap).
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

SUMMARY_RE = re.compile(r"^jitall: (\d+) ok, (\d+) failed$", re.M)
# The line this script writes when it fails a run (exit status 1).
VERDICT_RE = re.compile(r"^msil-jitall: .*: jitall .+$", re.M)


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
ConfigError = msil_run.ConfigError


class Outcome:
    """What a run printed, the exit status of this script and, for a status
    other than 0, why."""

    def __init__(self, out, err, status, reason=None):
        self.out = out
        self.err = err
        self.status = status
        self.reason = reason


def find_dotnet(explicit):
    if explicit:
        if os.path.isfile(explicit):
            return explicit
        return shutil.which(explicit)  # a bare command name such as 'dotnet'
    candidates = [shutil.which("dotnet")]
    if os.environ.get("DOTNET_ROOT"):
        candidates.append(os.path.join(os.environ["DOTNET_ROOT"], "dotnet" + EXE))
    if IS_WINDOWS:
        candidates.append(r"C:\Program Files\dotnet\dotnet.exe")
    else:
        candidates += [
            "/usr/share/dotnet/dotnet",
            "/usr/lib/dotnet/dotnet",
            "/usr/local/share/dotnet/dotnet",
        ]
    candidates.append(os.path.join(os.path.expanduser("~"), ".dotnet", "dotnet" + EXE))
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return None


def describe_status(rc):
    if rc < 0:
        return "was killed by signal %d" % -rc
    return "exited with status %s" % msil_run.describe_exit(rc)


def judge(out, rc):
    """Why a finished jitall run failed, or None if every method JITted."""
    summaries = SUMMARY_RE.findall(out)
    if not summaries:
        return "%s without a summary line" % describe_status(rc)
    failed = int(summaries[-1][1])
    if failed == 0 and rc == 0:
        return None
    if failed == 0:
        return "%s after reporting no failures" % describe_status(rc)
    return "%d method(s) failed to JIT" % failed


def run_local(args, jitall_dll, runtimeconfig):
    dotnet = find_dotnet(args.dotnet)
    if not dotnet:
        raise ConfigError(
            "dotnet host %r not found" % args.dotnet
            if args.dotnet
            else "no dotnet host found (pass --dotnet or set DOTNET_ROOT)"
        )
    env = msil_run.child_env(args.env)
    msil_run.check_deps(args.dep, [])
    private = tempfile.mkdtemp(prefix="msil-jitall-")
    try:
        target = os.path.join(private, os.path.basename(args.dll))
        shutil.copyfile(args.dll, target)
        msil_run.copy_deps(args.dep, [], private, [os.path.basename(args.dll)])
        cmd = [dotnet, "exec", "--runtimeconfig", runtimeconfig, jitall_dll, target]
        if args.filter is not None:
            cmd.append(args.filter)
        try:
            res = msil_run.run_limited(
                cmd, private, env, args.timeout, args.mem_limit_mb
            )
        except OSError as e:
            raise ConfigError("cannot run jitall under limits: %s" % e)
    finally:
        msil_run.remove_tree(private)

    out, err = msil_run.text_of(res.stdout), msil_run.text_of(res.stderr)
    if out and not out.endswith("\n"):
        out += "\n"
    if res.timed_out:
        return Outcome(
            out + "[timeout]\n", err, 1, "did not finish within %g s" % args.timeout
        )
    # A dotnet host or CoreCLR that refused to start jitall ('Failed to create
    # CoreCLR', a missing framework) is a configuration error, as in
    # msil-run.py, unless it ran out of memory under the cap.
    startup = None if SUMMARY_RE.search(out) else msil_run.host_startup_failure(err)
    # Under RLIMIT_DATA a failed allocation is the only sign of the cap, and
    # jitall reports the JIT's OutOfMemoryException in a 'JIT FAIL' line on
    # stdout, so search both streams (msil-run.py searches only stderr).
    if (
        res.memory_limit
        or (startup and msil_run.OOM_RE.search(startup))
        or (not IS_WINDOWS and msil_run.OOM_RE.search(out + err))
    ):
        return Outcome(
            out + "[memory limit]\n",
            err,
            1,
            "hit the %d MiB memory cap" % args.mem_limit_mb,
        )
    if startup:
        return Outcome(
            out,
            err,
            2,
            "the dotnet host could not start jitall (%s): %s"
            % (describe_status(res.returncode), startup),
        )
    reason = judge(out, res.returncode)
    return Outcome(out, err, 1 if reason else 0, reason)


def run_wsl(args, jitall_dir, runtimeconfig):
    """Run this script in WSL, so that jitall, the module initializer and the
    limits all run inside Linux."""
    if not IS_WINDOWS:
        raise ConfigError("--runner wsl needs a Windows host")
    wsl = shutil.which("wsl.exe") or os.path.join(
        os.environ.get("SystemRoot", r"C:\Windows"), "System32", "wsl.exe"
    )
    assignments = ["%s=%s" % kv for kv in msil_run.DOTNET_DEFAULTS]
    assignments += ["%s=%s" % kv for kv in msil_run.parse_env_args(args.env)]
    cmd = (
        [wsl, "-d", args.wsl_distro, "--exec", "env"]
        + assignments
        + [
            "python3",
            msil_run.to_wsl_path(os.path.abspath(__file__)),
            "--jitall",
            msil_run.to_wsl_path(jitall_dir),
            "--dotnet",
            args.dotnet or "dotnet",
            "--runtimeconfig",
            msil_run.to_wsl_path(runtimeconfig),
            "--timeout",
            repr(args.timeout),
            "--mem-limit-mb",
            str(args.mem_limit_mb),
        ]
    )
    if args.filter is not None:
        cmd.append("--filter=" + args.filter)
    # The copy inside WSL copies them next to its private copy of the dll.
    msil_run.check_deps(args.dep, [])
    for dep in args.dep:
        cmd += ["--dep", msil_run.to_wsl_path(dep)]
    cmd += ["--", msil_run.to_wsl_path(args.dll)]
    env = dict(os.environ)
    msil_run.set_env_var(env, "WSL_UTF8", "1")  # wsl.exe's own messages, not UTF-16
    limit = args.timeout + msil_run.WSL_GRACE
    try:
        res = msil_run.run_limited(cmd, None, env, limit, args.mem_limit_mb)
    except OSError as e:
        raise ConfigError("cannot run %s: %s" % (wsl, e))

    out, err = msil_run.text_of(res.stdout), msil_run.text_of(res.stderr)
    if out and not out.endswith("\n"):
        out += "\n"
    if res.timed_out:
        return Outcome(
            out + "[timeout]\n", err, 1, "did not finish within %g s (wsl.exe)" % limit
        )
    # The copy in WSL judged the run: a summary line with status 0, or its own
    # verdict line on stderr with status 1. Anything else (status 2, or a
    # status of wsl.exe) means it did not get that far.
    rc = res.returncode
    if (rc == 0 and SUMMARY_RE.search(out)) or (rc == 1 and VERDICT_RE.search(err)):
        return Outcome(out, err, rc)
    return Outcome(
        out, err, 2, "the WSL runner failed (wsl.exe %s)" % describe_status(rc)
    )


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument(
        "--jitall",
        metavar="DIR",
        help="directory with jitall.dll (default <build>/tools/msil/jitall)",
    )
    p.add_argument(
        "--dotnet",
        help="dotnet host (default: from PATH, DOTNET_ROOT or the "
        "standard install locations; with --runner wsl: inside WSL, "
        "default 'dotnet')",
    )
    p.add_argument(
        "--runtimeconfig",
        metavar="FILE",
        help="runtimeconfig.json for jitall (default <DIR>/jitall.runtimeconfig.json)",
    )
    p.add_argument(
        "--runner",
        choices=("local", "wsl"),
        default="local",
        help="where to run jitall (default: local)",
    )
    p.add_argument(
        "--wsl-distro",
        default="Ubuntu",
        help="WSL distribution for --runner wsl (default: Ubuntu)",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=msil_run.DEFAULT_TIMEOUT,
        help="wall-clock limit in seconds (default %g)" % msil_run.DEFAULT_TIMEOUT,
    )
    p.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap in MiB (default %d)" % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    p.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="VAR=VALUE",
        help="set an environment variable for jitall (repeatable)",
    )
    p.add_argument(
        "--filter", metavar="TEXT", help="only JIT methods whose name contains TEXT"
    )
    p.add_argument(
        "--dep",
        nargs="*",
        action="extend",
        default=[],
        metavar="FILE",
        help="copy these files next to the assembly (repeatable; takes every "
        "argument up to the next option, so end with '--' before the assembly)",
    )
    p.add_argument("dll", help="assembly to check")
    args = p.parse_args()
    if args.timeout <= 0 or args.mem_limit_mb <= 0:
        p.error("--timeout and --mem-limit-mb must be positive")

    jitall_dir = args.jitall or os.path.join(
        os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build"),
        "tools",
        "msil",
        "jitall",
    )
    jitall_dll = os.path.abspath(os.path.join(jitall_dir, "jitall.dll"))
    runtimeconfig = os.path.abspath(
        args.runtimeconfig or os.path.join(jitall_dir, "jitall.runtimeconfig.json")
    )
    try:
        if not os.path.isfile(jitall_dll):
            raise ConfigError(
                "%s not found; build it with 'python %s'"
                % (
                    jitall_dll,
                    os.path.join(REPO, "msil", "utils", "jitall", "build-jitall.py"),
                )
            )
        if not os.path.isfile(runtimeconfig):
            raise ConfigError("%s not found" % runtimeconfig)
        if not os.path.isfile(args.dll):
            raise ConfigError("%s not found" % args.dll)
        if args.runner == "wsl":
            res = run_wsl(args, os.path.dirname(jitall_dll), runtimeconfig)
        else:
            res = run_local(args, jitall_dll, runtimeconfig)
    except ConfigError as e:
        sys.stderr.write("msil-jitall: error: %s\n" % e)
        return 2

    msil_run.emit(sys.stdout, res.out)
    if res.err:
        msil_run.emit(sys.stderr, res.err if res.err.endswith("\n") else res.err + "\n")
    if res.status == 2:
        sys.stderr.write("msil-jitall: error: %s\n" % res.reason)
    elif res.reason:
        sys.stderr.write("msil-jitall: %s: jitall %s\n" % (args.dll, res.reason))
    return res.status


if __name__ == "__main__":
    sys.exit(main())
