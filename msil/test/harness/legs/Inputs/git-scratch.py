#!/usr/bin/env python3
# ===-- git-scratch.py - A scratch git repository for gate-select.test -----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Make and change a scratch git repository for gate-select.test.

    git-scratch.py init REPO FILE...     create REPO (which must not exist),
                                         its FILEs and one commit
    git-scratch.py change REPO FILE...   write new contents to each FILE
                                         (creating it), not staged
    git-scratch.py stage REPO FILE...    'git add' each FILE
    git-scratch.py delete REPO FILE...   delete each FILE from the work tree
    git-scratch.py commit REPO           commit every change (git add -A)
    git-scratch.py select REPO MODE [ARG...]
                                         run msil/utils/ci/gate-select.py
                                         MODE ARG... --repo REPO and exit
                                         with its status

FILEs are paths relative to REPO with '/' separators. Every git command
names REPO's .git directory and work tree explicitly (--git-dir,
--work-tree), so it can never act on a repository around REPO (the build
directory of a lit run may sit inside a checkout), and runs with
GIT_CONFIG_NOSYSTEM=1 and GIT_CONFIG_GLOBAL set to an empty file next to
REPO, so that no system or user setting (hooks, signing, templates, line
endings) reaches it. gate-select.py runs with that environment as well and
with GIT_CEILING_DIRECTORIES set to REPO's parent, so that its git never
finds a repository around REPO either. Exits 0, 1 when git fails, 2 on a
usage error ('select': gate-select.py's status).
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GATE_SELECT = os.path.normpath(
    os.path.join(HERE, *[os.pardir] * 5 + ["msil", "utils", "ci", "gate-select.py"])
)


def usage(msg):
    sys.stderr.write("git-scratch: %s\n" % msg)
    return 2


def environment(repo):
    config = os.path.join(
        os.path.dirname(os.path.abspath(repo)), "git-scratch.gitconfig"
    )
    if not os.path.exists(config):
        open(config, "w").close()
    env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=config)
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE"):
        env.pop(name, None)
    return env


def git(repo, *args):
    repo = os.path.abspath(repo)
    cmd = ["git", "--git-dir=" + os.path.join(repo, ".git"), "--work-tree=" + repo]
    res = subprocess.run(
        cmd + list(args), env=environment(repo), stdin=subprocess.DEVNULL
    )
    if res.returncode:
        sys.stderr.write(
            "git-scratch: git %s exited %d\n" % (" ".join(args), res.returncode)
        )
        sys.exit(1)


def write(repo, rel, text):
    path = os.path.join(repo, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def commit(repo, message):
    git(repo, "add", "-A")
    git(
        repo,
        "-c",
        "user.name=git-scratch",
        "-c",
        "user.email=git-scratch@example.invalid",
        "commit",
        "-q",
        "-m",
        message,
    )


def main(argv):
    if len(argv) < 2 or argv[0] not in (
        "init",
        "change",
        "stage",
        "delete",
        "commit",
        "select",
    ):
        return usage(
            "usage: init|change|stage|delete REPO FILE... | commit REPO | select REPO MODE [ARG...]"
        )
    cmd, repo, files = argv[0], argv[1], argv[2:]
    if cmd == "select":
        env = environment(repo)
        env["GIT_CEILING_DIRECTORIES"] = os.path.dirname(os.path.abspath(repo))
        sys.stdout.flush()
        return subprocess.run(
            [sys.executable, GATE_SELECT] + files + ["--repo", repo],
            env=env,
            stdin=subprocess.DEVNULL,
        ).returncode
    if cmd == "init":
        if os.path.exists(repo):
            return usage("%s exists" % repo)
        res = subprocess.run(
            ["git", "init", "-q", repo], env=environment(repo), stdin=subprocess.DEVNULL
        )
        if res.returncode or not os.path.isdir(os.path.join(repo, ".git")):
            sys.stderr.write("git-scratch: git init %s failed\n" % repo)
            return 1
        for rel in files:
            write(repo, rel, "base\n")
        commit(repo, "base")
        return 0
    if not os.path.isdir(os.path.join(repo, ".git")):
        return usage("%s is not a repository that 'init' made" % repo)
    if cmd == "commit":
        if files:
            return usage("commit takes no FILE")
        commit(repo, "change")
    elif not files:
        return usage("%s needs a FILE" % cmd)
    elif cmd == "change":
        for rel in files:
            write(repo, rel, "changed\n")
    elif cmd == "stage":
        git(repo, "add", "--", *files)
    elif cmd == "delete":
        for rel in files:
            os.remove(os.path.join(repo, *rel.split("/")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
