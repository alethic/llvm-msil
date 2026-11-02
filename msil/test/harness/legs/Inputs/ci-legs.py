#!/usr/bin/env python3
# ===-- ci-legs.py - Run the CI leg scripts against a stand-in build -------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run msil/utils/ci/lit-legs.ps1 or lit-legs.sh against a stand-in build
directory, for ci-legs-corpus.test, ci-legs-corpus-sh.test and M24's leg
tests (describe*.test, exit-tag*.test, timeout-retry*.test), and give
calibrate-label.test a build whose lit plays a scenario.

    ci-legs.py setup BUILD
        Make BUILD/bin/llvm-lit.py (what msil-lit.ps1 runs) and
        BUILD/bin/llvm-lit (what lit-legs.sh runs): a stand-in lit that
        appends its arguments ('arg: <a>') and the lines of every '@<file>'
        response file ('rsp: <line>') to BUILD/lit-calls.log. Without
        BUILD/lit-scenario.json it prints lit's summary line
        '  Passed: 1 (100.00%)' and exits 0.
    ci-legs.py scenario BUILD [--no-xunit] NAME=OUTCOME[,OUTCOME...]...
        Write BUILD/lit-scenario.json (and forget the runs counted so far).
        The stand-in lit then reports the tests NAME (full lit names,
        'MSIL :: <path below msil/test>') of every run whose paths hold them,
        as lit -sv does: a 'PASS: '/'FAIL: ' line each, lit's summary lines,
        an xunit file in lit's format at --xunit-xml-output (none with
        --no-xunit), and exit status 1 when one of them failed. The k-th run
        of a test (counted across runs) has its k-th OUTCOME, or its last:
        'pass'; 'timeout', a failure whose last command printed msil-run.py's
        timeout message 'msil-run: dotnet did not finish within 60 s' (a
        marker of plan 5.17.9); 'other', a failure without a marker;
        'unsupported', a test that lit reports UNSUPPORTED (no line, as
        under -sv; xunit <skipped message="Unsupported configuration"/>);
        'skip', a test that lit did not run (no line; lit's 'Skipped', xunit
        <skipped message="Skipped"/>); 'crash', a skipped test whose worker
        process died: the run then ends as lit's does after an error in tests,
        with lit's error '<prog>: <path>:283: error: a worker process crashed:
        A process in the process pool was terminated abruptly while the future
        was running or pending.' on stderr after the tests' lines, '1 error(s)
        in tests' on stderr after the summary, and exit status 2. An OUTCOME
        may end in '@SECONDS', the test's time in the xunit file (default 0).
        A test that --filter does not match, or that --filter-out matches, is
        excluded (lit's 'Excluded', xunit <skipped>) and not counted.
    ci-legs.py ps1 BUILD [VAR=VALUE...] -- [ARG...]
        pwsh -NoProfile -File lit-legs.ps1 -BuildDir BUILD ARG...
    ci-legs.py python DIR
        Make DIR/python.cmd (Windows) or DIR/python (elsewhere): a Python 3
        for MSIL_PYTHON (p3-tests-stop.test) that runs this Python on every
        command line, except that when its first argument is a file named
        check-matrix.py and $MSIL_FAKE_P3 is set, it plays that checker
        instead: 'fail' writes 'check-matrix: error: planted failure' to
        stderr and 'one stdout line' to stdout and exits 1; 'bad-path'
        prints the line 'llvm/test/CodeGen/MSIL/call.ll' and exits 0;
        'dot-path' prints 'msil/test/../llvm/test/CodeGen/MSIL/call.ll' and
        exits 0; 'silent' prints nothing and exits 3. It appends each
        command line it receives ('argv: <a> <b> ...') to DIR/calls.log.
    ci-legs.py error-message LOG
        Print 'error-message: <text>' for each error that pwsh reported in
        LOG (the output of 'ci-legs.py ps1') in its concise view
        ('Exception: <script>:<line>', 'Line |', the source line, the '~'
        marker and the message, wrapped at the console width over lines
        that start with '     | '): the message lines joined with one space,
        so a check can match the whole text. Exits 1 when LOG holds none.
    ci-legs.py sh [--distro D] BUILD [VAR=VALUE...] -- [ARG...]
        bash lit-legs.sh ARG... BUILD: inside the WSL distribution D (default
        Ubuntu) on Windows, with every absolute Windows path among BUILD, the
        VALUEs and the ARGs written as /mnt/<drive>/...; directly elsewhere.

The script runs with MSIL_CORPUS_SKIP, MSIL_CORPUS_CACHE, MSIL_CI_RESULTS and
GITHUB_STEP_SUMMARY removed from its environment and then the VAR=VALUE pairs
set, so neither a gate's results file nor a CI step summary gets these rows.
Its stdout and stderr are printed together, without colour escapes and CRs;
ci-legs.py exits with its exit status.
"""

import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 5))
CI = os.path.join(REPO, "msil", "utils", "ci")
CLEARED = ("MSIL_CORPUS_SKIP", "MSIL_CORPUS_CACHE", "MSIL_CI_RESULTS", "GITHUB_STEP_SUMMARY")

STAND_IN_LIT = r"""import json, os, re, sys
build = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = []
with open(os.path.join(build, "lit-calls.log"), "a", encoding="utf-8", newline="\n") as f:
    for a in sys.argv[1:]:
        f.write("arg: %s\n" % a)
        args.append(a)
        if a.startswith("@"):
            with open(a[1:], encoding="utf-8-sig") as r:
                for line in r.read().splitlines():
                    f.write("rsp: %s\n" % line)
                    args.append(line)
scenario_file = os.path.join(build, "lit-scenario.json")
if not os.path.exists(scenario_file):
    print("Total Discovered Tests: 1")
    print("  Passed: 1 (100.00%)")
    sys.exit(0)
with open(scenario_file, encoding="utf-8") as f:
    scenario = json.load(f)
state_file = os.path.join(build, "lit-scenario-state.json")
state = {}
if os.path.exists(state_file):
    with open(state_file, encoding="utf-8") as f:
        state = json.load(f)


def option(name):
    values = [a[len(name) + 1 :] for a in args if a.startswith(name + "=")]
    return values[-1] if values else None


def tail(path):
    path = os.path.normpath(path).replace(chr(92), "/").rstrip("/")
    i = path.find("msil/test")
    return path[i:] if i >= 0 else None


xunit, keep, drop = option("--xunit-xml-output"), option("--filter"), option("--filter-out")
tails = [t for t in (tail(a) for a in args if not a.startswith(("-", "@"))) if t]
results = []
for name, outcomes in scenario["tests"]:
    rel = "msil/test/" + name.split(" :: ", 1)[1]
    if not any(rel == t or rel.startswith(t + "/") for t in tails):
        continue
    if (keep and not re.search(keep, name)) or (drop and re.search(drop, name)):
        results.append((name, "excluded", 0.0))
        continue
    runs = state.get(name, 0)
    state[name] = runs + 1
    outcome, _, seconds = outcomes[min(runs, len(outcomes) - 1)].partition("@")
    results.append((name, outcome, float(seconds or 0)))
with open(state_file, "w", encoding="utf-8") as f:
    json.dump(state, f)
MESSAGE = {
    "timeout": "msil-run: dotnet did not finish within 60 s",
    "other": "msil-run: output of prog.exe does not match prog.expected",
}


def failure(outcome):
    return (
        "Exit Code: 1\n\nCommand Output (stdout):\n--\n# RUN: at line 1\n"
        "msil-run.py prog.exe\n# executed command: msil-run.py prog.exe\n"
        "# .---command stderr------------\n# | %s\n"
        "# `-----------------------------\n"
        "# error: command failed with exit status: 1\n\n--\n" % MESSAGE[outcome]
    )


ran = [r for r in results if r[1] not in ("excluded", "unsupported", "skip", "crash")]
excluded = len([r for r in results if r[1] == "excluded"])
unsupported = len([r for r in results if r[1] == "unsupported"])
skipped = len(results) - len(ran) - excluded - unsupported
crashed = any(r[1] == "crash" for r in results)
for i, (name, outcome, _) in enumerate(ran, 1):
    print(
        "%s: %s (%d of %d)"
        % ("PASS" if outcome == "pass" else "FAIL", name, i, len(results) - excluded)
    )
    if outcome != "pass":
        print("******************** TEST '%s' FAILED ********************" % name)
        sys.stdout.write(failure(outcome))
        print("********************")
failed = len([r for r in ran if r[1] != "pass"])
# A 'crash' test: lit's process pool lost a worker (lit_config.error in lit's main.py).
if crashed:
    sys.stdout.flush()
    sys.stderr.write(
        "%s: %s:283: error: a worker process crashed: A process in the process pool "
        "was terminated abruptly while the future was running or pending.\n"
        % (os.path.basename(sys.argv[0]), os.path.join(build, "lit", "main.py"))
    )
    sys.stderr.flush()
if xunit and not scenario.get("no_xunit"):
    with open(xunit, "w", encoding="utf-8", newline="\n") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<testsuites time="0.01">\n')
        f.write(
            '<testsuite name="MSIL" tests="%d" failures="%d" skipped="%d" time="0.00">\n'
            % (len(results), failed, len(results) - len(ran))
        )
        for name, outcome, seconds in sorted(results):
            suite, rel = name.split(" :: ", 1)
            folder, _, base = rel.rpartition("/")
            f.write(
                '<testcase classname="%s.%s" name="%s" time="%.2f"'
                % (suite, folder.replace(".", "_") or suite, base, seconds)
            )
            if outcome == "pass":
                f.write("/>\n")
            elif outcome == "excluded":
                f.write(
                    '>\n  <skipped message="Test not selected (--filter, --max-tests)"/>\n'
                    "</testcase>\n"
                )
            elif outcome == "unsupported":
                f.write('>\n  <skipped message="Unsupported configuration"/>\n</testcase>\n')
            elif outcome in ("skip", "crash"):
                f.write('>\n  <skipped message="Skipped"/>\n</testcase>\n')
            else:
                f.write(">\n  <failure><![CDATA[%s]]></failure>\n</testcase>\n" % failure(outcome))
        f.write("</testsuite>\n</testsuites>\n")
counts = [
    (label, n)
    for label, n in (
        ("Excluded", excluded),
        ("Skipped", skipped),
        ("Unsupported", unsupported),
        ("Passed", len(ran) - failed),
        ("Failed", failed),
    )
    if n
]
print("Total Discovered Tests: %d" % len(results))
width = max([len(label) for label, n in counts] or [0])
for label, n in counts:
    print("  %s: %d (%.2f%%)" % (label.ljust(width), n, 100.0 * n / len(results)))
if crashed:
    sys.stdout.flush()
    sys.stderr.write("\n1 error(s) in tests\n")
    sys.exit(2)
sys.exit(1 if failed else 0)
"""

# The stand-in Python of 'ci-legs.py python' (MSIL_PYTHON for lit-legs.ps1).
STAND_IN_PYTHON = r"""import os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
args = sys.argv[1:]
with open(os.path.join(here, "calls.log"), "a", encoding="utf-8", newline="\n") as f:
    f.write("argv: %s\n" % " ".join(args))
mode = os.environ.get("MSIL_FAKE_P3", "")
if mode and args and os.path.basename(args[0]) == "check-matrix.py":
    if mode == "fail":
        sys.stderr.write("check-matrix: error: planted failure\n")
        print("one stdout line")
        sys.exit(1)
    if mode == "bad-path":
        print("llvm/test/CodeGen/MSIL/call.ll")
        sys.exit(0)
    if mode == "dot-path":
        print("msil/test/../llvm/test/CodeGen/MSIL/call.ll")
        sys.exit(0)
    if mode == "silent":
        sys.exit(3)
    sys.stderr.write("stand-in python: unknown MSIL_FAKE_P3 %r\n" % mode)
    sys.exit(2)
sys.exit(subprocess.call([sys.executable] + args))
"""


def python_stand_in(directory):
    directory = os.path.abspath(directory)
    os.makedirs(directory, exist_ok=True)
    script = os.path.join(directory, "stand-in-python.py")
    with open(script, "w", encoding="utf-8", newline="\n") as f:
        f.write(STAND_IN_PYTHON)
    if os.name == "nt":
        path = os.path.join(directory, "python.cmd")
        with open(path, "w", encoding="utf-8", newline="\r\n") as f:
            f.write(
                '@echo off\n"%s" "%s" %%*\nexit /b %%ERRORLEVEL%%\n'
                % (sys.executable, script)
            )
    else:
        path = os.path.join(directory, "python")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write('#!/bin/sh\nexec "%s" "%s" "$@"\n' % (sys.executable, script))
        os.chmod(path, 0o755)
    print(path.replace("\\", "/"))
    return 0


def error_messages(log):
    """The messages of pwsh's concise error view in log (see 'error-message')."""
    with open(log, encoding="utf-8", errors="replace") as f:
        lines = f.read().replace("\r", "").split("\n")
    found = []
    i = 0
    while i < len(lines):
        if not re.match(r"^\s*\|\s*~+\s*$", lines[i]):
            i += 1
            continue
        i += 1
        parts = []
        while i < len(lines):
            m = re.match(r"^\s+\| ?(.*)$", lines[i])
            if not m:
                break
            parts.append(m.group(1).strip())
            i += 1
        if parts:
            found.append(" ".join(p for p in parts if p))
    for text in found:
        print("error-message: %s" % text)
    return 0 if found else 1


ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
DRIVE_RE = re.compile(r"^([A-Za-z]):[\\/](.*)$")


def usage(msg):
    sys.stderr.write("ci-legs.py: %s\n" % msg)
    return 2


def wsl_path(text):
    """X:\\a\\b -> /mnt/x/a/b; anything else unchanged."""
    m = DRIVE_RE.match(text)
    if not m:
        return text
    return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2).replace("\\", "/"))


