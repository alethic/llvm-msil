#!/usr/bin/env python3
# ===-- ci-build.py - Run the CI build scripts against stand-in tools ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run msil/utils/ci/build.ps1 or build.sh in a scratch repository whose build
tools are stand-ins, for ci-build-builtins.test and ci-build-builtins-sh.test.

    ci-build.py ps1 WORK --ninja N --builtins N [VAR=VALUE...] -- [ARG...]
        pwsh -NoProfile -NonInteractive -File
        WORK/repo/msil/utils/ci/build.ps1 ARG..., with MSIL_PYTHON set to the
        Python that runs ci-build.py unless a VAR=VALUE pair sets it.
    ci-build.py sh [--distro D] WORK --ninja N --builtins N [VAR=VALUE...]
                -- [ARG...]
        bash WORK/repo/msil/utils/ci/build.sh ARG... with WORK/bin first on
        PATH: inside the WSL distribution D (default Ubuntu) on Windows, with
        every absolute Windows path among WORK, the VALUEs and the ARGs
        written as /mnt/<drive>/...; directly elsewhere.

The scripts find their tools from their own location: build.ps1 through
$MsilRepo, which msil/utils/build/msil-env.ps1 derives from its own location,
and build.sh through $MSIL_REPO, which ci-common.sh derives the same way. So
before the run, WORK/repo becomes a scratch repository: fresh copies of
msil/utils/ci/build.ps1, ci-common.ps1, build-targets.txt, build.sh and
ci-common.sh and of msil/utils/build/msil-env.ps1, and these stand-ins, each
of which appends the line '<name> <its arguments>' to WORK/calls.log:
    WORK/repo/msil/utils/build/msil-build.ps1    exits N of --ninja (the
                                                 ninja step of build.ps1)
    WORK/bin/ninja                               exits N of --ninja (the
                                                 ninja step of build.sh)
    WORK/repo/msil/utils/build/build-builtins.py exits N of --builtins
    WORK/repo/msil/utils/jitall/build-jitall.py  exits 0
WORK/repo, WORK/bin and WORK/calls.log are removed first; nothing else in
WORK is touched.

The script runs with MSIL_CI_RESULTS, GITHUB_STEP_SUMMARY and MSIL_BUILD_DIR
removed from its environment and then the VAR=VALUE pairs set, so neither a
gate's results file nor a CI step summary gets these rows, and no build
directory but the one the ARGs name reaches it. Its stdout and stderr are
printed together, without colour escapes and CRs; ci-build.py exits with its
exit status (2 after a usage error of its own).
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 5))
CLEARED = ("MSIL_CI_RESULTS", "GITHUB_STEP_SUMMARY", "MSIL_BUILD_DIR")

# The real files of the scratch repository, repository-relative.
COPIED = (
    "msil/utils/ci/build.ps1",
    "msil/utils/ci/ci-common.ps1",
    "msil/utils/ci/build-targets.txt",
    "msil/utils/ci/build.sh",
    "msil/utils/ci/ci-common.sh",
    "msil/utils/build/msil-env.ps1",
)

# The stand-ins: @NAME@ is the stand-in's name and @STATUS@ its exit status.
# Each finds WORK/calls.log from its own location, so the same text works
# inside WSL and on Windows.
MSIL_BUILD_PS1 = """\
# Stand-in for msil/utils/build/msil-build.ps1 (ci-build.py).
Add-Content -LiteralPath (Join-Path $PSScriptRoot '../../../../calls.log') `
  -Encoding utf8 -Value ('@NAME@ ' + ($args -join ' '))
exit @STATUS@
"""

NINJA = """\
#!/bin/sh
# Stand-in for ninja (ci-build.py).
printf '%s\\n' "@NAME@ $*" >> "$(dirname "$0")/../calls.log"
exit @STATUS@
"""

PYTHON_TOOL = """\
# Stand-in for @NAME@ (ci-build.py).
import os
import sys

log = os.path.join(os.path.dirname(os.path.abspath(__file__)), *[os.pardir] * 4)
with open(os.path.join(log, "calls.log"), "a", encoding="utf-8", newline="\\n") as f:
    f.write(" ".join(["@NAME@"] + sys.argv[1:]) + "\\n")
sys.exit(@STATUS@)
"""

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
DRIVE_RE = re.compile(r"^([A-Za-z]):[\\/](.*)$")
PAIR_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")


class UsageError(Exception):
    pass


def usage(msg):
    sys.stderr.write("ci-build.py: %s\n" % msg)
    return 2


def wsl_path(text):
    """X:\\a\\b -> /mnt/x/a/b; anything else unchanged."""
    m = DRIVE_RE.match(text)
    if not m:
        return text
    return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2).replace("\\", "/"))


def parse(argv):
    """(WORK, ninja status, builtins status, VAR=VALUE pairs, ARGs) of
    'WORK --ninja N --builtins N [VAR=VALUE...] -- [ARG...]'."""
    if "--" not in argv:
        raise UsageError("missing '--' before the script's arguments")
    i = argv.index("--")
    head, args = argv[:i], argv[i + 1 :]
    if not head or head[0].startswith("-") or PAIR_RE.match(head[0]):
        raise UsageError("WORK must come first")
    work, rest = head[0], head[1:]
    status = {}
    pairs = []
    while rest:
        word = rest.pop(0)
        if word in ("--ninja", "--builtins"):
            if not rest or not re.match(r"^[0-9]{1,3}$", rest[0]) or int(rest[0]) > 255:
                raise UsageError("%s needs an exit status 0-255" % word)
            status[word] = int(rest.pop(0))
        elif PAIR_RE.match(word):
            pairs.append(tuple(word.split("=", 1)))
        else:
            raise UsageError("not --ninja N, --builtins N or VAR=VALUE: %r" % word)
    for name in ("--ninja", "--builtins"):
        if name not in status:
            raise UsageError("missing %s N" % name)
    return work, status["--ninja"], status["--builtins"], pairs, args


