#!/usr/bin/env python3
# ===-- calibrate.py - The load factor of a gate run -----------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Measure how loaded the development host is, for the load-normalized
minutes of a milestone gate (plan 5.17.8; steps G2c and G4c of 5.17.7).

    python msil/utils/ci/calibrate.py --label start|end [--build DIR] [--out FILE]
                                      [--calibration FILE]
    python msil/utils/ci/calibrate.py --combine START.json END.json
    python msil/utils/ci/calibrate.py --self-test

--label runs the calibration composite, the test of msil/utils/ci/calibration.json
(--calibration; msil/test/M1/03-control/loops.c) with that file's lit
parameters, alone and at -j1, through the command of the gate steps:

    pwsh -NoProfile -File msil/utils/build/msil-lit.ps1 -BuildDir <build>
         -LitArgs:"-sv -j1 @<build>/lit-results/calibration-<label>.params.rsp" <test>

The response file holds one '--param=<k>=<v>' line per parameter and
'--xunit-xml-output=<build>/lit-results/calibration-<label>.xml'; lit's output
goes to calibration-<label>.log. The test's time is read from lit's xunit
file: 'seconds'. The factor is seconds / quiet_seconds (calibration.json's
time of the same composite on a quiet host). The test's CPU time is the CPU
of that lit invocation minus the CPU of a configuration-only invocation with
the same parameters (calibration-<label>-config.params.rsp: a --filter that
matches no test, and --allow-empty-runs), both measured in a Windows job
object that this process enters, so the CPU of every process the
invocations start counts. The configuration-only invocation runs first, and
once more when lit printed 'probe cache: miss' (the probe cache of
lit.cfg.py, plan 5.17.10), so that both measured invocations find the cache
in the same state. Win32_Processor.LoadPercentage, read just before the
test's invocation, is recorded as 'load_percent'.

It writes <build>/lit-results/calibration-<label>.json, or --out FILE (whose
missing parent directories it creates; gates write under
C:\\llvm-msil-scratch\\milestones\\M<n>\\ before any other step has made the
directory), with 'seconds', 'quiet_seconds', 'factor', 'test_cpu_seconds' (the
measurement), 'load_percent' and the inputs, and prints

    calibration <label>: <seconds> s against <quiet_seconds> s quiet, factor <factor>

When the measured test CPU differs from calibration.json's test_cpu_seconds
by more than 30%, it also prints a warning: load inflates CPU by up to 1.3x
(tg risk 1), and a harness change that moves the composite's CPU for good
updates calibration.json (plan 5.17.8). The output file is removed before
the measurement, so a failed run leaves none behind for --combine. Exit
status: 0; 1 when the test does not pass (or a lit invocation fails); 2 for
a usage error, a missing or invalid calibration.json, or a host that is not
Windows (the job object and msil-lit.ps1 are Windows-only; --combine and
--self-test run anywhere).