def split(argv):
    """(VAR=VALUE pairs, ARGs) of '[VAR=VALUE...] -- [ARG...]'."""
    if "--" not in argv:
        raise ValueError("missing '--' before the script's arguments")
    i = argv.index("--")
    pairs = []
    for p in argv[:i]:
        if "=" not in p or not re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", p):
            raise ValueError("not VAR=VALUE: %r" % p)
        pairs.append(tuple(p.split("=", 1)))
    return pairs, argv[i + 1 :]


def run(cmd, env):
    res = subprocess.run(
        cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    text = ANSI_RE.sub("", res.stdout.decode("utf-8", "replace")).replace("\r", "")
    sys.stdout.write(text)
    sys.stdout.flush()
    return res.returncode


def clean_env(pairs):
    env = {k: v for k, v in os.environ.items() if k.upper() not in CLEARED}
    env.update(pairs)
    # pwsh's own colour (PowerShell 7.2 and later honour NO_COLOR).
    env["NO_COLOR"] = "1"
    return env


OUTCOMES = ("pass", "timeout", "other", "unsupported", "skip", "crash")


def outcome_ok(text):
    outcome, at, seconds = text.partition("@")
    if outcome not in OUTCOMES:
        return False
    try:
        return not at or float(seconds) >= 0
    except ValueError:
        return False


def scenario(build, argv):
    no_xunit = argv[:1] == ["--no-xunit"]
    tests = []
    for spec in argv[1:] if no_xunit else argv:
        name, sep, outcomes = spec.rpartition("=")
        outcomes = outcomes.split(",")
        if not sep or " :: " not in name or not all(outcome_ok(o) for o in outcomes):
            return usage(
                "not NAME=OUTCOME[@SECONDS][,...] (OUTCOME %s): %r"
                % ("|".join(OUTCOMES), spec)
            )
        tests.append([name, outcomes])
    os.makedirs(build, exist_ok=True)
    with open(
        os.path.join(build, "lit-scenario.json"), "w", encoding="utf-8", newline="\n"
    ) as f:
        json.dump({"no_xunit": no_xunit, "tests": tests}, f, indent=1)
    state = os.path.join(build, "lit-scenario-state.json")
    if os.path.exists(state):
        os.remove(state)
    return 0


def setup(build):
    bindir = os.path.join(build, "bin")
    os.makedirs(bindir, exist_ok=True)
    for name in ("llvm-lit.py", "llvm-lit"):
        with open(os.path.join(bindir, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(STAND_IN_LIT)
    return 0


def ps1(build, pairs, args):
    pwsh = shutil.which("pwsh")
    if not pwsh:
        return usage("pwsh not found")
    cmd = [pwsh, "-NoProfile", "-NonInteractive", "-File", os.path.join(CI, "lit-legs.ps1")]
    return run(cmd + ["-BuildDir", os.path.abspath(build)] + args, clean_env(pairs))


def sh(distro, build, pairs, args):
    build = os.path.abspath(build)
    if os.name != "nt":
        cmd = ["bash", os.path.join(CI, "lit-legs.sh")] + args + [build]
        return run(cmd, clean_env(pairs))
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return usage("wsl.exe not found")
    unset = []
    for v in CLEARED:
        unset += ["-u", v]
    sets = ["%s=%s" % (k, wsl_path(v)) for k, v in pairs]
    cmd = [wsl, "-d", distro, "--cd", REPO, "--exec", "env"] + unset + sets
    cmd += ["bash", "msil/utils/ci/lit-legs.sh"] + [wsl_path(a) for a in args] + [wsl_path(build)]
    env = dict(os.environ)
    env["WSL_UTF8"] = "1"  # wsl.exe's own messages in UTF-8, not UTF-16
    return run(cmd, env)


def main(argv):
    if len(argv) >= 2 and argv[0] == "setup":
        return setup(argv[1])
    if len(argv) >= 2 and argv[0] == "scenario":
        return scenario(argv[1], argv[2:])
    if len(argv) == 2 and argv[0] == "python":
        return python_stand_in(argv[1])
    if len(argv) == 2 and argv[0] == "error-message":
        return error_messages(argv[1])
    try:
        if len(argv) >= 2 and argv[0] == "ps1":
            pairs, args = split(argv[2:])
            return ps1(argv[1], pairs, args)
        if len(argv) >= 2 and argv[0] == "sh":
            rest = argv[1:]
            distro = "Ubuntu"
            if len(rest) >= 2 and rest[0] == "--distro":
                distro, rest = rest[1], rest[2:]
            if not rest:
                return usage("sh needs BUILD")
            pairs, args = split(rest[1:])
            return sh(distro, rest[0], pairs, args)
    except ValueError as e:
        return usage(str(e))
    return usage(
        "usage: setup BUILD | scenario BUILD [--no-xunit] NAME=OUTCOME... | python DIR | "
        "error-message LOG | "
        "ps1 BUILD [VAR=VALUE...] -- [ARG...] | "
        "sh [--distro D] BUILD [VAR=VALUE...] -- [ARG...]"
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
