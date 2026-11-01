#!/usr/bin/env python3
# ===-- steps-replay.py - Replay the steps of a msil-steps.py command ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run a msil-steps.py command, then the command line of each of its headers
by itself, and compare them (plan 5.17.10 item 2), for
../steps-equivalence.test.

    steps-replay.py [--status N] -- STEPS-COMMAND...

STEPS-COMMAND is a msil-steps.py command line as lit expands it
('%msil_steps --step=NAME CMD... --step=NAME CMD...'). steps-replay.py runs it
with stdin closed, prints its stdout and stderr, and splits its stdout at the
header lines 'msil-steps: NAME: <command line>'. For every header:

  - NAME is the next step of STEPS-COMMAND, and the words of <command line>
    (split with POSIX shell rules, as lit quotes its log) are the words that
    follow that --step=NAME in STEPS-COMMAND: the header is the step's command
    line;
  - that command line, run by itself (this directory and environment, stdin
    closed), prints on stdout what msil-steps.py printed after the header;
  - it exits with status 0, unless it is the last step that ran.

A header 'msil-steps: NAME: skipped: <reason>' (a check of a skipped run) is
not replayed; nothing may follow it before the next header. Then:
msil-steps.py's stderr is the replays' stderr in order, followed by
'msil-steps: NAME failed (exit status N)' when the last step failed with N;
msil-steps.py's exit status is that N, else 0; and when a step failed, no
later step ran. CRs are dropped before any comparison (Windows line ends).

Prints 'steps-replay: NAME: same output and exit status S' for each replayed
step, 'steps-replay: NAME: skipped' for a skipped one, and at the end
'steps-replay: msil-steps.py ran K of M steps, exit status S'. Exits 0 when
everything matched and S is --status (default 0), 1 with every difference
otherwise, 2 on a usage error.
"""

import re
import shlex
import subprocess
import sys

HEADER_RE = re.compile(r"^msil-steps: ([^:\s]+): (.*)$")


def usage(msg):
    sys.stderr.write("steps-replay: error: %s\n" % msg)
    return 2


def text(data):
    return data.decode("utf-8", "replace").replace("\r", "")


def run(words):
    res = subprocess.run(
        words, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    return res.returncode, text(res.stdout), text(res.stderr)


def given_steps(command):
    """[(NAME, [word...])] of the --step=NAME groups of a msil-steps command."""
    steps = []
    for word in command:
        if word.startswith("--step="):
            steps.append((word[len("--step=") :], []))
        elif steps:
            steps[-1][1].append(word)
    return steps


def segments(stdout):
    """(lines before the first header, [(NAME, command line, body)])."""
    lead, segs = [], []
    for line in stdout.splitlines(True):
        m = HEADER_RE.match(line.rstrip("\n"))
        if m:
            segs.append([m.group(1), m.group(2), ""])
        elif segs:
            segs[-1][2] += line
        else:
            lead.append(line)
    return lead, segs


def show(label, value):
    lines = value.splitlines() or ["(nothing)"]
    return "\n".join(["  %s:" % label] + ["    | " + l for l in lines])


def main(argv):
    want = 0
    if argv[:1] == ["--status"]:
        if len(argv) < 2 or not re.fullmatch(r"\d+", argv[1]):
            return usage("--status needs a number")
        want, argv = int(argv[1]), argv[2:]
    if argv[:1] != ["--"] or len(argv) < 2:
        return usage("usage: steps-replay.py [--status N] -- STEPS-COMMAND...")
    command = argv[1:]
    steps = given_steps(command)
    if not steps:
        return usage("the command has no --step=NAME")

    status, out, err = run(command)
    sys.stdout.write(
        "--- msil-steps.py stdout\n%s--- msil-steps.py stderr\n%s---\n" % (out, err)
    )
    problems = []
    lead, segs = segments(out)
    if lead:
        problems.append(
            "output before the first header:\n" + show("stdout", "".join(lead))
        )
    if len(segs) > len(steps):
        problems.append("%d headers for %d steps" % (len(segs), len(steps)))
    replay_err = ""
    failed = None
    for i, (name, cmdline, body) in enumerate(segs[: len(steps)]):
        gname, gwords = steps[i]
        if name != gname:
            problems.append(
                "header %d names step %s, the command's step %d is %s"
                % (i + 1, name, i + 1, gname)
            )
            continue
        if failed is not None:
            problems.append("step %s ran after step %s failed" % (name, failed[0]))
        if cmdline.startswith("skipped: "):
            if body:
                problems.append(
                    "step %s: output after its skip line:\n%s"
                    % (name, show("stdout", body))
                )
            print("steps-replay: %s: skipped" % name)
            continue
        try:
            words = shlex.split(cmdline)
        except ValueError as e:
            problems.append(
                "step %s: the header's command line does not split: %s" % (name, e)
            )
            continue
        if words != gwords:
            problems.append(
                "step %s: the header's words are not the step's words:\n%s\n%s"
                % (
                    name,
                    show("header", "\n".join(words)),
                    show("step", "\n".join(gwords)),
                )
            )
            continue
        rstatus, rout, rerr = run(words)
        replay_err += rerr
        if rstatus != 0 and failed is None:
            failed = (name, rstatus, i)
        if rout != body:
            problems.append(
                "step %s: stdout differs from the replay's:\n%s\n%s"
                % (name, show("msil-steps.py", body), show("replay", rout))
            )
            continue
        print("steps-replay: %s: same output and exit status %d" % (name, rstatus))

    expect_err = replay_err
    expect_status = 0
    if failed is not None:
        expect_err += "msil-steps: %s failed (exit status %d)\n" % failed[:2]
        expect_status = failed[1]
        if len(segs) != failed[2] + 1:
            problems.append("steps ran after the failing step %s" % failed[0])
    elif len(segs) != len(steps):
        problems.append("%d of %d steps ran, and none failed" % (len(segs), len(steps)))
    if err != expect_err:
        problems.append(
            "stderr differs from the replays' stderr:\n%s\n%s"
            % (show("msil-steps.py", err), show("replays", expect_err))
        )
    if status != expect_status:
        problems.append(
            "msil-steps.py exited with %d, the steps say %d" % (status, expect_status)
        )
    if status != want:
        problems.append(
            "msil-steps.py exited with %d, not --status %d" % (status, want)
        )
    print(
        "steps-replay: msil-steps.py ran %d of %d steps, exit status %d"
        % (len(segs), len(steps), status)
    )
    for p in problems:
        print("steps-replay: error: " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
