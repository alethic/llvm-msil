#!/usr/bin/env python3
# ===-- gate-select.py - Which optional gate steps a change needs ----------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Select the change-dependent steps of a milestone gate (plan 5.17.5, 5.17.7).

    python msil/utils/ci/gate-select.py harness [--base REV] [--repo DIR]
    python msil/utils/ci/gate-select.py expected-dirs [--base REV] [--repo DIR]
    python msil/utils/ci/gate-select.py --self-test

The change set is every path that 'git diff --name-only --no-renames <REV>'
prints (the working tree and the index against REV, default HEAD; a renamed
file counts under its old and its new name) plus every path of 'git status
--porcelain --untracked-files=all' (untracked files among them), all
repository-relative with '/'. --repo is the repository (default: the one
this script is in).

harness prints 'harness: run' and then the changed harness files, one per
line and sorted, deleted ones included, or 'harness: skip' when no harness
file changed (step G3c runs 'lit-legs.ps1 -Leg harness' only after
'harness: run'). Harness files are: msil/utils/** except
msil/utils/corpus/manifest.json and msil/utils/upstream-hooks.txt;
msil/test/lit.cfg.py and msil/test/lit.site.cfg.py.in; every lit.local.cfg
and every .py file under msil/test; msil/test/harness/**.

expected-dirs prints, sorted and one per line, each directory under msil/test
(repository-relative, outside every Inputs/ and Output/ directory) that holds
an added or changed .c, .ll, .test or .expected file (.linux.expected
included) that exists; a deleted file selects nothing. Step G7 passes them to
'update-expected.py --check --levels O0,O2' for both oracles, which walks
each directory (and the directories below it) and skips the tests that have
no native oracle. Nothing is printed when no such file changed.

Exit status: 0 (also for 'harness: skip' and an empty expected-dirs), 1 when
git fails (its message is printed; an unknown REV, no repository), 2 for a
usage error. --self-test builds throwaway repositories in a temporary
directory outside this repository (git with neither the user's nor the
system's configuration) and exits 0, or 1 naming each failed case. Python 3
standard library and git only.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
)

NOT_HARNESS = ("msil/utils/corpus/manifest.json", "msil/utils/upstream-hooks.txt")
HARNESS_FILES = ("msil/test/lit.cfg.py", "msil/test/lit.site.cfg.py.in")
EXPECTED_SUFFIXES = (".c", ".ll", ".test", ".expected")
SKIPPED_DIRS = ("Inputs", "Output")


class GitError(Exception):
    pass


def git(repo, args, env=None):
    try:
        res = subprocess.run(
            ["git", "-C", repo] + args,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
    except OSError as e:
        raise GitError("cannot run git: %s" % e)
    if res.returncode != 0:
        msg = res.stderr.decode("utf-8", "replace").strip()
        raise GitError("git %s exited %d: %s" % (" ".join(args), res.returncode, msg))
    return res.stdout.decode("utf-8", "surrogateescape")


def status_paths(text):
    """The paths of 'git status --porcelain=v1 -z' output: 'XY <path>' entries,
    a rename or copy followed by its source path as an entry of its own."""
    paths = []
    fields = text.split("\0")
    i = 0
    while i < len(fields):
        entry = fields[i]
        i += 1
        if not entry:
            continue
        if len(entry) < 4 or entry[2] != " ":
            raise GitError("unexpected 'git status --porcelain -z' entry %r" % entry)
        paths.append(entry[3:])
        if entry[0] in "RC" and i < len(fields):
            paths.append(fields[i])
            i += 1
    return paths


def changed_paths(repo, base, env=None):
    diff = git(repo, ["diff", "--name-only", "--no-renames", "-z", base, "--"], env)
    status = git(
        repo,
        ["status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames"],
        env,
    )
    paths = {p for p in diff.split("\0") if p}
    paths.update(status_paths(status))
    return sorted(paths)


def is_harness(path):
    if path.startswith("msil/utils/"):
        return path not in NOT_HARNESS
    if path in HARNESS_FILES or path.startswith("msil/test/harness/"):
        return True
    if path.startswith("msil/test/"):
        name = path.rsplit("/", 1)[-1]
        return name == "lit.local.cfg" or name.endswith(".py")
    return False


def expected_dir(repo, path):
    """The directory of path for expected-dirs, or None."""
    if not path.startswith("msil/test/") or not path.endswith(EXPECTED_SUFFIXES):
        return None
    parts = path.split("/")
    if any(p in SKIPPED_DIRS for p in parts[:-1]):
        return None
    if not os.path.isfile(os.path.join(repo, *parts)):
        return None
    return "/".join(parts[:-1])


def harness(repo, base, out, env=None):
    files = [p for p in changed_paths(repo, base, env) if is_harness(p)]
    if not files:
        out.write("harness: skip\n")
        return 0
    out.write("harness: run\n")
    for p in files:
        out.write(p + "\n")
    return 0


def expected_dirs(repo, base, out, env=None):
    dirs = {
        d for d in (expected_dir(repo, p) for p in changed_paths(repo, base, env)) if d
    }
    for d in sorted(dirs):
        out.write(d + "\n")
    return 0


# ---------------------------------------------------------------- self-test


def isolated_env(work):
    """git without the user's or the system's configuration (no hooks,
    signing or attributes of the host), with a fixed identity."""
    empty = os.path.join(work, "empty-gitconfig")
    open(empty, "w").close()
    env = dict(os.environ)
    for k in list(env):
        if k.upper().startswith("GIT_"):
            del env[k]
    env.update(
        GIT_CEILING_DIRECTORIES=work,
        GIT_CONFIG_NOSYSTEM="1",
        GIT_CONFIG_GLOBAL=empty,
        GIT_AUTHOR_NAME="gate-select",
        GIT_AUTHOR_EMAIL="gate-select@example.invalid",
        GIT_COMMITTER_NAME="gate-select",
        GIT_COMMITTER_EMAIL="gate-select@example.invalid",
    )
    return env


BASE_FILES = {
    "llvm/lib/Target/MSIL/MSILAsmPrinter.cpp": "// target\n",
    "llvm/test/lit.local.cfg": "config.suffixes = ['.ll']\n",
    "msil/utils/msil-run.py": "# runner\n",
    "msil/utils/ci/lit-legs.ps1": "# legs\n",
    "msil/utils/corpus/manifest.json": "{}\n",
    "msil/utils/upstream-hooks.txt": "# hooks\n",
    "msil/test/lit.cfg.py": "# lit\n",
    "msil/test/lit.site.cfg.py.in": "# site\n",
    "msil/test/README.md": "# tests\n",
    "msil/test/M1/02-fp/fadd.c": "// RUN: %{msil_c}\n",
    "msil/test/M1/02-fp/fadd.expected": "[exit 0]\n",
    "msil/test/M1/05-memory/memcpy.c": "// RUN: %{msil_c}\n",
    "msil/test/M2/08-semantics/matrix.txt": "# matrix\n",
    "msil/test/M2/08-semantics/Inputs/check-matrix.py": "# checker\n",
    "msil/test/M2/08-semantics/Inputs/helper.c": "int h;\n",
    "msil/test/harness/csc.test": "# RUN: true\n",
}


def write(root, rel, text):
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def make_repo(work, name, env):
    root = os.path.join(work, name)
    os.makedirs(root)
    git(root, ["init", "-q"], env)
    git(root, ["config", "core.autocrlf", "false"], env)
    for rel, text in BASE_FILES.items():
        write(root, rel, text)
    git(root, ["add", "-A"], env)
    git(root, ["commit", "-q", "-m", "base"], env)
    return root


def run_mode(mode, root, base, env):
    import io

    out = io.StringIO()
    rc = (harness if mode == "harness" else expected_dirs)(root, base, out, env)
    return rc, out.getvalue().splitlines()


def self_test():
    work = tempfile.mkdtemp(prefix="gate-select-self-test-")
    env = isolated_env(work)
    problems = []
    count = [0]

    def expect(what, root, mode, want, base="HEAD"):
        count[0] += 1
        try:
            rc, lines = run_mode(mode, root, base, env)
        except GitError as e:
            problems.append("%s: %s" % (what, e))
            return
        if rc != 0 or lines != want:
            problems.append(
                "%s: %s printed %r (exit %d), want %r" % (what, mode, lines, rc, want)
            )

    try:
        n = [0]

        def fresh():
            n[0] += 1
            return make_repo(work, "r%d" % n[0], env)

        r = fresh()
        expect("a clean tree", r, "harness", ["harness: skip"])
        expect("a clean tree", r, "expected-dirs", [])

        r = fresh()
        write(r, "msil/utils/corpus/manifest.json", '{"entries": []}\n')
        write(r, "msil/utils/upstream-hooks.txt", "# hooks, changed\n")
        write(r, "msil/test/README.md", "# tests, changed\n")
        write(r, "msil/test/M2/08-semantics/matrix.txt", "# matrix, changed\n")
        write(r, "llvm/test/lit.local.cfg", "config.suffixes = ['.ll', '.mir']\n")
        write(r, "llvm/lib/Target/MSIL/MSILAsmPrinter.cpp", "// target, changed\n")
        expect(
            "the corpus manifest, the hooks list and non-harness files",
            r,
            "harness",
            ["harness: skip"],
        )
        expect("no test source changed", r, "expected-dirs", [])

        r = fresh()
        write(r, "msil/utils/msil-run.py", "# runner, changed\n")
        write(r, "msil/test/M3/lit.local.cfg", "config.msil_enable_hosted(config)\n")
        write(
            r,
            "msil/test/M2/08-semantics/Inputs/check-matrix.py",
            "# checker, changed\n",
        )
        os.remove(os.path.join(r, "msil", "test", "harness", "csc.test"))
        write(r, "msil/test/harness/legs/new dir/timeout-retry.test", "# RUN: true\n")
        expect(
            "a changed tool, an untracked lit.local.cfg, a changed .py under Inputs, a deleted "
            "and an untracked harness test",
            r,
            "harness",
            [
                "harness: run",
                "msil/test/M2/08-semantics/Inputs/check-matrix.py",
                "msil/test/M3/lit.local.cfg",
                "msil/test/harness/csc.test",
                "msil/test/harness/legs/new dir/timeout-retry.test",
                "msil/utils/msil-run.py",
            ],
        )
        expect(
            "a harness test in a directory with a blank",
            r,
            "expected-dirs",
            ["msil/test/harness/legs/new dir"],
        )

        r = fresh()
        write(r, "msil/test/lit.site.cfg.py.in", "# site, changed\n")
        git(r, ["add", "-A"], env)
        expect(
            "a staged change",
            r,
            "harness",
            ["harness: run", "msil/test/lit.site.cfg.py.in"],
        )

        r = fresh()
        write(r, "msil/test/M1/02-fp/fadd.c", "// RUN: %{msil_c}\n// changed\n")
        write(r, "msil/test/M1/02-fp/strict-i128.c", "// RUN: %{msil_c}\n")
        write(r, "msil/test/M3/10-hosted/stdio.c", "// RUN: %{msil_c}\n")
        write(r, "msil/test/M3/10-hosted/stdio.linux.expected", "[exit 0]\n")
        write(r, "msil/test/M2/09-hosted/glibc.test", "# RUN: true\n")
        write(r, "msil/test/M2/08-semantics/Inputs/helper.c", "int h, i;\n")
        write(r, "msil/test/M2/08-semantics/Output/stale.c", "int x;\n")
        write(r, "msil/test/M2/08-semantics/notes.txt", "notes\n")
        write(r, "msil/test/M1/05-memory/memcpy.h", "// a header\n")
        write(r, "llvm/test/CodeGen/MSIL/new.ll", "; RUN: llc\n")
        os.remove(os.path.join(r, "msil", "test", "M1", "05-memory", "memcpy.c"))
        expect(
            "changed, added and deleted test sources",
            r,
            "expected-dirs",
            ["msil/test/M1/02-fp", "msil/test/M2/09-hosted", "msil/test/M3/10-hosted"],
        )
        expect("test sources only", r, "harness", ["harness: skip"])

        # --base: committed changes count against an older revision only.
        r = fresh()
        write(r, "msil/test/M1/05-memory/memcpy.expected", "[exit 0]\n")
        write(r, "msil/utils/ci/lit-legs.ps1", "# legs, changed\n")
        git(r, ["add", "-A"], env)
        git(r, ["commit", "-q", "-m", "change"], env)
        expect("committed changes against HEAD", r, "expected-dirs", [])
        expect("committed changes against HEAD", r, "harness", ["harness: skip"])
        expect(
            "--base HEAD~1",
            r,
            "expected-dirs",
            ["msil/test/M1/05-memory"],
            base="HEAD~1",
        )
        expect(
            "--base HEAD~1",
            r,
            "harness",
            ["harness: run", "msil/utils/ci/lit-legs.ps1"],
            base="HEAD~1",
        )
        # A rename counts under both names.
        git(r, ["mv", "msil/utils/msil-run.py", "msil/utils/msil-run2.py"], env)
        expect(
            "a rename",
            r,
            "harness",
            ["harness: run", "msil/utils/msil-run.py", "msil/utils/msil-run2.py"],
        )
        git(r, ["commit", "-q", "-m", "rename"], env)
        expect(
            "a committed rename against --base HEAD~1",
            r,
            "harness",
            ["harness: run", "msil/utils/msil-run.py", "msil/utils/msil-run2.py"],
            base="HEAD~1",
        )

        # Failures: an unknown revision and a directory that is no repository.
        count[0] += 2
        try:
            run_mode("harness", r, "no-such-revision", env)
            problems.append("an unknown --base: no error")
        except GitError as e:
            if "no-such-revision" not in str(e):
                problems.append("an unknown --base: %s" % e)
        plain = os.path.join(work, "plain")
        os.makedirs(plain)
        try:
            run_mode("expected-dirs", plain, "HEAD", env)
            problems.append("no repository: no error")
        except GitError:
            pass
        # The command line.
        for argv, want in (
            ([], 2),
            (["frob"], 2),
            (["harness", "--base"], 2),
            (["harness", "--repo", plain], 1),
        ):
            count[0] += 1
            res = subprocess.run(
                [sys.executable, os.path.abspath(__file__)] + argv,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env,
            )
            if res.returncode != want or res.stdout:
                problems.append(
                    "command line %r: exit %d (want %d), stdout %r"
                    % (argv, res.returncode, want, res.stdout)
                )
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for p in problems:
        print("gate-select: self-test: FAIL: %s" % p)
    print(
        "gate-select: self-test: %d of %d cases passed"
        % (count[0] - len(problems), count[0])
    )
    return 1 if problems else 0


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write("gate-select: %s\n" % message)
        raise SystemExit(2)


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    ap = Parser(prog="gate-select.py", description=__doc__.split("\n\n")[0])
    ap.add_argument("mode", choices=("harness", "expected-dirs"))
    ap.add_argument("--base", default="HEAD", metavar="REV")
    ap.add_argument("--repo", default=REPO, metavar="DIR")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2
    try:
        fn = harness if args.mode == "harness" else expected_dirs
        return fn(os.path.abspath(args.repo), args.base, sys.stdout)
    except GitError as e:
        sys.stderr.write("gate-select: %s\n" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
