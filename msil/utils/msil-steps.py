#!/usr/bin/env python3
# ===-- msil-steps.py - Run the steps of one composite level --------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run the post-build steps of one composite level in one Python process.

    msil-steps.py [--timeout S] [--mem-limit-mb N]
                  --step=NAME CMD [ARG...] [--step=NAME CMD [ARG...]]...

Each level of lit's composites (%{msil_c}, %{msil_ll}, %{msil_prog}, their
per-level forms and %{msil_prog_loud_<L>}; plan 5.17.10) is '<build> &&
%msil_steps %{msil_limits} --step=verify %msil_verify ... --step=run
%msil_exec ...': every --step=NAME starts a step, and the words up to the
next --step= are its command line, the expansion of the atom (%msil_verify,
%msil_jitall, %msil_exec, %ilasm, %msil_equiv, %msil_roundtrip,
%msil_prog_build, FileCheck, ...) that an explicit RUN line runs. The steps
run in order. Before each one msil-steps.py prints

    msil-steps: NAME: <equivalent command line>

on stdout, every word quoted as lit quotes the command lines of its log
('# executed command: ...'), so the line runs by itself; then the step's own
output follows. The first step that fails ends the level: msil-steps.py
prints 'msil-steps: NAME failed (exit status N)' on stderr and exits with
that step's exit status N.