--combine prints 'gate load factor <F>' with F = max(1.0, (factor_start +
factor_end) / 2), the factors of the two files; a gate step's normalized
minutes are its raw minutes / F (plan 5.17.7's budget).

--self-test checks the measurement with synthetic files (calibration.json,
lit's xunit file, a stand-in lit invocation and load probe) in a temporary
directory outside the repository, an --out two levels below a directory
that does not exist among them, and --combine; it exits 0, or 1 naming each
failed case. Python 3 standard library only.
"""

import argparse
import contextlib
import datetime
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

REPO = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
)
DEFAULT_CALIBRATION = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "calibration.json"
)
CPU_TOLERANCE = 0.30
# A --filter that matches no lit name (they all contain ' :: ').
CONFIG_FILTER = "^calibrate configuration only$"
PROBE_MISS = "probe cache: miss"
LOAD_COMMAND = (
    "(Get-CimInstance Win32_Processor | Measure-Object -Property LoadPercentage "
    "-Average).Average"
)


class Usage(Exception):
    """Exit status 2."""


def default_build():
    if os.environ.get("MSIL_BUILD_DIR"):
        return os.path.abspath(os.environ["MSIL_BUILD_DIR"])
    return os.path.join(REPO, "build")


def number(value, what, positive=True):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise Usage("%s is not a number" % what)
    if positive and value <= 0:
        raise Usage("%s must be positive" % what)
    return float(value)


def load_calibration(path):
    """calibration.json as a dict with 'test', 'params' (a list of 'k=v'),
    'quiet_seconds' and 'test_cpu_seconds' (None when the file has none)."""
    try:
        with open(path, encoding="utf-8-sig") as f:
            data = json.load(f)
    except FileNotFoundError:
        raise Usage(
            "%s does not exist (the harness-cost WP of M24 writes it, plan 5.17.8)"
            % path
        )
    except (OSError, ValueError) as e:
        raise Usage("cannot read %s: %s" % (path, e))
    if not isinstance(data, dict) or data.get("version") != 1:
        raise Usage("%s is not a calibration file of version 1 (plan 5.17.8)" % path)
    test = data.get("test")
    if not isinstance(test, str) or not test.startswith("msil/test/") or "\\" in test:
        raise Usage("%s: 'test' is not a path under msil/test" % path)
    params = data.get("params")
    if isinstance(params, dict):
        params = ["%s=%s" % (k, v) for k, v in params.items()]
    if (
        not isinstance(params, list)
        or not params
        or not all(
            isinstance(p, str) and "=" in p and not any(c in p for c in "\r\n")
            for p in params
        )
    ):
        raise Usage("%s: 'params' is not a list of '<name>=<value>' parameters" % path)
    quiet = number(data.get("quiet_seconds"), "%s: 'quiet_seconds'" % path)
    cpu = data.get("test_cpu_seconds")
    cpu = None if cpu is None else number(cpu, "%s: 'test_cpu_seconds'" % path)
    return {
        "test": test,
        "params": params,
        "quiet_seconds": quiet,
        "test_cpu_seconds": cpu,
    }


def testcase_names(test):
    """(classname, name) of a test under msil/test in lit's xunit file."""
    below = test[len("msil/test/") :]
    directory, _, name = below.rpartition("/")
    return ("MSIL." + directory.replace(".", "_") if directory else "MSIL.MSIL"), name


def read_test_time(xml_path, test):
    """(passed, seconds, why) of the test in lit's xunit file."""
    classname, name = testcase_names(test)
    try:
        root = ET.parse(xml_path).getroot()
    except (OSError, ET.ParseError) as e:
        return False, None, "cannot read %s: %s" % (xml_path, e)
    for case in root.iter("testcase"):
        if case.get("classname") == classname and case.get("name") == name:
            try:
                seconds = float(case.get("time", ""))
            except ValueError:
                return False, None, "%s: no time for %s" % (xml_path, test)
            for child in case:
                if child.tag in ("failure", "error"):
                    return False, seconds, "%s failed" % test
                if child.tag == "skipped":
                    return (
                        False,
                        seconds,
                        "%s did not run (%s)" % (test, child.get("message", "skipped")),
                    )
            return True, seconds, ""
    return False, None, "%s has no testcase for %s" % (xml_path, test)


class JobCpu:
    """The CPU time (user + kernel) of this process and every process it
    starts afterwards: a Windows job object that this process enters (as
    llvm/docs/MSIL/recon/p3/tests-gates-p3-files/scripts/jobrun.py measures)."""

    def __init__(self):
        import ctypes
        import ctypes.wintypes as wt

        self.ctypes = ctypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateJobObjectW.restype = wt.HANDLE
        k32.CreateJobObjectW.argtypes = (ctypes.c_void_p, wt.LPCWSTR)
        k32.GetCurrentProcess.restype = wt.HANDLE
        k32.AssignProcessToJobObject.argtypes = (wt.HANDLE, wt.HANDLE)
        k32.QueryInformationJobObject.argtypes = (
            wt.HANDLE,
            ctypes.c_int,
            ctypes.c_void_p,
            wt.DWORD,
            ctypes.c_void_p,
        )

        class Basic(ctypes.Structure):
            _fields_ = [
                ("TotalUserTime", ctypes.c_longlong),
                ("TotalKernelTime", ctypes.c_longlong),
                ("ThisPeriodTotalUserTime", ctypes.c_longlong),
                ("ThisPeriodTotalKernelTime", ctypes.c_longlong),
                ("TotalPageFaultCount", wt.DWORD),
                ("TotalProcesses", wt.DWORD),
                ("ActiveProcesses", wt.DWORD),
                ("TotalTerminatedProcesses", wt.DWORD),
            ]

        self.k32, self.Basic = k32, Basic
        self.job = k32.CreateJobObjectW(None, None)
        if not self.job:
            raise OSError("CreateJobObjectW failed: %d" % ctypes.get_last_error())
        if not k32.AssignProcessToJobObject(self.job, k32.GetCurrentProcess()):
            raise OSError(
                "AssignProcessToJobObject failed: %d" % ctypes.get_last_error()
            )

    def cpu(self):
        info = self.Basic()
        # JobObjectBasicAccountingInformation (1).
        if not self.k32.QueryInformationJobObject(
            self.job, 1, self.ctypes.byref(info), self.ctypes.sizeof(info), None
        ):
            raise OSError(
                "QueryInformationJobObject failed: %d" % self.ctypes.get_last_error()
            )
        return (info.TotalUserTime + info.TotalKernelTime) / 1e7


def lit_runner(build, meter):
    """run(rsp, test, log) -> (exit status, CPU seconds or None, output) of
    one msil-lit.ps1 invocation."""
    pwsh = shutil.which("pwsh")
    if not pwsh:
        raise Usage("pwsh not found (calibrate.py runs msil/utils/build/msil-lit.ps1)")
    msil_lit = os.path.join(REPO, "msil", "utils", "build", "msil-lit.ps1")

    def run(rsp, test, log):
        cmd = [
            pwsh,
            "-NoProfile",
            "-NonInteractive",
            "-File",
            msil_lit,
            "-BuildDir",
            build,
            "-LitArgs:-sv -j1 @" + rsp.replace("\\", "/"),
            test,
        ]
        before = meter.cpu() if meter else None
        res = subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        after = meter.cpu() if meter else None
        text = res.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
        with open(log, "w", encoding="utf-8", newline="\n") as f:
            f.write("calibrate.py: %s\n" % subprocess.list2cmdline(cmd))
            f.write(text)
        return res.returncode, (None if before is None else after - before), text

    return run


def load_percent():
    pwsh = shutil.which("pwsh")
    if not pwsh:
        return None
    try:
        res = subprocess.run(
            [pwsh, "-NoProfile", "-NonInteractive", "-Command", LOAD_COMMAND],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=120,
        )
        return int(round(float(res.stdout.decode("ascii", "replace").strip())))
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None


def write_rsp(path, lines):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(line + "\n" for line in lines))


def tail(text, n=40):
    lines = text.rstrip("\n").split("\n")
    return "\n".join(lines[-n:])


def measure(label, build, out, calibration, runner, probe, stdout):
    """--label: returns the exit status."""
    cal = load_calibration(calibration)
    if any(c.isspace() for c in build):
        raise Usage(
            "the build directory '%s' contains a blank, which msil-lit.ps1 would split "
            "inside -LitArgs" % build
        )
    results = os.path.join(build, "lit-results")
    os.makedirs(results, exist_ok=True)
    out = out or os.path.join(results, "calibration-%s.json" % label)
    if os.path.isdir(out):
        raise Usage("--out %s is a directory" % out)
    if os.path.lexists(out):
        os.remove(out)
    stem = os.path.join(results, "calibration-%s" % label).replace("\\", "/")
    xml = stem + ".xml"
    if os.path.lexists(xml):
        os.remove(xml)
    params = ["--param=" + p for p in cal["params"]]
    write_rsp(stem + ".params.rsp", params + ["--xunit-xml-output=" + xml])
    write_rsp(
        stem + "-config.params.rsp",
        params + ["--filter=" + CONFIG_FILTER, "--allow-empty-runs"],
    )
    test = cal["test"]

    rc, config_cpu, text = runner(
        stem + "-config.params.rsp", test, stem + "-config.log"
    )
    if rc == 0 and PROBE_MISS in text:
        rc, config_cpu, text = runner(
            stem + "-config.params.rsp", test, stem + "-config.log"
        )
    if rc != 0:
        stdout.write(
            "%s\ncalibrate.py: the configuration-only lit invocation exited %d (%s-config.log)\n"
            % (tail(text), rc, stem)
        )
        return 1
    load = probe()
    rc, run_cpu, text = runner(stem + ".params.rsp", test, stem + ".log")
    passed, seconds, why = read_test_time(xml, test)
    if rc != 0 or not passed:
        stdout.write(
            "%s\ncalibrate.py: the calibration test did not pass: %s (lit exit %d; %s.log)\n"
            % (tail(text), why or "lit failed", rc, stem)
        )
        return 1

    quiet = cal["quiet_seconds"]
    factor = seconds / quiet
    test_cpu = None if config_cpu is None or run_cpu is None else run_cpu - config_cpu
    record = {
        "version": 1,
        "label": label,
        "test": test,
        "params": cal["params"],
        "seconds": round(seconds, 3),
        "quiet_seconds": quiet,
        "factor": round(factor, 4),
        "test_cpu_seconds": None if test_cpu is None else round(test_cpu, 2),
        "reference_test_cpu_seconds": cal["test_cpu_seconds"],
        "configuration_cpu_seconds": None
        if config_cpu is None
        else round(config_cpu, 2),
        "run_cpu_seconds": None if run_cpu is None else round(run_cpu, 2),
        "load_percent": load,
        "lit_exit": rc,
        "xunit": xml,
        "log": stem + ".log",
        "created": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    }
    parent = os.path.dirname(os.path.abspath(out))
    os.makedirs(parent, exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(record, f, indent=2)
        f.write("\n")
    stdout.write(
        "calibration %s: %.1f s against %.1f s quiet, factor %.2f\n"
        % (label, seconds, quiet, factor)
    )
    ref = cal["test_cpu_seconds"]
    if test_cpu is None:
        stdout.write(
            "calibrate.py: note: the test CPU was not measured (no job object)\n"
        )
    elif ref is not None and abs(test_cpu - ref) > CPU_TOLERANCE * ref:
        stdout.write(
            "calibrate.py: warning: the measured test CPU, %.1f s, differs from calibration.json's "
            "test_cpu_seconds, %.1f s, by %d%% (more than %d%%; load inflates CPU by up to 1.3x, "
            "and a harness change that moves it for good updates calibration.json, plan 5.17.8)\n"
            % (
                test_cpu,
                ref,
                round(abs(test_cpu - ref) * 100 / ref),
                round(CPU_TOLERANCE * 100),
            )
        )
    stdout.write("calibrate.py: wrote %s\n" % out.replace("\\", "/"))
    return 0


def read_factor(path):
    try:
        with open(path, encoding="utf-8-sig") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        raise Usage("cannot read %s: %s" % (path, e))
    if not isinstance(data, dict):
        raise Usage("%s is not a calibration result" % path)
    return number(data.get("factor"), "%s: 'factor'" % path)


def combine(start, end, stdout):
    fs, fe = read_factor(start), read_factor(end)
    stdout.write("gate load factor %.2f\n" % max(1.0, (fs + fe) / 2))
    return 0


# ---------------------------------------------------------------- self-test

TEST = "msil/test/M1/03-control/loops.c"
PARAMS = [
    "msil_pipeline=obj",
    "msil_dotnet_runtime=8",
    "msil_native_oracle=required",
    "msil_run_netfx=1",
    "msil_equiv=1",
    "msil_also_run=10,8+tc0",
    "msil_require_verify=1",
    "msil_require_exec=1",
    "msil_require_link=1",
]


class FakeLit:
    """A stand-in for the msil-lit.ps1 invocations: the configuration-only
    one (its response file has --allow-empty-runs) and the test's, which
    writes an xunit file of lit's form."""

    def __init__(
        self,
        seconds=15.0,
        run_cpu=9.6,
        config_cpu=1.2,
        misses=0,
        passed=True,
        test_rc=0,
        config_rc=0,
        cpu=True,
        name="loops.c",
    ):
        self.seconds, self.run_cpu, self.config_cpu = seconds, run_cpu, config_cpu
        self.misses, self.passed, self.test_rc, self.config_rc = (
            misses,
            passed,
            test_rc,
            config_rc,
        )
        self.cpu, self.name = cpu, name
        self.calls = []

    def __call__(self, rsp, test, log):
        with open(rsp, encoding="utf-8") as f:
            lines = f.read().splitlines()
        self.calls.append((lines, test))
        with open(log, "w", encoding="utf-8") as f:
            f.write("stand-in lit\n")
        if "--allow-empty-runs" in lines:
            note = ""
            if self.misses:
                self.misses -= 1
                note = "lit.cfg.py: note: probe cache: miss (no cache file)\n"
            return (
                self.config_rc,
                (self.config_cpu if self.cpu else None),
                note + "0 tests\n",
            )
        xml = next(
            x[len("--xunit-xml-output=") :]
            for x in lines
            if x.startswith("--xunit-xml-output=")
        )
        body = (
            ""
            if self.passed
            else "<failure><![CDATA[# executed command: x\n]]></failure>"
        )
        with open(xml, "w", encoding="utf-8") as f:
            f.write(
                '<?xml version="1.0" encoding="UTF-8"?>\n<testsuites time="1">\n'
                '<testsuite name="MSIL" tests="1" failures="0" skipped="0" time="1">\n'
                '<testcase classname="MSIL.M1/03-control" name="%s" time="%.2f">%s</testcase>\n'
                "</testsuite>\n</testsuites>\n" % (self.name, self.seconds, body)
            )
        return self.test_rc, (self.run_cpu if self.cpu else None), "Passed: 1\n"


def self_test():
    work = tempfile.mkdtemp(prefix="calibrate-self-test-")
    problems = []
    count = [0]

    def write_json(path, data):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)

    cal = os.path.join(work, "calibration.json")
    write_json(
        cal,
        {
            "version": 1,
            "test": TEST,
            "params": PARAMS,
            "quiet_seconds": 10.0,
            "test_cpu_seconds": 8.0,
            "source": "self-test",
        },
    )
    build = os.path.join(work, "build")

    def run(
        what, lit, label="start", out=None, calibration=cal, want_rc=0, has=(), lacks=()
    ):
        count[0] += 1
        buf = io.StringIO()
        try:
            rc = measure(label, build, out, calibration, lit, lambda: 37, buf)
        except Usage as e:
            rc = 2
            buf.write("usage: %s\n" % e)
        except Exception as e:  # a defect: report it as this case's failure
            rc = -1
            buf.write("%s: %s\n" % (type(e).__name__, e))
        text = buf.getvalue()
        bad = [h for h in has if h not in text] + [h for h in lacks if h in text]
        if rc != want_rc or bad:
            problems.append(
                "%s: exit %d (want %d); output %r; %s" % (what, rc, want_rc, text, bad)
            )
        return text

    try:
        # --out two levels below a directory that does not exist.
        out = os.path.join(work, "new", "a", "b", "calibration-start.json")
        lit = FakeLit()
        run(
            "--out below missing directories",
            lit,
            out=out,
            has=["calibration start: 15.0 s against 10.0 s quiet, factor 1.50\n"],
            lacks=["warning"],
        )
        count[0] += 1
        try:
            with open(out, encoding="utf-8") as f:
                got = json.load(f)
            want = {
                "seconds": 15.0,
                "quiet_seconds": 10.0,
                "factor": 1.5,
                "test_cpu_seconds": 8.4,
                "load_percent": 37,
                "label": "start",
                "test": TEST,
            }
            diff = {k: got.get(k) for k in want if got.get(k) != want[k]}
            if diff:
                problems.append("the --out file: %r differ from %r" % (diff, want))
        except (OSError, ValueError) as e:
            problems.append("the --out file: %s" % e)
        count[0] += 1
        config_calls = [c for c in lit.calls if "--allow-empty-runs" in c[0]]
        test_calls = [c for c in lit.calls if "--allow-empty-runs" not in c[0]]
        rsp_ok = (
            len(config_calls) == 1
            and len(test_calls) == 1
            and lit.calls.index(config_calls[0]) == 0
            and all(c[1] == TEST for c in lit.calls)
            and all(
                ["--param=" + p for p in PARAMS] == c[0][: len(PARAMS)]
                for c in lit.calls
            )
            and "--filter=" + CONFIG_FILTER in config_calls[0][0]
            and not any(x.startswith("--xunit-xml-output=") for x in config_calls[0][0])
            and any(x.startswith("--xunit-xml-output=") for x in test_calls[0][0])
        )
        if not rsp_ok:
            problems.append("the response files: %r" % lit.calls)
        if os.path.exists(os.path.join(build, "lit-results", "calibration-start.json")):
            problems.append("--out also wrote the default file")

        # The default file; a probe-cache miss repeats the configuration-only run.
        lit = FakeLit(seconds=24.0, misses=1)
        run(
            "the default output and a probe-cache miss",
            lit,
            label="end",
            has=["calibration end: 24.0 s against 10.0 s quiet, factor 2.40\n"],
        )
        count[0] += 1
        default = os.path.join(build, "lit-results", "calibration-end.json")
        if not os.path.isfile(default) or len(lit.calls) != 3:
            problems.append(
                "the default output: %s exists %s, %d lit runs (want 3)"
                % (default, os.path.isfile(default), len(lit.calls))
            )

        run(
            "a test CPU 37% above calibration.json's",
            FakeLit(run_cpu=12.2, config_cpu=1.2),
            has=[
                "warning: the measured test CPU, 11.0 s, differs from calibration.json's "
                "test_cpu_seconds, 8.0 s, by 38%"
            ],
        )
        run(
            "a test CPU 27% below calibration.json's (no warning)",
            FakeLit(run_cpu=7.0, config_cpu=1.2),
            lacks=["warning"],
        )
        run(
            "a test CPU 33% below calibration.json's",
            FakeLit(run_cpu=6.5, config_cpu=1.2),
            has=["warning: the measured test CPU, 5.3 s"],
        )
        run(
            "no job object",
            FakeLit(cpu=False),
            has=["note: the test CPU was not measured"],
        )

        # A failing test: exit 1, and a stale result is removed.
        stale = os.path.join(work, "stale", "calibration-end.json")
        os.makedirs(os.path.dirname(stale))
        write_json(stale, {"factor": 9.9})
        run(
            "a failing test",
            FakeLit(passed=False, test_rc=1),
            label="end",
            out=stale,
            want_rc=1,
            has=[
                "the calibration test did not pass: msil/test/M1/03-control/loops.c failed"
            ],
        )
        count[0] += 1
        if os.path.exists(stale):
            problems.append("a failing test left %s" % stale)
        run(
            "a failing configuration-only run",
            FakeLit(config_rc=2),
            want_rc=1,
            has=["configuration-only lit invocation exited 2"],
        )
        run(
            "no testcase of the test in the xunit file",
            FakeLit(name="other.c"),
            want_rc=1,
            has=["has no testcase for msil/test/M1/03-control/loops.c"],
        )

        # calibration.json problems: exit 2.
        run(
            "a missing calibration.json",
            FakeLit(),
            calibration=os.path.join(work, "none.json"),
            want_rc=2,
            has=["does not exist"],
        )
        bad = os.path.join(work, "bad.json")
        write_json(
            bad, {"version": 1, "test": TEST, "params": PARAMS, "quiet_seconds": 0}
        )
        run(
            "quiet_seconds 0",
            FakeLit(),
            calibration=bad,
            want_rc=2,
            has=["must be positive"],
        )
        write_json(
            bad,
            {
                "version": 1,
                "test": TEST,
                "params": {"msil_pipeline": "obj"},
                "quiet_seconds": 5,
                "test_cpu_seconds": None,
            },
        )
        run(
            "params as an object, no test_cpu_seconds",
            FakeLit(seconds=5.0),
            calibration=bad,
            has=["factor 1.00\n"],
            lacks=["warning", "note"],
        )

        # --combine.
        start = os.path.join(work, "s.json")
        end = os.path.join(work, "e.json")
        for fs, fe, want in (
            (1.2, 1.6, "gate load factor 1.40"),
            (0.8, 0.9, "gate load factor 1.00"),
            (2.0, 3.5, "gate load factor 2.75"),
        ):
            count[0] += 1
            write_json(start, {"factor": fs})
            write_json(end, {"factor": fe})
            buf = io.StringIO()
            rc = combine(start, end, buf)
            if rc != 0 or buf.getvalue() != want + "\n":
                problems.append(
                    "--combine %s %s: exit %d, %r" % (fs, fe, rc, buf.getvalue())
                )
        for what, data in (
            ("a missing file", None),
            ("a file without a factor", {"seconds": 3}),
        ):
            count[0] += 1
            if data is None:
                os.remove(end)
            else:
                write_json(end, data)
            try:
                combine(start, end, io.StringIO())
                problems.append("--combine with %s: no error" % what)
            except Usage:
                pass

        # The command line.
        for argv in (
            [],
            ["--label", "middle"],
            ["--combine", start],
            ["--label", "start", "--combine", start, end],
        ):
            count[0] += 1
            err = io.StringIO()
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(
                io.StringIO()
            ):
                rc = main(argv)
            if rc != 2:
                problems.append("command line %r: exit %d (want 2)" % (argv, rc))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    for p in problems:
        print("calibrate: self-test: FAIL: %s" % p)
    print(
        "calibrate: self-test: %d of %d cases passed"
        % (count[0] - len(problems), count[0])
    )
    return 1 if problems else 0


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write("calibrate.py: %s\n" % message)
        raise SystemExit(2)


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    ap = Parser(prog="calibrate.py", description=__doc__.split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--label", choices=("start", "end"))
    mode.add_argument("--combine", nargs=2, metavar=("START", "END"))
    ap.add_argument("--build", default=None, metavar="DIR")
    ap.add_argument("--out", default=None, metavar="FILE")
    ap.add_argument("--calibration", default=DEFAULT_CALIBRATION, metavar="FILE")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2
    try:
        if args.combine:
            return combine(args.combine[0], args.combine[1], sys.stdout)
        if os.name != "nt":
            raise Usage(
                "measuring needs Windows (a job object and msil/utils/build/msil-lit.ps1); "
                "--combine and --self-test run anywhere"
            )
        build = os.path.abspath(args.build or default_build())
        try:
            meter = JobCpu()
        except OSError as e:
            sys.stdout.write(
                "calibrate.py: note: no job object (%s); the test CPU is not measured\n"
                % e
            )
            meter = None
        return measure(
            args.label,
            build,
            args.out and os.path.abspath(args.out),
            args.calibration,
            lit_runner(build, meter),
            load_percent,
            sys.stdout,
        )
    except Usage as e:
        sys.stderr.write("calibrate.py: %s\n" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
