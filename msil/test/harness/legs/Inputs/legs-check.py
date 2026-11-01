#!/usr/bin/env python3
# ===-- legs-check.py - Check the legs that lit-legs.ps1/.sh describe ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check the output of msil/utils/ci/lit-legs.ps1 -Describe and -List and
lit-legs.sh --describe and --list against plan 5.17.6, for describe.test,
describe-sh.test, exit-tag.test and exit-tag-sh.test.

    legs-check.py describe ps1|sh DESCRIBE [--plant FAULT]
        DESCRIBE is the -Describe (--describe) output, which must be nothing
        but JSON: {"version": 1, "script": "lit-legs.ps1"|"lit-legs.sh",
        "legs": [...]}, each leg {name, tag, kind, runs}, a non-lit leg with
        no runs, each lit run {paths, lit, params, pipeline, prog_pipeline,
        triple, runner, also_run} with the derived fields of 5.17.6 and
        msil_require_verify, _exec, _link and _hosted=1 among its params.
        For lit-legs.ps1 the M24 rows of 5.17.6 (net-core, p3-full, harness,
        wsl-p3, scale, ctest-wsl; the changed wsl-net10, lto, lto-wsl,
        expected, expected-linux and ctest), with $p3Tests read from
        'check-matrix.py --p3-tests <repo>': p3-full and wsl-p3's run 1 hold
        every one of those paths, and the --filter-out of net-core and of
        wsl-p3's run 2 matches exactly those tests' full lit names (and, for
        net-core, still every Programs, harness, M3 and spill-limits test).
        For lit-legs.sh: none of the Quick or WSL legs of lit-legs.ps1,
        msil_glibc_sysroot on every lit run, M3 in lto, .NET 10 in ctest.
        --plant changes the JSON first, so that the check must fail (the
        failure path): drop-p3-path (wsl-p3's run 1 loses its last path),
        keep-p3 (wsl-p3's run 2 loses its --filter-out), no-hosted (harness,
        for lit-legs.sh net8, loses msil_require_hosted), wrong-prog-pipeline
        (lto's run says prog_pipeline obj); for lit-legs.sh also no-glibc
        (net8 loses msil_glibc_sysroot) and wsl-leg (a copy of ctest named
        ctest-wsl joins the legs).
    legs-check.py tags ps1|sh LIST DESCRIBE [--plant LEG=TAG]
        LIST (-List, --list) and DESCRIBE name the same legs in the same
        order, and each LIST line is 'name' or 'name (<switch>)' for the
        leg's tag: lit-legs.ps1 '' none, Wsl -Wsl, Quick -Quick, Full -Full,
        Exit -Exit; lit-legs.sh '' none, Full --full, Exit --exit, and Quick
        none (it has no switch: its Quick leg runs only when --leg names it).
    legs-check.py rows ps1|sh DESCRIBE ROWS [--wsl] [--full] [--exit]
                  [--plant LEG=TAG]
        ROWS (MSIL_CI_RESULTS of one run: 'lit-legs<TAB>name<TAB>RESULT...')
        holds exactly the legs that run with those switches, in DESCRIBE's
        order: the untagged legs, the Wsl legs with --wsl, the Full legs with
        --full and the Exit legs with --exit, where for lit-legs.ps1 an Exit
        leg named *-wsl also needs --wsl; never a Quick leg.
    legs-check.py calls DESCRIBE LEG CALLS [--run K]...
        CALLS is the stand-in lit's log (ci-legs.py: 'arg: ' and 'rsp: '
        lines) of one run of lit-legs.ps1 -Leg LEG (lit-legs.sh --leg LEG):
        its lit invocations are LEG's runs (or the runs K), in order, each
        with the run's paths and exactly its params ('--param=k=v', or
        lit-legs.sh's '--param' 'k=v') and lit arguments, so -Describe
        (--describe) shows what the run passes. A path inside WSL's
        /mnt/<drive>/ counts as the Windows path it names.

--plant LEG=TAG gives LEG the tag TAG in DESCRIBE first. Prints
'legs-check: ...: OK' lines; exits 0 when everything holds, 1 with every
problem ('legs-check: error: ...'), 2 on a usage error.
"""

import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 5))
CHECK_MATRIX = os.path.join(
    REPO, "msil", "test", "M2", "08-semantics", "Inputs", "check-matrix.py"
)
REQUIRES = (
    "msil_require_verify",
    "msil_require_exec",
    "msil_require_link",
    "msil_require_hosted",
)
DEFAULT_TRIPLE = {
    "ps1": "x86_64-pc-windows-msvc-msil",
    "sh": "x86_64-unknown-linux-gnu-msil",
}
KINDS = {
    "ps1": {"lit", "expected", "utc", "encoder", "obj-text"},
    "sh": {"lit", "expected", "utc", "upstream"},
}
SWITCH = {
    "ps1": {"": "", "Wsl": "-Wsl", "Quick": "-Quick", "Full": "-Full", "Exit": "-Exit"},
    "sh": {"": "", "Quick": "", "Full": "--full", "Exit": "--exit"},
}
FAULTS = {
    "ps1": ("drop-p3-path", "keep-p3", "no-hosted", "wrong-prog-pipeline"),
    "sh": ("no-hosted", "wrong-prog-pipeline", "no-glibc", "wsl-leg"),
}


class Usage(Exception):
    pass


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    try:
        return json.loads(text)
    except ValueError as e:
        raise Usage("%s is not JSON alone: %s" % (path, e))


def legs_by_name(data):
    return {leg.get("name"): leg for leg in data.get("legs", [])}


def plant_tag(data, spec):
    name, sep, tag = spec.partition("=")
    if not sep or name not in legs_by_name(data):
        raise Usage("--plant %s: not LEG=TAG for a leg of the JSON" % spec)
    legs_by_name(data)[name]["tag"] = tag


def p3_tests():
    res = subprocess.run(
        [sys.executable, CHECK_MATRIX, "--p3-tests", REPO],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode:
        raise Usage(
            "check-matrix.py --p3-tests exited %d: %s"
            % (res.returncode, res.stderr.decode("utf-8", "replace"))
        )
    return [l.strip() for l in res.stdout.decode("utf-8").splitlines() if l.strip()]


def lit_name(path):
    """The full lit name of a test path below msil/test."""
    assert path.startswith("msil/test/"), path
    return "MSIL :: " + path[len("msil/test/") :]


def filter_out(run):
    values = [
        a[len("--filter-out=") :] for a in run["lit"] if a.startswith("--filter-out=")
    ]
    return values


def check_run_shape(script, leg, k, run, problems):
    where = "%s run %d" % (leg["name"], k)
    keys = {
        "paths",
        "lit",
        "params",
        "pipeline",
        "prog_pipeline",
        "triple",
        "runner",
        "also_run",
    }
    if set(run) != keys:
        problems.append("%s: keys %s, not %s" % (where, sorted(run), sorted(keys)))
        return
    if not run["paths"] or not all(isinstance(p, str) and p for p in run["paths"]):
        problems.append("%s: paths %r" % (where, run["paths"]))
    for p in run["paths"]:
        if "\\" in p or os.path.isabs(p) or not os.path.exists(os.path.join(REPO, p)):
            problems.append(
                "%s: path %r is not a repository-relative path that exists" % (where, p)
            )
    params = run["params"]
    for r in REQUIRES:
        if params.get(r) != "1":
            problems.append("%s: params lack %s=1" % (where, r))
    pipeline = params.get("msil_pipeline", "obj")
    if run["pipeline"] != pipeline:
        problems.append(
            "%s: pipeline %r, msil_pipeline %r" % (where, run["pipeline"], pipeline)
        )
    prog = "lto" if pipeline == "lto" else "obj"
    if run["prog_pipeline"] != prog:
        problems.append(
            "%s: prog_pipeline %r, not %r" % (where, run["prog_pipeline"], prog)
        )
    triple = params.get("msil_triple", DEFAULT_TRIPLE[script])
    if run["triple"] != triple:
        problems.append("%s: triple %r, not %r" % (where, run["triple"], triple))
    runner = params.get("msil_runner", "local")
    if run["runner"] != runner:
        problems.append("%s: runner %r, not %r" % (where, run["runner"], runner))
    also = [a for a in params.get("msil_also_run", "").split(",") if a]
    if run["also_run"] != also:
        problems.append("%s: also_run %r, not %r" % (where, run["also_run"], also))


def check_shape(script, data, problems):
    if set(data) != {"version", "script", "legs"} or data["version"] != 1:
        problems.append("the top level is not {version: 1, script, legs}")
    want = "lit-legs.%s" % script
    if data.get("script") != want:
        problems.append("script %r, not %r" % (data.get("script"), want))
    names = []
    for leg in data.get("legs", []):
        if set(leg) != {"name", "tag", "kind", "runs"}:
            problems.append("leg %r: keys %s" % (leg.get("name"), sorted(leg)))
            continue
        names.append(leg["name"])
        if leg["tag"] not in SWITCH[script]:
            problems.append("leg %s: tag %r" % (leg["name"], leg["tag"]))
        if leg["kind"] not in KINDS[script]:
            problems.append("leg %s: kind %r" % (leg["name"], leg["kind"]))
        if leg["kind"] != "lit" and leg["runs"]:
            problems.append("leg %s: kind %s with runs" % (leg["name"], leg["kind"]))
        if leg["kind"] == "lit" and not leg["runs"]:
            problems.append("leg %s: a lit leg without runs" % leg["name"])
        for k, run in enumerate(leg["runs"], 1):
            check_run_shape(script, leg, k, run, problems)
    if len(set(names)) != len(names):
        problems.append("a leg name appears twice")


def expect_params(where, run, want, problems, exact=True):
    params = {k: v for k, v in run["params"].items() if k not in REQUIRES}
    if exact and params != want:
        problems.append(
            "%s: params %s, not %s (plus the msil_require_*)" % (where, params, want)
        )
    elif not exact:
        for k, v in want.items():
            if params.get(k) != v:
                problems.append(
                    "%s: params %s=%r, not %r" % (where, k, params.get(k), v)
                )


def check_p3_filter(where, regex, p3, problems, net_core=False):
    try:
        rx = re.compile(regex)
    except re.error as e:
        problems.append("%s: --filter-out %r: %s" % (where, regex, e))
        return
    for path in p3:
        name = lit_name(path)
        if not rx.search(name):
            problems.append("%s: --filter-out does not match %s" % (where, name))
        if rx.search(name + "x") or rx.search(name.replace(".", "X")):
            problems.append("%s: --filter-out matches more than %s" % (where, name))
    others = ["MSIL :: M1/03-control/loops.c", "MSIL :: M2/00-link/basic.test"]
    if net_core:
        for name in (
            "MSIL :: Programs/zlib/zlib.test",
            "MSIL :: harness/timeout.test",
            "MSIL :: M3/10-hosted/a.c",
            "MSIL :: M2/00-link/spill-limits.test",
        ):
            if not rx.search(name):
                problems.append("%s: --filter-out no longer matches %s" % (where, name))
    for name in others:
        if rx.search(name):
            problems.append("%s: --filter-out matches %s" % (where, name))


def check_ps1_rows(data, p3, problems):
    legs = legs_by_name(data)
    need = [
        "net-core",
        "p3-full",
        "harness",
        "wsl-p3",
        "scale",
        "ctest-wsl",
        "net8",
        "wsl-net10",
        "lto",
        "lto-wsl",
        "expected",
        "expected-linux",
        "ctest",
    ]
    missing = [n for n in need if n not in legs]
    if missing:
        problems.append("legs missing: %s" % ", ".join(missing))
        return

    def one(name, tag, runs=1):
        leg = legs[name]
        if leg["tag"] != tag or leg["kind"] != "lit" or len(leg["runs"]) != runs:
            problems.append(
                "%s: tag %r, kind %r, %d runs; not %r, lit, %d"
                % (name, leg["tag"], leg["kind"], len(leg["runs"]), tag, runs)
            )
            return None
        return leg["runs"]

    r = one("net-core", "Quick")
    if r:
        if r[0]["paths"] != legs["net8"]["runs"][0]["paths"]:
            problems.append("net-core: paths are not $checkMsil (net8's paths)")
        expect_params(
            "net-core",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_dotnet_runtime": "10",
                "msil_run_netfx": "1",
                "msil_equiv": "1",
                "msil_require_csc": "1",
            },
            problems,
        )
        fo = filter_out(r[0])
        if len(fo) != 1 or len(r[0]["lit"]) != 1:
            problems.append("net-core: lit %r, not one --filter-out" % r[0]["lit"])
        else:
            check_p3_filter("net-core", fo[0], p3, problems, net_core=True)
    r = one("p3-full", "Quick")
    if r:
        if r[0]["paths"] != ["msil/test/M3"] + p3 or r[0]["lit"]:
            problems.append(
                "p3-full: paths %r, lit %r; not msil/test/M3 and $p3Tests %r"
                % (r[0]["paths"], r[0]["lit"], p3)
            )
        expect_params(
            "p3-full",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_dotnet_runtime": "10",
                "msil_native_oracle": "required",
                "msil_run_netfx": "1",
                "msil_equiv": "1",
                "msil_also_run": "10+tc0",
                "msil_require_csc": "1",
            },
            problems,
        )
    r = one("harness", "Quick")
    if r:
        if r[0]["paths"] != ["msil/test/harness"] or r[0]["lit"]:
            problems.append("harness: paths %r, lit %r" % (r[0]["paths"], r[0]["lit"]))
        expect_params(
            "harness",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_dotnet_runtime": "10",
                "msil_native_oracle": "required",
                "msil_run_netfx": "1",
                "msil_require_csc": "1",
            },
            problems,
        )
    r = one("wsl-p3", "Quick", runs=2)
    if r:
        if r[0]["paths"] != ["msil/test/M3"] + p3 or r[0]["lit"]:
            problems.append(
                "wsl-p3 run 1: paths %r, lit %r; not msil/test/M3 and $p3Tests %r"
                % (r[0]["paths"], r[0]["lit"], p3)
            )
        if r[1]["paths"] != ["msil/test/M2/08-semantics", "msil/test/M1/11-programs"]:
            problems.append("wsl-p3 run 2: paths %r" % r[1]["paths"])
        fo = filter_out(r[1])
        if len(fo) != 1 or len(r[1]["lit"]) != 1:
            problems.append(
                "wsl-p3 run 2: lit %r, not one --filter-out ($p3Out)" % r[1]["lit"]
            )
        else:
            check_p3_filter("wsl-p3 run 2", fo[0], p3, problems)
        for k, run in enumerate(r, 1):
            expect_params(
                "wsl-p3 run %d" % k,
                run,
                {
                    "msil_pipeline": "obj",
                    "msil_runner": "wsl",
                    "msil_triple": "x86_64-unknown-linux-gnu-msil",
                    "msil_dotnet_runtime": "10",
                    "msil_native_oracle": "required",
                },
                problems,
                exact=False,
            )
            for key in ("msil_linux_sysroot", "msil_glibc_sysroot"):
                if not run["params"].get(key):
                    problems.append("wsl-p3 run %d: no %s ($e7h)" % (k, key))
        if r[0]["also_run"] != ["10+tc0"] or r[1]["also_run"]:
            problems.append(
                "wsl-p3: also_run %r and %r, not ['10+tc0'] and []"
                % (r[0]["also_run"], r[1]["also_run"])
            )
    r = one("scale", "Quick")
    if r:
        if r[0]["paths"] != ["msil/test/M2/00-link/spill-limits.test"]:
            problems.append("scale: paths %r" % r[0]["paths"])
        expect_params(
            "scale",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_dotnet_runtime": "10",
                "msil_scale_tests": "1",
            },
            problems,
        )
    r = one("ctest-wsl", "Wsl")
    if r:
        if r[0]["paths"] != ["msil/test/Programs/c-testsuite"]:
            problems.append("ctest-wsl: paths %r" % r[0]["paths"])
        expect_params(
            "ctest-wsl",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_runner": "wsl",
                "msil_dotnet_runtime": "10",
                "msil_native_oracle": "required",
                "msil_also_run": "8",
                "msil_require_corpus": "1",
            },
            problems,
            exact=False,
        )
        if not r[0]["params"].get("msil_glibc_sysroot"):
            problems.append("ctest-wsl: no msil_glibc_sysroot ($e7h)")
    r = one("ctest", "")
    if r:
        expect_params(
            "ctest",
            r[0],
            {
                "msil_pipeline": "obj",
                "msil_dotnet_runtime": "10",
                "msil_native_oracle": "required",
                "msil_run_netfx": "1",
                "msil_also_run": "8",
                "msil_require_corpus": "1",
            },
            problems,
            exact=False,
        )
    for name, paths, glibc in (
        ("wsl-net10", ["msil/test/M3"], True),
        ("lto", ["msil/test/M3"], False),
        (
            "lto-wsl",
            [
                "msil/test/M2/01-archive",
                "msil/test/M2/03-native",
                "msil/test/M2/04-shared",
                "msil/test/M2/05-refs",
                "msil/test/M2/07-equiv",
                "msil/test/M3",
            ],
            True,
        ),
    ):
        r = legs[name]["runs"]
        if len(r) != 1:
            problems.append("%s: %d runs" % (name, len(r)))
            continue
        for p in paths:
            if p not in r[0]["paths"]:
                problems.append("%s: paths lack %s" % (name, p))
        if glibc and not r[0]["params"].get("msil_glibc_sysroot"):
            problems.append("%s: no msil_glibc_sysroot" % name)
    for name in ("expected", "expected-linux"):
        if legs[name]["kind"] != "expected":
            problems.append("%s: kind %r" % (name, legs[name]["kind"]))


def check_sh_rows(data, problems):
    legs = legs_by_name(data)
    for name in legs:
        if (
            name
            in (
                "net-core",
                "p3-full",
                "harness",
                "wsl-p3",
                "scale",
                "ctest-wsl",
                "wsl-quick",
            )
            or name.endswith("-wsl")
            or name.startswith("wsl-")
        ):
            problems.append("lit-legs.sh has the Quick or WSL leg %s" % name)
    for leg in data["legs"]:
        for k, run in enumerate(leg["runs"], 1):
            if not run["params"].get("msil_glibc_sysroot"):
                problems.append("%s run %d: no msil_glibc_sysroot" % (leg["name"], k))
    if "lto" not in legs or "msil/test/M3" not in legs["lto"]["runs"][0]["paths"]:
        problems.append("lto: no msil/test/M3")
    if "ctest" not in legs:
        problems.append("no ctest leg")
    else:
        expect_params(
            "ctest",
            legs["ctest"]["runs"][0],
            {"msil_dotnet_runtime": "10", "msil_also_run": "8"},
            problems,
            exact=False,
        )
    if legs.get("expected", {}).get("kind") != "expected":
        problems.append("expected: not kind expected")


def plant_fault(script, data, fault):
    if fault not in FAULTS[script]:
        raise Usage(
            "--plant %s: for lit-legs.%s one of %s"
            % (fault, script, ", ".join(FAULTS[script]))
        )
    legs = legs_by_name(data)
    try:
        if fault == "drop-p3-path":
            legs["wsl-p3"]["runs"][0]["paths"].pop()
        elif fault == "keep-p3":
            legs["wsl-p3"]["runs"][1]["lit"] = []
        elif fault == "no-hosted":
            del legs["harness" if script == "ps1" else "net8"]["runs"][0]["params"][
                "msil_require_hosted"
            ]
        elif fault == "wrong-prog-pipeline":
            legs["lto"]["runs"][0]["prog_pipeline"] = "obj"
        elif fault == "no-glibc":
            del legs["net8"]["runs"][0]["params"]["msil_glibc_sysroot"]
        elif fault == "wsl-leg":
            copy = json.loads(json.dumps(legs["ctest"]))
            copy["name"] = "ctest-wsl"
            data["legs"].append(copy)
    except (KeyError, IndexError) as e:
        raise Usage("--plant %s: the JSON has no %s to change" % (fault, e))


def cmd_describe(script, path, plant):
    data = load_json(path)
    if plant:
        plant_fault(script, data, plant)
    problems = []
    check_shape(script, data, problems)
    p3 = []
    if not problems:
        if script == "ps1":
            p3 = p3_tests()
            if not p3:
                problems.append("check-matrix.py --p3-tests printed no path")
            check_ps1_rows(data, p3, problems)
        else:
            check_sh_rows(data, problems)
    runs = sum(len(l["runs"]) for l in data.get("legs", []))
    if not problems:
        print(
            "legs-check: %s: %d legs, %d lit runs: OK"
            % (data["script"], len(data["legs"]), runs)
        )
        if p3:
            print("legs-check: $p3Tests: %s" % " ".join(p3))
    return problems


def list_lines(path):
    with open(path, encoding="utf-8-sig") as f:
        return [l.rstrip() for l in f.read().splitlines() if l.strip()]


def cmd_tags(script, list_path, describe_path, plant):
    data = load_json(describe_path)
    if plant:
        plant_tag(data, plant)
    problems = []
    listed = []
    for line in list_lines(list_path):
        m = re.fullmatch(r"(\S+)(?: \((-\S+)\))?", line)
        if not m:
            problems.append("list line %r is not 'name' or 'name (<switch>)'" % line)
            continue
        listed.append((m.group(1), m.group(2) or ""))
    legs = [(l["name"], l["tag"]) for l in data["legs"]]
    if [n for n, _ in listed] != [n for n, _ in legs]:
        problems.append(
            "the list names %s, the JSON %s"
            % ([n for n, _ in listed], [n for n, _ in legs])
        )
    tags = dict(legs)
    for name, switch in listed:
        if name in tags and SWITCH[script].get(tags[name]) != switch:
            problems.append(
                "leg %s: listed with %r, its tag %r"
                % (name, switch or "no switch", tags[name])
            )
    if not problems:
        counts = {}
        for _, tag in legs:
            counts[tag or "untagged"] = counts.get(tag or "untagged", 0) + 1
        exit_legs = [n for n, t in legs if t == "Exit"]
        print(
            "legs-check: %d legs, the list's tags are the JSON's (%s); Exit legs: %s"
            % (
                len(legs),
                ", ".join("%s %d" % kv for kv in sorted(counts.items())),
                ", ".join(exit_legs) or "none",
            )
        )
    return problems


def cmd_rows(script, describe_path, rows_path, wsl, full, exit_, plant):
    data = load_json(describe_path)
    if plant:
        plant_tag(data, plant)
    want = []
    for leg in data["legs"]:
        tag, name = leg["tag"], leg["name"]
        if tag == "" or (tag == "Wsl" and wsl) or (tag == "Full" and full):
            want.append(name)
        elif (
            tag == "Exit"
            and exit_
            and (script == "sh" or wsl or not name.endswith("-wsl"))
        ):
            want.append(name)
    with open(rows_path, encoding="utf-8-sig") as f:
        rows = [l.split("\t") for l in f.read().splitlines() if l.strip()]
    got = [r[1] for r in rows if len(r) >= 3 and r[0] == "lit-legs"]
    problems = []
    if got != want:
        problems.append("the run's legs %s, not %s" % (got, want))
    if not problems:
        switches = " ".join(
            s for s, on in (("wsl", wsl), ("full", full), ("exit", exit_)) if on
        )
        exit_legs = [
            l["name"] for l in data["legs"] if l["tag"] == "Exit" and l["name"] in got
        ]
        print(
            "legs-check: %d rows, the legs of %s; Exit legs among them: %s"
            % (len(got), switches or "the default set", ", ".join(exit_legs) or "none")
        )
    return problems


def invocations(calls_path):
    """[[arg...]] of the stand-in lit's log, response file lines expanded and
    lit-legs.sh's '--param' 'k=v' joined into '--param=k=v'."""
    calls, current = [], None
    with open(calls_path, encoding="utf-8") as f:
        for line in f.read().splitlines():
            kind, _, value = line.partition(": ")
            if kind == "arg" and value == "-sv":
                current = []
                calls.append(current)
            if current is None:
                continue
            if kind == "rsp" or (kind == "arg" and not value.startswith("@")):
                if current and current[-1] == "--param":
                    current[-1] = "--param=" + value
                else:
                    current.append(value)
    return calls


def repo_relative(path):
    path = path.replace("\\", "/")
    m = re.match(r"/mnt/([a-zA-Z])(/.*)?$", path)
    if m and os.name == "nt":
        path = "%s:%s" % (m.group(1).upper(), m.group(2) or "/")
    root = REPO.replace("\\", "/").rstrip("/") + "/"
    if path.lower().startswith(root.lower()):
        return path[len(root) :]
    return path


def cmd_calls(describe_path, leg_name, calls_path, runs):
    data = load_json(describe_path)
    legs = legs_by_name(data)
    if leg_name not in legs:
        raise Usage("no leg %s in the JSON" % leg_name)
    leg = legs[leg_name]
    picked = runs or list(range(1, len(leg["runs"]) + 1))
    calls = invocations(calls_path)
    problems = []
    if len(calls) != len(picked):
        problems.append("%d lit invocations, %d runs" % (len(calls), len(picked)))
    for k, call in zip(picked, calls):
        run = leg["runs"][k - 1]
        params = {}
        lit = []
        paths = []
        for a in call:
            if a.startswith("--param="):
                key, _, value = a[len("--param=") :].partition("=")
                params[key] = value
            elif (
                a.startswith("--xunit-xml-output=")
                or a in ("-sv", "-j1")
                or a.startswith("-j")
            ):
                continue
            elif a.startswith("-"):
                lit.append(a)
            else:
                paths.append(repo_relative(a))
        if params != run["params"]:
            problems.append(
                "%s run %d: passes params %s, the JSON says %s"
                % (leg_name, k, params, run["params"])
            )
        if lit != run["lit"]:
            problems.append(
                "%s run %d: passes lit arguments %s, the JSON says %s"
                % (leg_name, k, lit, run["lit"])
            )
        if paths != run["paths"]:
            problems.append(
                "%s run %d: passes paths %s, the JSON says %s"
                % (leg_name, k, paths, run["paths"])
            )
    if not problems:
        print(
            "legs-check: %s: %d lit invocations pass the paths, params and lit arguments of runs %s"
            % (leg_name, len(calls), ",".join(map(str, picked)))
        )
    return problems


def main(argv):
    try:
        if len(argv) >= 3 and argv[0] == "describe" and argv[1] in ("ps1", "sh"):
            rest = argv[3:]
            plant = None
            if rest[:1] == ["--plant"] and len(rest) == 2:
                plant = rest[1]
            elif rest:
                raise Usage("describe: unexpected %s" % " ".join(rest))
            problems = cmd_describe(argv[1], argv[2], plant)
        elif len(argv) >= 4 and argv[0] == "tags" and argv[1] in ("ps1", "sh"):
            rest = argv[4:]
            plant = rest[1] if rest[:1] == ["--plant"] and len(rest) == 2 else None
            if rest and not plant:
                raise Usage("tags: unexpected %s" % " ".join(rest))
            problems = cmd_tags(argv[1], argv[2], argv[3], plant)
        elif len(argv) >= 4 and argv[0] == "rows" and argv[1] in ("ps1", "sh"):
            rest = argv[4:]
            flags = {"--wsl": False, "--full": False, "--exit": False}
            plant = None
            while rest:
                if rest[0] in flags:
                    flags[rest[0]] = True
                    rest = rest[1:]
                elif rest[0] == "--plant" and len(rest) >= 2:
                    plant, rest = rest[1], rest[2:]
                else:
                    raise Usage("rows: unexpected %s" % rest[0])
            problems = cmd_rows(
                argv[1],
                argv[2],
                argv[3],
                flags["--wsl"],
                flags["--full"],
                flags["--exit"],
                plant,
            )
        elif len(argv) >= 4 and argv[0] == "calls":
            runs = []
            rest = argv[4:]
            while rest:
                if rest[0] == "--run" and len(rest) >= 2 and rest[1].isdigit():
                    runs.append(int(rest[1]))
                    rest = rest[2:]
                else:
                    raise Usage("calls: unexpected %s" % rest[0])
            problems = cmd_calls(argv[1], argv[2], argv[3], runs)
        else:
            raise Usage(
                "usage: describe ps1|sh FILE [--plant FAULT] | tags ps1|sh LIST FILE "
                "[--plant LEG=TAG] | rows ps1|sh FILE ROWS [--wsl] [--full] [--exit] "
                "[--plant LEG=TAG] | calls FILE LEG CALLS [--run K]..."
            )
    except Usage as e:
        sys.stderr.write("legs-check: %s\n" % e)
        return 2
    for p in problems:
        print("legs-check: error: %s" % p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