def write(path, template, name, status):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = template.replace("@NAME@", name).replace("@STATUS@", str(status))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def make_repo(work, ninja, builtins):
    """Write WORK/repo and WORK/bin as the module docstring says."""
    repo = os.path.join(work, "repo")
    bindir = os.path.join(work, "bin")
    for d in (repo, bindir):
        if os.path.isdir(d):
            shutil.rmtree(d)
    calls = os.path.join(work, "calls.log")
    if os.path.exists(calls):
        os.remove(calls)
    for rel in COPIED:
        dst = os.path.join(repo, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(REPO, *rel.split("/")), dst)
    build = os.path.join(repo, "msil", "utils", "build")
    write(
        os.path.join(build, "msil-build.ps1"), MSIL_BUILD_PS1, "msil-build.ps1", ninja
    )
    write(os.path.join(bindir, "ninja"), NINJA, "ninja", ninja)
    # Native Linux needs the mode bit; WSL shows Windows files as rwxrwxrwx.
    os.chmod(os.path.join(bindir, "ninja"), 0o755)
    write(
        os.path.join(build, "build-builtins.py"),
        PYTHON_TOOL,
        "build-builtins.py",
        builtins,
    )
    write(
        os.path.join(repo, "msil", "utils", "jitall", "build-jitall.py"),
        PYTHON_TOOL,
        "build-jitall.py",
        0,
    )
    return repo


def clean_env(pairs):
    env = {k: v for k, v in os.environ.items() if k.upper() not in CLEARED}
    env.update(pairs)
    # pwsh's own colour (PowerShell 7.2 and later honour NO_COLOR).
    env["NO_COLOR"] = "1"
    return env


def run(cmd, env, cwd):
    res = subprocess.run(
        cmd,
        env=env,
        cwd=cwd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    text = ANSI_RE.sub("", res.stdout.decode("utf-8", "replace")).replace("\r", "")
    # As bytes, so that no console code page can refuse a character.
    sys.stdout.flush()
    sys.stdout.buffer.write(text.encode("utf-8"))
    sys.stdout.buffer.flush()
    return res.returncode


def ps1(work, ninja, builtins, pairs, args):
    pwsh = shutil.which("pwsh")
    if not pwsh:
        return usage("pwsh not found")
    work = os.path.abspath(work)
    repo = make_repo(work, ninja, builtins)
    env = clean_env(pairs)
    if "MSIL_PYTHON" not in dict(pairs):
        env["MSIL_PYTHON"] = sys.executable.replace("\\", "/")
    script = os.path.join(repo, "msil", "utils", "ci", "build.ps1")
    cmd = [pwsh, "-NoProfile", "-NonInteractive", "-File", script] + args
    return run(cmd, env, work)


def sh(distro, work, ninja, builtins, pairs, args):
    work = os.path.abspath(work)
    if os.name != "nt":
        repo = make_repo(work, ninja, builtins)
        env = clean_env(pairs)
        env["PATH"] = os.path.join(work, "bin") + os.pathsep + env.get("PATH", "")
        script = os.path.join(repo, "msil", "utils", "ci", "build.sh")
        return run(["bash", script] + args, env, work)
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        return usage("wsl.exe not found")
    make_repo(work, ninja, builtins)
    unset = []
    for v in CLEARED:
        unset += ["-u", v]
    sets = ["%s=%s" % (k, wsl_path(v)) for k, v in pairs]
    # WSL appends the Windows PATH to its own, so the shell puts WORK/bin in
    # front of what WSL computed.
    prepend = 'PATH="$1:$PATH"; shift; exec bash "$@"'
    cmd = [wsl, "-d", distro, "--cd", work, "--exec", "env"] + unset + sets
    cmd += ["bash", "-c", prepend, "ci-build.py", wsl_path(os.path.join(work, "bin"))]
    cmd += [wsl_path(os.path.join(work, "repo", "msil", "utils", "ci", "build.sh"))]
    cmd += [wsl_path(a) for a in args]
    env = dict(os.environ)
    env["WSL_UTF8"] = "1"  # wsl.exe's own messages in UTF-8, not UTF-16
    return run(cmd, env, work)


def main(argv):
    try:
        if argv[:1] == ["ps1"]:
            return ps1(*parse(argv[1:]))
        if argv[:1] == ["sh"]:
            rest = argv[1:]
            distro = "Ubuntu"
            if rest[:1] == ["--distro"]:
                if len(rest) < 2:
                    raise UsageError("--distro needs a distribution")
                distro, rest = rest[1], rest[2:]
            return sh(distro, *parse(rest))
    except UsageError as e:
        return usage(str(e))
    return usage(
        "usage: ps1 WORK --ninja N --builtins N [VAR=VALUE...] -- [ARG...] | "
        "sh [--distro D] WORK --ninja N --builtins N [VAR=VALUE...] -- [ARG...]"
    )


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
