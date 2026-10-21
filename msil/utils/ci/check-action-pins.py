#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Check that the MSIL workflows pin every GitHub action as the upstream workflows pin it, and
that msil-ci.yml runs the upstream-hooks step after its Build step under a valid guard.

  python msil/utils/ci/check-action-pins.py [<repo>]   (default: the repository of this script)
  python msil/utils/ci/check-action-pins.py --self-test

The CI contract (plan M0, the CI work package) is that third-party actions are pinned by SHA
exactly as upstream llvm-project pins them; upstream bumps its pins (Renovate) and an upstream
merge does not touch the fork's workflows, so the pins drift (plan M11, item 2). For every
'uses: <owner>/<repo>[/<path>]@<ref> # <version>' line of .github/**/msil-*.yml and
msil-*.yaml, the ref and the version comment (its first word, '#' stripped, so '#v6.1.0' and
'# v6.1.0' agree) must equal those of at least one use of the same <owner>/<repo> in the other
YAML files under .github (actions/cache/restore and actions/cache/save count as
actions/cache). The value may be single- or double-quoted ('uses: "<owner>/<repo>@<ref>" #
<version>' is valid YAML and GitHub runs it); one matching pair of quotes is stripped, in the
MSIL and in the upstream workflows alike. Each mismatch prints

  <file>:<line>: <action>@<ref> # <version>; upstream: <ref> # <version> (<n> uses), ...

An action that upstream never uses (actions/setup-dotnet) is printed as 'not pinned upstream'
without failing. Every line of an MSIL workflow that starts 'uses:' (or '- uses:') must be such
a pin or an exemption: a local action or reusable workflow ('./...') or a 'docker://' image;
any other uses: line (no '@<ref>', an unterminated quote, the value on the next line) fails
with its file and line, so a pin the parser cannot read is never skipped silently. An MSIL
workflow with uses: lines but no pin fails as well. Unreadable uses: lines of the upstream
workflows only contribute no pin.

It also checks the wiring of the upstream-hooks step (plan M11.d, item (6)), reading
.github/workflows/msil-ci.yml as text: under its top-level 'jobs:', the 'windows:' job must
have a step whose run: is exactly (a one-line scalar)

  pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest -MinPass 250

whose if: is '${{ !cancelled() && steps.<id>.outcome == 'success' }}' (blanks inside may vary),
where <id> is the id: of the job's Build step (the one step whose run: names
msil/utils/ci/build.ps1), and which comes after that step. GitHub evaluates steps.<id> of a
missing, misspelled or later id as null, which would skip the step while the job stays green.
Each problem prints '<file>:<line>: ...' and fails; so does a missing msil-ci.yml. The summary
line ends with '<file> upstream-hooks step: OK' or the number of problems.

--self-test plants .github trees in a temporary directory outside the repository (removed
afterwards) and checks that a reverted unquoted pin, a stale single-quoted pin, a stale
double-quoted pin, a stale pin without a version comment, an unparsable uses: line and an MSIL
workflow without a pin each fail; that a misspelled id in the guard, a missing or blank id: of
the Build step, a deleted upstream-hooks step, one moved to another job or before the Build
step, a changed run: line, a guard without !cancelled() or no guard, no Build step and no
msil-ci.yml each fail; and that the clean tree (quoted pins, every exemption and a correct
upstream-hooks step included) passes.