A step whose command is this Python running msil-run.py, msil-jitall.py,
msil-verify.py, msil-prog.py or msil-equiv.py from this directory runs as a
function of the imported script (its main(argv), under the script's own name),
so its output, its messages and its exit status are the command's, without
starting another Python. Every other step (FileCheck, a compiler, a stand-in
of the harness self-tests) runs as a child process under the memory cap
--mem-limit-mb and ten times --timeout (the factor of the build steps,
msil-prog.py --timeout-factor 10; defaults 60 s and 4096 MiB, lit passes
%{msil_limits}), its output and exit status passed through; one that runs
out of time prints 'msil-steps: <command> did not finish within <T> s' and
fails with status 1.

Consecutive steps that run in WSL (msil-run.py without --tool, and
msil-jitall.py, both with --runner wsl) go through msil-run.py's --steps
machinery: one wsl.exe for all of them instead of one per step (plan
5.17.10), every step reported in its turn as its own run would be.

A run that msil-run.py skips (--skip-netfx; an --expected-rule that names the
run's OS) prints its skip line and passes. A later step that reads that
run's --stderr-out file (FileCheck --input-file, as in %{msil_prog_loud_<L>})
is then skipped too:

    msil-steps: NAME: skipped: <file> is the --stderr-out of the skipped step <S>

lit expands the words before msil-steps.py starts: substitutions, %if,
quotes, and wildcards, against the files that exist at that time.

Exit status: 0 when every step passes, else the exit status of the first
failing step; 2 on a usage error of msil-steps.py itself.
"""

import importlib.util
import os
import re
import shlex
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
IS_WINDOWS = os.name == "nt"
# The scripts that run as functions, and those of them that may run in WSL
# (msil-run.py --steps).
IN_PROCESS = (
    "msil-run.py",
    "msil-jitall.py",
    "msil-verify.py",
    "msil-prog.py",
    "msil-equiv.py",
)
WSL_SCRIPTS = ("msil-run.py", "msil-jitall.py")
# Child steps get the build steps' timeout (msil-prog.py --timeout-factor).
CHILD_TIMEOUT_FACTOR = 10
STEP_PREFIX = "--step="


class UsageError(Exception):
    pass


_modules = {}


def load(script):
    """The module of the script in this directory (loaded once, without
    bytecode files)."""
    if script not in _modules:
        saved = sys.dont_write_bytecode  # no __pycache__ in msil/utils
        sys.dont_write_bytecode = True
        try:
            spec = importlib.util.spec_from_file_location(
                "msil_steps_" + re.sub(r"\W", "_", script), os.path.join(HERE, script)
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        finally:
            sys.dont_write_bytecode = saved
        _modules[script] = module
    return _modules[script]


def same_file(a, b):
    return os.path.normcase(os.path.realpath(a)) == os.path.normcase(
        os.path.realpath(b)
    )


def script_of(words):
    """The script of IN_PROCESS that the command line words runs with this
    Python, or None."""
    if len(words) < 2:
        return None
    name = os.path.basename(words[1])
    if name not in IN_PROCESS or not os.path.isfile(words[1]):
        return None
    if not same_file(os.path.dirname(os.path.abspath(words[1])), HERE):
        return None
    if not same_file(words[0], sys.executable):
        return None
    return name


def options_of(argv):
    """The words of argv before the first '--'."""
    return argv[: argv.index("--")] if "--" in argv else argv


def runs_in_wsl(script, argv):
    """Whether the arguments argv of script run a program or jitall in WSL
    (the steps that msil-run.py --steps runs)."""
    if script not in WSL_SCRIPTS:
        return False
    words = options_of(argv)
    wsl = "--runner=wsl" in words or any(
        a == "--runner" and b == "wsl" for a, b in zip(words, words[1:])
    )
    if script == "msil-run.py":
        return (
            wsl
            and "--tool" not in words
            and not any(w.startswith("--steps") for w in words)
        )
    return wsl


def input_file(words):
    """The file of a FileCheck-style --input-file option of words, or None."""
    for i, w in enumerate(words):
        if w == "--input-file" and i + 1 < len(words):
            return words[i + 1]
        if w.startswith("--input-file="):
            return w[len("--input-file=") :]
    return None


def norm(path):
    return os.path.normcase(os.path.abspath(path))


def parse(argv):
    """(timeout, memory cap, [(name, words)]) of msil-steps.py's command line;
    raises UsageError."""
    msil_run = load("msil-run.py")
    timeout, mem_limit_mb = msil_run.DEFAULT_TIMEOUT, msil_run.DEFAULT_MEM_LIMIT_MB
    i = 0
    while i < len(argv) and not argv[i].startswith(STEP_PREFIX):
        option, sep, value = argv[i].partition("=")
        if option not in ("--timeout", "--mem-limit-mb"):
            raise UsageError(
                "unknown option %s (each step starts with --step=NAME)" % argv[i]
            )
        if not sep:
            i += 1
            if i >= len(argv):
                raise UsageError("%s needs a value" % option)
            value = argv[i]
        try:
            number = float(value) if option == "--timeout" else int(value)
        except ValueError:
            number = 0
        if number <= 0:
            raise UsageError("%s %s: give a positive number" % (option, value))
        if option == "--timeout":
            timeout = number
        else:
            mem_limit_mb = number
        i += 1
    steps, names = [], set()
    while i < len(argv):
        name = argv[i][len(STEP_PREFIX) :]
        j = i + 1
        while j < len(argv) and not argv[j].startswith(STEP_PREFIX):
            j += 1
        if not name:
            raise UsageError("--step= needs a name")
        if name in names:
            raise UsageError("two steps are named %s" % name)
        if j == i + 1:
            raise UsageError("step %s has no command" % name)
        names.add(name)
        steps.append((name, argv[i + 1 : j]))
        i = j
    if not steps:
        raise UsageError("no steps (each step starts with --step=NAME)")
    return timeout, mem_limit_mb, steps


def command_line(words):
    """words as lit prints a command line in its log."""
    return " ".join(shlex.quote(w) for w in words)


def say(stream, text):
    load("msil-run.py").emit(stream, text)


def run_child(words, timeout, mem_limit_mb):
    """A step that is not an imported script: a child process under the
    limits, its output and exit status passed through."""
    msil_run = load("msil-run.py")
    limit = timeout * CHILD_TIMEOUT_FACTOR
    try:
        res = msil_run.run_limited(words, None, dict(os.environ), limit, mem_limit_mb)
    except OSError as e:
        say(sys.stderr, "msil-steps: cannot run %s: %s\n" % (words[0], e))
        return 127
    say(sys.stdout, res.stdout)
    say(sys.stderr, res.stderr)
    if res.timed_out:
        say(
            sys.stderr,
            "msil-steps: %s did not finish within %g s\n" % (words[0], limit),
        )
        return 1
    if res.memory_limit:
        say(
            sys.stderr,
            "msil-steps: %s hit the %d MiB memory cap\n" % (words[0], mem_limit_mb),
        )
        return 1
    rc = res.returncode
    if rc < 0:
        return 128 - rc  # POSIX signal, as a shell reports it
    if IS_WINDOWS and rc >= (1 << 31):
        rc -= 1 << 32
    return rc


def run_script(script, argv):
    """A step that runs an imported script's main(argv)."""
    try:
        return load("msil-run.py").call_main(load(script), argv)
    except Exception:
        traceback.print_exc()
        sys.stderr.flush()
        return 1


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    if argv[:1] in (["-h"], ["--help"]):
        sys.stdout.write(__doc__)
        return 0
    try:
        timeout, mem_limit_mb, steps = parse(argv)
    except UsageError as e:
        sys.stderr.write("msil-steps: error: %s\n" % e)
        return 2
    msil_run = load("msil-run.py")
    scripts = [script_of(words) for _, words in steps]
    wsl = [runs_in_wsl(s, words[2:]) for s, (_, words) in zip(scripts, steps)]
    skipped = {}  # the --stderr-out files of skipped runs -> the run's step

    def header(k):
        name, words = steps[k]
        say(sys.stdout, "msil-steps: %s: %s\n" % (name, command_line(words)))

    def plan_skip(k):
        if scripts[k] == "msil-run.py":
            skip = msil_run.planned_skip(steps[k][1][2:])
            if skip and skip[1]:
                skipped[norm(skip[1])] = steps[k][0]

    def failed(k, status):
        sys.stdout.flush()
        say(
            sys.stderr,
            "msil-steps: %s failed (exit status %s)\n" % (steps[k][0], status),
        )
        return status

    i = 0
    while i < len(steps):
        name, words = steps[i]
        if wsl[i]:
            j = i
            while j < len(steps) and wsl[j]:
                j += 1
            if j - i > 1:
                for k in range(i, j):
                    plan_skip(k)
                batch = [
                    {"name": steps[k][0], "script": scripts[k], "argv": steps[k][1][2:]}
                    for k in range(i, j)
                ]
                reported = []

                def batch_header(k, base=i):
                    reported.append(base + k)
                    header(base + k)

                try:
                    status, bad = msil_run.run_steps(
                        batch, header=batch_header, load=load
                    )
                except Exception:
                    traceback.print_exc()
                    sys.stderr.flush()
                    return failed(reported[-1] if reported else i, 1)
                if status != 0:
                    return failed(i + bad, status)
                i = j
                continue
        source = input_file(words)
        if source is not None and norm(source) in skipped:
            say(
                sys.stdout,
                "msil-steps: %s: skipped: %s is the --stderr-out of the skipped "
                "step %s\n" % (name, source, skipped[norm(source)]),
            )
            i += 1
            continue
        header(i)
        plan_skip(i)
        if scripts[i]:
            status = run_script(scripts[i], words[2:])
        else:
            status = run_child(words, timeout, mem_limit_mb)
        if status != 0:
            return failed(i, status)
        i += 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