Exit status: 0 = every pin matches and the upstream-hooks step is wired, 1 = a mismatch, an
unreadable uses: line or a wiring problem (or a self-test failure), 2 = usage error (no .github
directory or no msil-*.yml workflow). Python 3 standard library only.
"""

import collections
import contextlib
import glob
import io
import os
import re
import shutil
import stat
import sys
import tempfile

# Every uses: line, parsed or not; group 1 is the value with its comment.
USES_LINE = re.compile(r"^\s*(?:-\s*)?uses:(.*)$")
# The value of a pin, quotes stripped: <owner>/<repo>[/<path>]@<ref>.
PIN = re.compile(r"^([\w.-]+/[\w.-]+)(/[\w./-]+)?@(\S+)$")
EXEMPT_PREFIXES = ("./", "docker://")

# The upstream-hooks step of the CI workflow's Windows job (plan M11.d (6)) and its guard.
CI_WORKFLOW = ".github/workflows/msil-ci.yml"
CI_JOB = "windows"
HOOKS_SCRIPT = "msil/utils/ci/upstream-hooks.ps1"
HOOKS_RUN = f"pwsh -NoProfile -File {HOOKS_SCRIPT} -NoCompile -NoManifest -MinPass 250"
BUILD_SCRIPT = "msil/utils/ci/build.ps1"
GUARD = "${{ !cancelled() && steps.<id>.outcome == 'success' }}"
GUARD_RE = re.compile(r"^\$\{\{\s*!cancelled\(\)\s*&&\s*steps\.([A-Za-z_][\w-]*)\.outcome\s*==\s*"
                      r"'success'\s*\}\}$")
KEY_LINE = re.compile(r"^([A-Za-z_][\w-]*):(?:[ \t]+(.*))?$")


def version_of(comment):
    words = (comment or "").lstrip("#").split()
    return words[0] if words else ""


def parse_uses(line):
    """None for a line that is not a uses: line; else (kind, value, action, ref, comment) with
    kind 'pin', 'exempt' or 'unparsed'."""
    m = USES_LINE.match(line.rstrip("\r\n"))
    if not m:
        return None
    rest = m.group(1).strip()
    if rest[:1] in ("'", '"'):
        end = rest.find(rest[0], 1)
        if end < 0:
            return ("unparsed", rest, None, None, None)
        value, comment = rest[1:end], rest[end + 1:].strip()
        if comment and not comment.startswith("#"):
            return ("unparsed", rest, None, None, None)
    else:
        mm = re.match(r"^(\S+)(?:\s+(#.*))?$", rest)
        if not mm:
            return ("unparsed", rest, None, None, None)
        value, comment = mm.group(1), mm.group(2) or ""
    if value.startswith(EXEMPT_PREFIXES):
        return ("exempt", value, None, None, None)
    pm = PIN.match(value)
    if not pm:
        return ("unparsed", rest, None, None, None)
    return ("pin", value, pm.group(1), pm.group(3), comment)


# --------------------------------------------------------------------------- CI wiring


def _indent(line):
    return len(line) - len(line.lstrip(" "))


def _scalar(raw):
    """A one-line YAML scalar: the value of a quoted one, or a plain one without its comment."""
    v = (raw or "").strip()
    m = re.match(r"^'((?:[^']|'')*)'\s*(#.*)?$", v)
    if m:
        return m.group(1).replace("''", "'")
    m = re.match(r'^"((?:[^"\\]|\\.)*)"\s*(#.*)?$', v)
    if m:
        return m.group(1)
    return re.sub(r"\s+#.*$", "", v)


def _steps(lines, job, end, significant):
    """The steps of the job whose key is at line index job (its block ends before end): a list
    of (index of the step's '-' line, {key: (line index, inline value, [more-indented lines])}),
    or None when the job has no 'steps:' list."""
    keys = [i for i in range(job + 1, end) if significant(i)]
    if not keys:
        return None
    kind = _indent(lines[keys[0]])
    st = next((i for i in keys if _indent(lines[i]) == kind
               and re.match(r"^\s*steps:\s*(#.*)?$", lines[i])), None)
    if st is None:
        return None
    dash = re.compile(r"^\s*-(?:[ \t]+(\S.*))?$")      # a list item; group 1: its first key
    starts, iind, j = [], None, st + 1
    while j < end:
        if significant(j):
            ind = _indent(lines[j])
            is_item = dash.match(lines[j]) is not None
            if iind is None:
                if not is_item or ind < kind:
                    return None
                iind = ind
            if ind < iind or (ind == iind and not is_item):
                break                                   # the next key of the job
            if ind == iind:
                starts.append(j)
        j += 1
    steps = []
    for k, s in enumerate(starts):
        stop = starts[k + 1] if k + 1 < len(starts) else j
        m = dash.match(lines[s])
        rest = [i for i in range(s + 1, stop) if significant(i)]
        # The column of the step's keys: that of the key after '- ', else of the next line.
        cind = m.start(1) if m.group(1) else (_indent(lines[rest[0]]) if rest else iind + 2)
        entries = ([(s, m.group(1))] if m.group(1) else []) + [
            (i, lines[i][cind:]) for i in range(s + 1, stop)]
        fields, cur = {}, None
        for i, text in entries:
            if i != s and not lines[i].strip():
                continue                                    # blank, also inside a block scalar
            if i != s and _indent(lines[i]) > cind:
                if cur is not None and lines[i].strip():    # a block scalar or nested value
                    fields[cur][2].append(lines[i].strip())
                continue
            km = KEY_LINE.match(text.rstrip()) if i == s or significant(i) else None
            cur = km.group(1) if km else None
            if km:
                fields[cur] = (i, km.group(2) or "", [])
        steps.append((s, fields))
    return steps


def ci_wiring_problems(repo):
    """The problems of the upstream-hooks step of CI_WORKFLOW's CI_JOB job, as
    '<file>:<line>: ...' lines: the job must have a step whose run: is exactly HOOKS_RUN and whose
    if: is GUARD, with <id> the id: of the job's Build step (the step that runs BUILD_SCRIPT),
    which comes before it. GitHub evaluates steps.<id> of a missing or later id as null, so a
    wrong id would skip the step while the job stays green."""
    try:
        with open(os.path.join(repo, *CI_WORKFLOW.split("/")), encoding="utf-8",
                  errors="replace") as fh:
            lines = fh.read().splitlines()
    except OSError as e:
        return [f"{CI_WORKFLOW}: cannot read it ({e.strerror or e}); its {CI_JOB} job must run "
                f"'{HOOKS_RUN}' after the Build step"]

    def where(i):
        return f"{CI_WORKFLOW}:{i + 1}"

    def significant(i):
        s = lines[i].strip()
        return bool(s) and not s.startswith("#")

    def block_end(i):
        j = i + 1
        while j < len(lines) and (not significant(j) or _indent(lines[j]) > _indent(lines[i])):
            j += 1
        return j

    jobs = next((i for i, line in enumerate(lines) if re.match(r"^jobs:\s*(#.*)?$", line)), None)
    if jobs is None:
        return [f"{where(0)}: no top-level 'jobs:'; the {CI_JOB} job must run '{HOOKS_RUN}'"]
    jend = block_end(jobs)
    job_keys = [i for i in range(jobs + 1, jend) if significant(i)]
    jind = _indent(lines[job_keys[0]]) if job_keys else None
    job = next((i for i in job_keys if _indent(lines[i]) == jind
                and re.match(rf"^\s*{CI_JOB}:\s*(#.*)?$", lines[i])), None)
    if job is None:
        return [f"{where(jobs)}: no '{CI_JOB}:' job under 'jobs:'; it must run '{HOOKS_RUN}'"]
    steps = _steps(lines, job, block_end(job), significant)
    if not steps:
        return [f"{where(job)}: the {CI_JOB} job has no 'steps:' list; it must run '{HOOKS_RUN}'"]

    def run_text(fields):
        if "run" not in fields:
            return ""
        _, inline, body = fields["run"]
        return "\n".join([_scalar(inline)] + body)

    problems = []
    hooks = [(s, f) for s, f in steps if HOOKS_SCRIPT in run_text(f)]
    exact = [(s, f) for s, f in hooks if run_text(f) == HOOKS_RUN]
    builds = [(s, f) for s, f in steps
              if BUILD_SCRIPT in run_text(f) and HOOKS_SCRIPT not in run_text(f)]
    if not hooks:
        problems.append(f"{where(job)}: the {CI_JOB} job has no step that runs '{HOOKS_RUN}' "
                        "(the 'Upstream hook tests' step)")
    elif not exact:
        for s, f in hooks:
            problems.append(f"{where(f['run'][0])}: run: is '{run_text(f)}'; the upstream-hooks "
                            f"step must run exactly '{HOOKS_RUN}'")
    build_id = build_line = None
    if not builds:
        problems.append(f"{where(job)}: the {CI_JOB} job has no Build step (a run: of "
                        f"{BUILD_SCRIPT}) whose id: the upstream-hooks guard can name")
    elif len(builds) > 1:
        problems.append(f"{where(builds[1][0])}: {len(builds)} steps of the {CI_JOB} job run "
                        f"{BUILD_SCRIPT} (lines {', '.join(str(s + 1) for s, _ in builds)}); "
                        "the upstream-hooks guard must name one Build step")
    else:
        build_line, fields = builds[0]
        if "id" in fields and _scalar(fields["id"][1]):
            build_id = _scalar(fields["id"][1])
        else:
            problems.append(f"{where(build_line)}: the Build step has no id:; the upstream-hooks "
                            f"step's guard needs it: if: {GUARD}")
    for s, f in exact:
        if "if" not in f:
            problems.append(f"{where(s)}: the upstream-hooks step has no if:; it needs "
                            f"if: {GUARD}, <id> the id: of the Build step")
            continue
        n, inline, body = f["if"]
        value = " ".join([_scalar(inline)] + body)
        m = GUARD_RE.match(value)
        if not m:
            problems.append(f"{where(n)}: if: is '{value}'; the upstream-hooks step needs "
                            f"if: {GUARD}, <id> the id: of the Build step")
        elif build_id is not None and m.group(1) != build_id:
            problems.append(f"{where(n)}: the guard names steps.{m.group(1)}, but the Build "
                            f"step's id: (line {build_line + 1}) is '{build_id}'; "
                            f"steps.{m.group(1)}.outcome is null and would skip the step")
        if build_line is not None and s < build_line:
            problems.append(f"{where(s)}: the upstream-hooks step comes before the Build step "
                            f"(line {build_line + 1}); it must come after it")
    return problems


def check(repo):
    """Checks the .github tree of repo, prints one line per finding and the summary, and
    returns the exit status."""
    github = os.path.join(repo, ".github")
    if not os.path.isdir(github):
        print(f"check-action-pins: {github} is not a directory")
        return 2
    files = sorted(set(glob.glob(os.path.join(github, "**", "*.yml"), recursive=True) +
                       glob.glob(os.path.join(github, "**", "*.yaml"), recursive=True)))
    ours = [f for f in files if os.path.basename(f).startswith("msil-")]
    if not ours:
        print(f"check-action-pins: no msil-*.yml workflow under {github}")
        return 2

    upstream = collections.defaultdict(collections.Counter)
    for f in files:
        if f in ours:
            continue
        with open(f, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                u = parse_uses(line)
                if u and u[0] == "pin":
                    upstream[u[2]][(u[3], version_of(u[4]))] += 1

    bad = checked = unpinned = unparsed = exempt = empty = 0
    for f in ours:
        rel = os.path.relpath(f, repo).replace(os.sep, "/")
        uses = pins = 0
        with open(f, encoding="utf-8", errors="replace") as fh:
            for n, line in enumerate(fh, 1):
                u = parse_uses(line)
                if not u:
                    continue
                uses += 1
                kind, value = u[0], u[1]
                if kind == "exempt":
                    exempt += 1
                    continue
                if kind == "unparsed":
                    print(f"{rel}:{n}: cannot read the uses: line ('{value}'); write "
                          "'uses: <owner>/<repo>[/<path>]@<ref> # <version>', a local './...' "
                          "or a 'docker://' value")
                    unparsed += 1
                    continue
                checked += 1
                pins += 1
                action, ref, ver = u[2], u[3], version_of(u[4])
                seen = upstream.get(action)
                if not seen:
                    print(f"{rel}:{n}: {action}@{ref}: not pinned upstream")
                    unpinned += 1
                elif (ref, ver) not in seen:
                    want = ", ".join(f"{r} # {v} ({c} uses)" for (r, v), c in seen.most_common())
                    print(f"{rel}:{n}: {action}@{ref} # {ver}; upstream: {want}")
                    bad += 1
        if uses and not pins:
            print(f"{rel}: {uses} uses: line(s) but no pin read; the parser may have regressed")
            empty += 1
    wiring = ci_wiring_problems(repo)
    for p in wiring:
        print(p)
    print(f"check-action-pins: {checked} pin(s) in {len(ours)} MSIL workflow(s), "
          f"{bad} mismatched, {unpinned} not pinned upstream, {unparsed} unreadable, "
          f"{exempt} local or docker; {CI_WORKFLOW} upstream-hooks step: "
          f"{f'{len(wiring)} problem(s)' if wiring else 'OK'}")
    return 1 if bad or unparsed or empty or wiring else 0


# --------------------------------------------------------------------------- self-test

_OLD = "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"   # actions/upload-artifact v7.0.1
_NEW = "cf430e030ddbb5b0abf93d22962f4752f3646cd9"   # actions/upload-artifact v7.0.2
_CHECKOUT = "3d3c42e5aac5ba805825da76410c181273ba90b1"
SELF_TEST_UPSTREAM = f"""\
name: upstream
jobs:
  a:
    steps:
      - uses: actions/checkout@{_CHECKOUT} # v7.0.1
      - uses: actions/upload-artifact@{_NEW} # v7.0.2
      - uses: "actions/upload-artifact@{_NEW}" # v7.0.2
      - uses: ./.github/actions/build-container
  b:
    uses: ./.github/workflows/reusable.yml
"""
SELF_TEST_CLEAN = f"""\
name: msil
jobs:
  build:
    steps:
      - name: Checkout
        uses: actions/checkout@{_CHECKOUT} # v7.0.1
      - uses: 'actions/upload-artifact@{_NEW}' # v7.0.2
      - uses: "actions/upload-artifact@{_NEW}" # v7.0.2
      - uses: actions/setup-dotnet@v5 # not used upstream
      - uses: ./.github/actions/local
      - uses: docker://alpine:3.20
  reuse:
    uses: ./.github/workflows/reusable.yml
  windows:
    runs-on: windows-2022
    defaults:
      run:
        shell: pwsh
    steps:
      - name: Build (msil/utils/ci/build.ps1)
        id: build
        run: pwsh -NoProfile -File {BUILD_SCRIPT}

      - name: Test legs
        run: |
          # not the upstream-hooks step
          pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1

      - name: Upstream hook tests (msil/utils/ci/upstream-hooks.ps1)
        if: ${{{{ !cancelled() && steps.build.outcome == 'success' }}}}
        run: {HOOKS_RUN}
"""
_HOOKS_STEP = f"""\
      - name: Upstream hook tests (msil/utils/ci/upstream-hooks.ps1)
        if: ${{{{ !cancelled() && steps.build.outcome == 'success' }}}}
        run: {HOOKS_RUN}
"""
# The defects of the CI wiring (the upstream-hooks step of the windows job): name -> (the
# replacements in the clean workflow, a needle of the output). Its Build step is at line 20.
_GUARD = "${{ !cancelled() && steps.build.outcome == 'success' }}"
SELF_TEST_CI_DEFECTS = {
    "misspelled id in the guard": (
        [("steps.build.outcome", "steps.biuld.outcome")],
        "msil-ci.yml:30: the guard names steps.biuld, but the Build step's id: (line 20) is "
        "'build'"),
    "no id: on the Build step": (
        [("        id: build\n", "")], "msil-ci.yml:20: the Build step has no id:"),
    "blank id: and a misspelled guard": (
        [("        id: build\n", "        id:\n"), ("steps.build.outcome", "steps.biuld.outcome")],
        "msil-ci.yml:20: the Build step has no id:"),
    "deleted upstream-hooks step": (
        [(_HOOKS_STEP, "")], "msil-ci.yml:14: the windows job has no step that runs"),
    "upstream-hooks step moved to another job": (
        [(_HOOKS_STEP, ""), ("      - uses: docker://alpine:3.20\n",
                             "      - uses: docker://alpine:3.20\n" + _HOOKS_STEP)],
        "msil-ci.yml:17: the windows job has no step that runs"),
    "upstream-hooks step before the Build step": (
        [(_HOOKS_STEP, ""), ("    steps:\n      - name: Build", "    steps:\n" + _HOOKS_STEP
                             + "      - name: Build")],
        "the upstream-hooks step comes before the Build step"),
    "changed run: of the upstream-hooks step": (
        [("-MinPass 250", "-MinPass 200")], "the upstream-hooks step must run exactly"),
    "guard without !cancelled()": (
        [(_GUARD, "steps.build.outcome == 'success'")],
        "msil-ci.yml:30: if: is 'steps.build.outcome == 'success''"),
    "no guard": (
        [(f"        if: {_GUARD}\n", "")], "msil-ci.yml:29: the upstream-hooks step has no if:"),
    "no Build step": (
        [(f"        run: pwsh -NoProfile -File {BUILD_SCRIPT}\n", "        run: echo\n")],
        "msil-ci.yml:14: the windows job has no Build step"),
}
# name -> (the line of the clean workflow to replace, its replacement, a needle of the output)
SELF_TEST_DEFECTS = {
    "reverted unquoted pin": (f"'actions/upload-artifact@{_NEW}' # v7.0.2",
                              f"actions/upload-artifact@{_OLD} # v7.0.1",
                              f"actions/upload-artifact@{_OLD} # v7.0.1; upstream: {_NEW}"),
    "stale single-quoted pin": (f"'actions/upload-artifact@{_NEW}' # v7.0.2",
                                f"'actions/upload-artifact@{_OLD}' # v7.0.1",
                                f"actions/upload-artifact@{_OLD} # v7.0.1; upstream: {_NEW}"),
    "stale double-quoted pin": (f'"actions/upload-artifact@{_NEW}" # v7.0.2',
                                f'"actions/upload-artifact@{_OLD}" # v7.0.1',
                                f"actions/upload-artifact@{_OLD} # v7.0.1; upstream: {_NEW}"),
    "stale pin without a version comment": (f"'actions/upload-artifact@{_NEW}' # v7.0.2",
                                            f"actions/upload-artifact@{_OLD}",
                                            f"actions/upload-artifact@{_OLD} # ; upstream:"),
    "unparsable uses: line (no @<ref>)": (f"'actions/upload-artifact@{_NEW}' # v7.0.2",
                                          "actions/upload-artifact # v7.0.2",
                                          "cannot read the uses: line"),
    "unparsable uses: line (unterminated quote)": (
        f'"actions/upload-artifact@{_NEW}" # v7.0.2', f'"actions/upload-artifact@{_NEW} # v7.0.2',
        "cannot read the uses: line"),
}


def _rmtree(path):
    def retry(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=retry)
    else:
        shutil.rmtree(path, onerror=retry)


def self_test():
    failures = []

    def expect(cond, what):
        print(("ok    " if cond else "FAIL  ") + what)
        if not cond:
            failures.append(what)

    script_repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
    tmp = tempfile.mkdtemp(prefix="check-action-pins-")
    try:
        try:
            common = os.path.normcase(os.path.commonpath([tmp, script_repo]))
        except ValueError:                               # another drive
            common = ""
        expect(common != os.path.normcase(script_repo),
               f"the temporary directory {tmp} is outside the repository")

        def run(name, msil_text, upstream_text=SELF_TEST_UPSTREAM, msil_name="msil-ci.yml",
                ci_text=None):
            # ci_text: also write it as msil-ci.yml (when msil_name is another workflow).
            root = os.path.join(tmp, re.sub(r"\W+", "-", name))
            wf = os.path.join(root, ".github", "workflows")
            os.makedirs(wf)
            for fname, text in (("upstream.yml", upstream_text), (msil_name, msil_text),
                                ("msil-ci.yml", ci_text)):
                if text is not None:
                    with open(os.path.join(wf, fname), "w", encoding="utf-8", newline="\n") as f:
                        f.write(text)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = check(root)
            out = buf.getvalue()
            for line in out.splitlines():
                print("      " + line)
            return rc, out

        rc, out = run("clean", SELF_TEST_CLEAN)
        expect(rc == 0 and "4 pin(s) in 1 MSIL workflow(s), 0 mismatched, 1 not pinned "
               "upstream, 0 unreadable, 3 local or docker; .github/workflows/msil-ci.yml "
               "upstream-hooks step: OK" in out,
               f"clean tree (quoted pins, local, reusable and docker uses:, the upstream-hooks "
               f"step of the windows job): exit 0 (exit {rc})")
        for name, (old, new, needle) in SELF_TEST_DEFECTS.items():
            assert SELF_TEST_CLEAN.count(old) == 1, name
            rc, out = run(name, SELF_TEST_CLEAN.replace(old, new))
            expect(rc == 1 and needle in out, f"{name}: exit 1 (exit {rc})")
        for name, (replacements, needle) in SELF_TEST_CI_DEFECTS.items():
            text = SELF_TEST_CLEAN
            for old, new in replacements:
                assert text.count(old) == 1, (name, old)
                text = text.replace(old, new)
            rc, out = run(name, text)
            expect(rc == 1 and needle in out and "upstream-hooks step: OK" not in out,
                   f"CI wiring, {name}: exit 1 (exit {rc})")
        rc, out = run("value on the next line", SELF_TEST_CLEAN.replace(
            "      - uses: docker://alpine:3.20\n", "      - uses:\n          docker://alpine\n"))
        expect(rc == 1 and "cannot read the uses: line" in out,
               f"uses: with its value on the next line: exit 1 (exit {rc})")
        rc, out = run("no pin", "jobs:\n  a:\n    steps:\n      - uses: ./.github/actions/x\n")
        expect(rc == 1 and "but no pin read" in out,
               f"an MSIL workflow with uses: lines but no pin: exit 1 (exit {rc})")
        rc, out = run("upstream quoted", SELF_TEST_CLEAN, SELF_TEST_UPSTREAM.replace(
            f"- uses: actions/upload-artifact@{_NEW} # v7.0.2",
            f"- uses: 'actions/upload-artifact@{_NEW}' # v7.0.2"), "msil-other.yaml",
            SELF_TEST_CLEAN)
        expect(rc == 0, f"quoted upstream pins and a .yaml MSIL workflow: exit 0 (exit {rc})")
        rc, out = run("no msil-ci.yml", SELF_TEST_CLEAN, msil_name="msil-other.yaml")
        expect(rc == 1 and ".github/workflows/msil-ci.yml: cannot read it" in out,
               f"an MSIL workflow but no msil-ci.yml: exit 1 (exit {rc})")
        rc, out = run("no msil workflow", None)
        expect(rc == 2, f"no msil-*.yml workflow: exit 2 (exit {rc})")
    finally:
        _rmtree(tmp)
    expect(not os.path.exists(tmp), "temporary directory removed")
    print(f"check-action-pins self-test: {'PASS' if not failures else 'FAIL'} "
          f"({len(failures)} failure(s))")
    return 0 if not failures else 1


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) > 1 or (argv and argv[0].startswith("-")):
        print("usage: check-action-pins.py [<repo>] | --self-test")
        return 2
    default_repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
    return check(os.path.abspath(argv[0] if argv else default_repo))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
