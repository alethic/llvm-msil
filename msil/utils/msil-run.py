#!/usr/bin/env python3
# ===-- msil-run.py - Run a program of the MSIL test suite ----------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run a program built by the MSIL test suite and normalize what it did.

    msil-run.py [OPTIONS] --dotnet D --runtimeconfig C [--] prog.dll [ARGS...]
    msil-run.py [OPTIONS] --dep FILE... [--dep-dir DIR] ... -- prog.dll [ARGS...]
    msil-run.py [OPTIONS] --runner wsl [--dotnet D] --runtimeconfig C [--] prog.dll
    msil-run.py [OPTIONS] --netfx [--] prog.exe [ARGS...]      (.NET Framework)
    msil-run.py [OPTIONS] --native [--] prog.exe [ARGS...]     (native oracle)
    msil-run.py [OPTIONS] --native --runner wsl [--] prog.elf  (E7 Linux oracle)
    msil-run.py --tool [--timeout S] [--mem-limit-mb N] -- TOOL [ARGS...]
    msil-run.py --tool --runner wsl [--wsl-distro D] [LIMITS] -- TOOL [ARGS...]
    msil-run.py --runner wsl --steps FILE      (several steps in one wsl.exe)

The transcript is the program's stdout, followed by one status line, with
every run of CRs before an LF (the regular expression \\r+\\n) replaced by a
single LF (plan section 12, 'Transcript newline rule'). The same rule applies
to .expected files and in update-expected.py, on every runner. It is
idempotent, so puts("a\\r") gives the transcript 'a\\n' both on Windows, where
text-mode stdout writes 'a\\r\\r\\n', and on Linux ('a\\r\\n'), and a
transcript survives being committed with 'eol=lf' (msil/.gitattributes). The
status lines are:

    [exit N]                      exit status N, 0 <= N <= 255
    [exit N: outside 0..255]      any other exit status; never matches (D37)
    [unhandled exception: T]      the CLR reported an unhandled exception T
    [failfast]                    the program called Environment.FailFast
    [signal N]                    killed by signal N (POSIX)
    [timeout]                     still running after --timeout seconds
    [memory limit]                hit the --mem-limit-mb memory cap

'[no newline at end of output]' comes first when stdout does not end in a
newline. Linux keeps only the low 8 bits of an exit status, so a status above
255 that Windows reports in full is never accepted (plan D37). N is decimal
(signed on Windows), except for Windows exception and HRESULT codes
(0x80000000..0xEFFFFFFF, for example 0xC0000005), which are hexadecimal.

'[failfast]' (plan 5.17.4; the trap thunks of loud failures) is the status
of a managed program ('dotnet exec' or --netfx) that ended in
Environment.FailFast, which the runtimes report differently (measured
2026-10-29 with FailFast(string) and FailFast(string, Exception)): on Windows
the .NET Framework 4.8, .NET 8 and .NET 10 exit with 0x80131623
(COR_E_FAILFAST; '[exit 0x80131623: outside 0..255]' without the mapping),
and on Linux .NET 8 and 10 print 'Process terminated.' on stderr and abort
(SIGABRT; '[signal 6]' without it). So the status is '[failfast]' for exit
status 0x80131623 on Windows, and for signal 6 after a stderr line that
starts with 'Process terminated.' on POSIX (also inside WSL). A --native
program is never mapped: its abort() stays '[signal 6]'.

The program runs from a fresh private directory that holds only a copy of it
(and of its runtimeconfig.json and --dep files): two assemblies with the same name in one
directory silently run the wrong code (llvm/docs/MSIL/recon/asmprinter.md 0.9).
It runs under a wall-clock timeout (--timeout, default 60 s) and a memory cap
on committed memory (--mem-limit-mb, default 4096): a Windows Job Object, or
RLIMIT_DATA in a new session on POSIX. The .NET 10 JIT was seen growing past
40 GB on bad IL (llvm/docs/MSIL/recon/cil-runtime.md, e15).

--runner wsl runs a .dll in WSL ('wsl.exe -d <distro> --exec env VAR=VALUE...
python3 msil-run.py ... prog.dll' with the paths translated to /mnt/<drive>/),
so the timeout, the memory cap and the private directory apply inside Linux,
where 'dotnet exec' really runs. --dotnet is then the dotnet of the
distribution (default 'dotnet') and --runtimeconfig a Windows path.

--native --runner wsl runs a Linux executable the same way: the native twin
of a test that the E7 Linux oracle (plan 5.12.8) linked on Windows with
ld.lld against the libraries of msil/utils/make-linux-sysroot.py. The copy
of this script inside WSL runs it with --native from its own private
directory (an executable copy), under the timeout and the memory cap, and
the transcript comes back to Windows, where --expect compares it (lit passes
--expect-os linux). --dotnet and --runtimeconfig are not used.

--env VAR=VALUE (repeatable) sets an environment variable for the program in
every mode (for example DOTNET_TieredCompilation=0, plan D38).

--dep FILE... (one or more files; repeatable) copies each FILE into the
private directory next to the program, and --dep-dir DIR (repeatable) copies
everything in DIR there (plan 5.12.4, M15): the managed assemblies and the
native libraries a program needs at run time, in every mode (.dll, --netfx,
--native; with --runner wsl into the Linux temporary directory, where the
copy of this script inside WSL runs the program). On Linux and other POSIX
hosts, and so in WSL, a .dll run that is given --dep or --dep-dir also puts
that directory first in LD_LIBRARY_PATH ('<dir>', or '<dir>:<old value>'): a
native data import dlopen()s the library's bare name, which glibc never looks
for in the program's directory (plan 5.10.12 as clarified in section 12; the
native oracle links with -rpath '$ORIGIN' instead). Windows, --netfx and
--native runs keep the environment. A missing FILE or DIR, and two files of
one name (or one named like the program), are configuration errors. --dep
takes every argument up to the next option, so the lit composites pass
'--dep %{msil_deps} --' before the program; an empty list is allowed.

With --expect STEM the transcript is compared with STEM.<os>.expected if that
file exists, else STEM.expected; <os> (windows, linux or darwin) comes from
--expect-os, which lit sets from the target triple (default: linux with
--runner wsl, else the host's). A mismatch prints a unified diff and exits 1.
--out FILE also writes the transcript to FILE. Without --expect the
transcript goes to stdout. --stderr-out FILE (plan 5.17.4) writes the
program's stderr, under the transcript newline rule, to FILE whenever the
program ran (with --runner wsl: what the run inside WSL wrote to stderr), for
the FileCheck of a loud failure's message; a run that fails before the
program starts or is skipped leaves no FILE (an old one is removed).

--skip-netfx REASON (with --netfx; plan 5.17.4, lit's %{msil_netfx_skip})
runs nothing: after the usage checks it prints 'msil-run: netfx run
skipped: REASON' on stdout and exits with status 0 (a program that needs
what the .NET Framework lacks, such as the attribute of --uco-thunks).

--expected-rule RULE (with --native; plan 5.17.4, %{msil_expected_rule}) is
empty (no rule, as when it is left out) or '<os>[,<os>]: <reason>', where
each <os> is windows or linux and the reason holds no single quote, the
grammar of msil-prog.py --expected-rule. When RULE names the OS of
--expect-os, the test's expected file for that OS is written by hand and the
native oracle is not run: after the usage checks msil-run.py prints
'msil-run: native oracle skipped (hand-written expected file): <reason>' on
stdout and exits with status 0 (msil-prog.py --mode native skipped the build
the same way). A malformed RULE is a usage error.

--runner wsl --steps FILE (plan 5.17.10; msil-steps.py) runs the steps that
FILE lists, msil-run.py and msil-jitall.py runs with --runner wsl, in order
and through one wsl.exe for each run of consecutive steps of one
distribution, instead of one wsl.exe per step: FILE is JSON, {"version": 1,
"steps": [{"name": N, "script": "msil-run.py" or "msil-jitall.py", "argv":
[ARG...], "display": TEXT}, ...]}, where argv is the step's own command line
after the script and display (optional) a line printed before the step's
output. Each step's command line inside WSL is the one its own wsl.exe would
run; a copy of this script inside WSL (the internal --steps-inside) runs
them one by one, each under the wall-clock limit its own wsl.exe would have
had (its --timeout plus the WSL start-up grace), and stops after the first
one that cannot pass. Back on Windows every step is judged and reported in
order exactly as its own run would be, with the same messages, and the
first failing step ends the list with its exit status. A step that needs no
WSL (a skip of --skip-netfx or --expected-rule, a step without --runner wsl)
or that fails before it would start wsl.exe (a usage or configuration
error) runs by itself in its turn. --steps takes no other option; its exit
status is that of the first failing step, else 0.

--tool runs any other program (ilasm, a verifier, a compiler) under the same
timeout and memory cap, from the current directory, passing its output and
exit status through.

--tool --runner wsl runs a Linux program that lives on a Windows drive (the
Linux ilasm of the M10 assembler leg, lit parameter msil_wsl_ilasm) inside
WSL, under the limits there: wsl.exe starts 'python3 msil-run.py --tool
--stage' in the current directory. TOOL, and every argument that is an
absolute Windows path (alone or as the VALUE of -OPTION=VALUE or
-OPTION:VALUE), become /mnt/<drive>/... paths; relative paths and other
arguments are passed unchanged. --stage copies TOOL into a fresh private
temporary directory, makes the copy executable and runs it from there; the
directory is removed afterwards. This is a precaution: under WSL's default
DrvFs mount (no 'metadata' option) files on a Windows drive show as 0777 and
run in place, but a DrvFs mount with 'metadata' and a restrictive fmask (or a
package extracted without the execute bit) makes the original non-executable.

A .dll that the dotnet host or CoreCLR does not start at all (a missing
framework, an invalid runtimeconfig.json, 'Failed to create CoreCLR') is a
configuration error on every OS, never an '[exit N]' line: POSIX keeps only
the low 8 bits of the host's HRESULT (0x80008096 -> 150), and CoreCLR exits
with 137 there. host_startup_failure() recognizes the messages (msil-jitall.py
uses it too). An out-of-memory start-up failure (HRESULT 0x8007000E or
0x80070008) is the memory cap at work and gives '[memory limit]'.

Exit status: 0 on success (and for a skipped run); 1 on a mismatch or when
the program's exit status is outside 0..255 (the transcript is still
written); 2 on a usage or configuration error. With --tool, the tool's own
status (1 when it hit the timeout or the memory cap, 2 when it could not be
started). main(argv) returns the status, for a usage error too, so
msil-steps.py runs this script as a function.
"""

import argparse
import base64
import contextlib
import difflib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

IS_WINDOWS = os.name == "nt"

DEFAULT_TIMEOUT = 60.0
DEFAULT_MEM_LIMIT_MB = 4096
# Extra wall-clock time for wsl.exe around the timeout that msil-run.py
# enforces inside WSL: a cold WSL start takes more than 10 s on this host.
WSL_GRACE = 120.0

# CoreCLR prints "Unhandled exception. System.Foo: ..." and .NET Framework
# prints "Unhandled Exception: System.Foo: ...".
UNHANDLED_RE = re.compile(r"Unhandled [Ee]xception[.:]\s*([\w.`+]+)")
# How a runtime reports a failed allocation under RLIMIT_DATA (POSIX), where
# the limit itself is silent. 0x8007000E is E_OUTOFMEMORY (GC heap setup) and
# 0x80070008 is ERROR_NOT_ENOUGH_MEMORY ('Failed to create CoreCLR' under a
# cap of a few dozen MiB).
OOM_RE = re.compile(r"OutOfMemoryException|[Oo]ut of memory|0x8007000[Ee8]")
# How the dotnet host (muxer, hostfxr, hostpolicy) and CoreCLR say that the
# program never started: a missing framework, an invalid runtimeconfig.json,
# a runtime that cannot initialize. Their statuses are 0x8000808x HRESULTs on
# Windows, but POSIX keeps only 8 bits (0x80008096 -> 150) and a failed CoreCLR
# start exits with 137 there, which would read as an ordinary '[exit N]'.
HOST_STARTUP_RE = re.compile(
    r"^(?:Failed to create CoreCLR, HRESULT: 0x[0-9A-Fa-f]+"
    r"|GC heap initialization failed.*"
    r"|You must install or update \.NET.*"
    r"|A fatal error (?:occurred|was encountered).*"
    r"|Invalid runtimeconfig\.json.*)$",
    re.M,
)
STATUS_RE = re.compile(
    r"^\[(?:exit -?\w+(?:: outside 0\.\.255)?|unhandled exception: .*|signal \d+"
    r"|timeout|memory limit|failfast)\]$"
)
OUTSIDE_RE = re.compile(r"^\[exit (-?\w+): outside 0\.\.255\]$")
NEWLINE_RE = re.compile(r"\r+\n")
# Environment.FailFast (see '[failfast]' above): Windows runtimes exit with
# COR_E_FAILFAST; CoreCLR on Linux prints 'Process terminated.' and aborts.
COR_E_FAILFAST = 0x80131623
SIGABRT = 6
FAILFAST_RE = re.compile(r"^Process terminated\.", re.M)
# --expected-rule (plan 5.17.4): the operating systems a rule may name.
RULE_OSES = ("windows", "linux")
NETFX_SKIP = "netfx run skipped: %s"
RULE_SKIP = "native oracle skipped (hand-written expected file): %s"
# --steps: the version of FILE and of the internal file of --steps-inside,
# and the prefix of the result lines that the copy inside WSL prints.
STEPS_VERSION = 1
STEP_RESULT = "MSIL-STEP "


class ConfigError(Exception):
    pass


class UsageError(Exception):
    pass


class LimitedResult:
    def __init__(self, returncode, stdout, stderr, timed_out, memory_limit):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.timed_out = timed_out
        self.memory_limit = memory_limit
        # --steps: why the shared wsl.exe could not run at all, else None.
        self.error = None


def host_startup_failure(stderr_text):
    """The message with which the dotnet host or CoreCLR refused to start the
    program (it never ran), or None. msil-jitall.py uses it too."""
    m = HOST_STARTUP_RE.search(stderr_text)
    return m.group(0).strip() if m else None


# run_limited(), OOM_RE, host_startup_failure(), child_env(), remove_tree(),
# to_wsl_path(), text_of(), fold_newlines() (the transcript newline rule),
# check_deps(), copy_deps() and WSL_GRACE are shared: msil-verify.py,
# msil-jitall.py, il-snapshot.py and
# update-expected.py import this file, so they stay module-level, and
# importing it must not do anything else.
if IS_WINDOWS:
    import ctypes
    from ctypes import wintypes

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _ntdll = ctypes.WinDLL("ntdll")

    class _IO_COUNTERS(ctypes.Structure):
        _fields_ = [
            (n, ctypes.c_ulonglong)
            for n in (
                "ReadOperationCount",
                "WriteOperationCount",
                "OtherOperationCount",
                "ReadTransferCount",
                "WriteTransferCount",
                "OtherTransferCount",
            )
        ]

    class _JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("PerProcessUserTimeLimit", ctypes.c_int64),
            ("PerJobUserTimeLimit", ctypes.c_int64),
            ("LimitFlags", wintypes.DWORD),
            ("MinimumWorkingSetSize", ctypes.c_size_t),
            ("MaximumWorkingSetSize", ctypes.c_size_t),
            ("ActiveProcessLimit", wintypes.DWORD),
            ("Affinity", ctypes.c_size_t),
            ("PriorityClass", wintypes.DWORD),
            ("SchedulingClass", wintypes.DWORD),
        ]

    class _JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("BasicLimitInformation", _JOBOBJECT_BASIC_LIMIT_INFORMATION),
            ("IoInfo", _IO_COUNTERS),
            ("ProcessMemoryLimit", ctypes.c_size_t),
            ("JobMemoryLimit", ctypes.c_size_t),
            ("PeakProcessMemoryUsed", ctypes.c_size_t),
            ("PeakJobMemoryUsed", ctypes.c_size_t),
        ]

    class _JOBOBJECT_ASSOCIATE_COMPLETION_PORT(ctypes.Structure):
        _fields_ = [
            ("CompletionKey", ctypes.c_void_p),
            ("CompletionPort", wintypes.HANDLE),
        ]

    _JobObjectAssociateCompletionPortInformation = 7
    _JobObjectExtendedLimitInformation = 9
    _JOB_OBJECT_LIMIT_PROCESS_MEMORY = 0x00000100
    _JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION = 0x00000400
    _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
    _JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO = 4
    _JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT = 9
    _CREATE_SUSPENDED = 0x00000004
    _PROCESS_TERMINATE = 0x0001
    _PROCESS_SET_QUOTA = 0x0100
    _PROCESS_SUSPEND_RESUME = 0x0800
    _INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value

    _k32.CreateJobObjectW.restype = wintypes.HANDLE
    _k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    _k32.SetInformationJobObject.restype = wintypes.BOOL
    _k32.SetInformationJobObject.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        ctypes.c_void_p,
        wintypes.DWORD,
    ]
    _k32.CreateIoCompletionPort.restype = wintypes.HANDLE
    _k32.CreateIoCompletionPort.argtypes = [
        wintypes.HANDLE,
        wintypes.HANDLE,
        ctypes.c_size_t,
        wintypes.DWORD,
    ]
    _k32.GetQueuedCompletionStatus.restype = wintypes.BOOL
    _k32.GetQueuedCompletionStatus.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.DWORD),
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(ctypes.c_void_p),
        wintypes.DWORD,
    ]
    _k32.OpenProcess.restype = wintypes.HANDLE
    _k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _k32.AssignProcessToJobObject.restype = wintypes.BOOL
    _k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _k32.TerminateJobObject.restype = wintypes.BOOL
    _k32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _k32.CloseHandle.restype = wintypes.BOOL
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _k32.SetErrorMode.restype = wintypes.UINT
    _k32.SetErrorMode.argtypes = [wintypes.UINT]
    _ntdll.NtResumeProcess.restype = ctypes.c_long
    _ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]

    def _win_check(ok, what):
        if not ok:
            raise OSError(
                "%s failed: %s" % (what, ctypes.FormatError(ctypes.get_last_error()))
            )
        return ok

    def run_limited(cmd, cwd, env, timeout, mem_limit_mb, stdin=None):
        """Run cmd in a Job Object with a per-process commit limit.

        The child starts suspended and is resumed only after it is in the job,
        so it never runs unconstrained. Closing the job kills everything in it.
        A memory-limit hit is read from the job's completion port.
        """
        # No WER or critical-error dialogs; inherited by the child.
        _k32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
        job = _win_check(_k32.CreateJobObjectW(None, None), "CreateJobObject")
        port = None
        hproc = None
        try:
            info = _JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
            info.BasicLimitInformation.LimitFlags = (
                _JOB_OBJECT_LIMIT_PROCESS_MEMORY
                | _JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                | _JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION
            )
            info.ProcessMemoryLimit = mem_limit_mb * 1024 * 1024
            _win_check(
                _k32.SetInformationJobObject(
                    job,
                    _JobObjectExtendedLimitInformation,
                    ctypes.byref(info),
                    ctypes.sizeof(info),
                ),
                "SetInformationJobObject(limits)",
            )
            port = _win_check(
                _k32.CreateIoCompletionPort(_INVALID_HANDLE_VALUE, None, 0, 1),
                "CreateIoCompletionPort",
            )
            assoc = _JOBOBJECT_ASSOCIATE_COMPLETION_PORT(None, port)
            _win_check(
                _k32.SetInformationJobObject(
                    job,
                    _JobObjectAssociateCompletionPortInformation,
                    ctypes.byref(assoc),
                    ctypes.sizeof(assoc),
                ),
                "SetInformationJobObject(port)",
            )

            proc = subprocess.Popen(
                cmd,
                cwd=cwd,
                env=env,
                stdin=subprocess.DEVNULL if stdin is None else stdin,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                creationflags=_CREATE_SUSPENDED,
            )
            try:
                hproc = _win_check(
                    _k32.OpenProcess(
                        _PROCESS_TERMINATE
                        | _PROCESS_SET_QUOTA
                        | _PROCESS_SUSPEND_RESUME,
                        False,
                        proc.pid,
                    ),
                    "OpenProcess",
                )
                _win_check(
                    _k32.AssignProcessToJobObject(job, hproc),
                    "AssignProcessToJobObject",
                )
                status = _ntdll.NtResumeProcess(hproc)
                if status < 0:
                    raise OSError(
                        "NtResumeProcess failed: NTSTATUS 0x%08X"
                        % (status & 0xFFFFFFFF)
                    )
            except BaseException:
                proc.kill()
                proc.communicate()
                raise

            timed_out = False
            try:
                out, err = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                timed_out = True
                _k32.TerminateJobObject(job, 1)
                out, err = proc.communicate()

            # Job notifications arrive asynchronously; read them until the
            # job reports that no process is left (or a short grace period).
            memory_limit = False
            msg = wintypes.DWORD()
            key = ctypes.c_size_t()
            ovl = ctypes.c_void_p()
            while _k32.GetQueuedCompletionStatus(
                port, ctypes.byref(msg), ctypes.byref(key), ctypes.byref(ovl), 1000
            ):
                if msg.value == _JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT:
                    memory_limit = True
                elif msg.value == _JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO:
                    break
            return LimitedResult(proc.returncode, out, err, timed_out, memory_limit)
        finally:
            if hproc:
                _k32.CloseHandle(hproc)
            _k32.CloseHandle(job)  # KILL_ON_JOB_CLOSE: nothing outlives this
            if port:
                _k32.CloseHandle(port)

else:
    import resource
    import signal

    def run_limited(cmd, cwd, env, timeout, mem_limit_mb, stdin=None):
        """Run cmd with RLIMIT_DATA in its own session; kill the session on timeout.

        RLIMIT_DATA, not RLIMIT_AS: the .NET 8 GC reserves 256 GiB of address
        space at startup, so under a 4 GiB RLIMIT_AS CoreCLR 8.0.31 fails with
        'GC heap initialization failed with error 0x8007000E' (measured in WSL
        by M0.c). Since Linux 4.7 RLIMIT_DATA counts private writable mappings,
        that is committed memory, like the Windows Job Object limit.
        """
        limit = mem_limit_mb * 1024 * 1024

        def preexec():
            resource.setrlimit(resource.RLIMIT_DATA, (limit, limit))

        proc = subprocess.Popen(
            cmd,
            cwd=cwd,
            env=env,
            stdin=subprocess.DEVNULL if stdin is None else stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=preexec,
            start_new_session=True,
        )
        timed_out = False
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except OSError:
                pass
            out, err = proc.communicate()
        # The rlimit makes allocations fail instead of reporting the limit, so
        # an out-of-memory message from the runtime is the only sign.
        memory_limit = not timed_out and bool(
            OOM_RE.search(err.decode("utf-8", "replace"))
        )
        return LimitedResult(proc.returncode, out, err, timed_out, memory_limit)


def fold_newlines(text):
    """The transcript newline rule: every run of CRs before an LF becomes one
    LF. Idempotent; update-expected.py uses it too."""
    return NEWLINE_RE.sub("\n", text)


def text_of(data):
    return fold_newlines(data.decode("utf-8", errors="backslashreplace"))


def emit(stream, data):
    """Write str or bytes to stream unchanged (no CRLF translation on Windows)."""
    if isinstance(data, str):
        data = data.encode("utf-8")
    stream.flush()
    stream.buffer.write(data)
    stream.buffer.flush()


def remove_tree(path):
    # A killed child can keep the copied program open for a moment on Windows.
    for _ in range(50):
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.exists(path):
            return
        time.sleep(0.1)


def set_env_var(env, key, value, override=True):
    # Windows variable names are case-insensitive; never pass two spellings.
    same = [k for k in env if (k.upper() == key.upper() if IS_WINDOWS else k == key)]
    if same and not override:
        return
    for k in same:
        del env[k]
    env[key] = value


DOTNET_DEFAULTS = (
    ("DOTNET_CLI_TELEMETRY_OPTOUT", "1"),
    ("DOTNET_NOLOGO", "1"),
    ("DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1"),
    ("DOTNET_DbgEnableMiniDump", "0"),
)


def parse_env_args(env_args):
    pairs = []
    for kv in env_args:
        key, sep, value = kv.partition("=")
        if not sep or not key:
            raise ConfigError("--env expects VAR=VALUE, got %r" % kv)
        pairs.append((key, value))
    return pairs


def child_env(env_args):
    env = dict(os.environ)
    for key, value in DOTNET_DEFAULTS:
        set_env_var(env, key, value, override=False)
    for key, value in parse_env_args(env_args):
        set_env_var(env, key, value)
    return env


def prepend_library_path(env, directory):
    """Put directory first in env's LD_LIBRARY_PATH ('<dir>', or '<dir>:<old
    value>'; an empty old value would add the current directory). A .NET
    program's native data import dlopen()s the library's bare name, and glibc
    never looks for one in the program's directory (plan 5.10.12 as clarified
    in section 12): the analogue of the native oracle's -rpath '$ORIGIN'."""
    old = env.get("LD_LIBRARY_PATH")
    env["LD_LIBRARY_PATH"] = directory + ":" + old if old else directory


def describe_exit(rc):
    """An exit status as text: decimal, except that Windows NTSTATUS and HRESULT
    style codes (0x80000000..0xEFFFFFFF, for example 0xC0000005 or the CLR's
    0xE0434352) are hexadecimal; 0xF0000000 and above are small negative ints."""
    if IS_WINDOWS and rc >= (1 << 31):
        if rc < 0xF0000000:
            return "0x%08X" % rc
        return "%d" % (rc - (1 << 32))
    return "%d" % rc


def is_failfast(rc, stderr_text):
    """Whether a managed program's exit status rc (and its stderr) is the end
    of Environment.FailFast on this OS (see '[failfast]' above)."""
    if IS_WINDOWS:
        return rc == COR_E_FAILFAST
    return rc == -SIGABRT and bool(FAILFAST_RE.search(stderr_text))


def status_line(res, stderr_text, managed=False):
    """The status line of a run; managed: a 'dotnet exec' or --netfx run,
    whose Environment.FailFast is '[failfast]'."""
    if res.timed_out:
        return "[timeout]"
    if res.memory_limit:
        return "[memory limit]"
    m = UNHANDLED_RE.search(stderr_text)
    if m:
        return "[unhandled exception: %s]" % m.group(1)
    rc = res.returncode
    if managed and is_failfast(rc, stderr_text):
        return "[failfast]"
    if rc < 0:  # POSIX: killed by a signal
        return "[signal %d]" % -rc
    if rc <= 255:
        return "[exit %d]" % rc
    return "[exit %s: outside 0..255]" % describe_exit(rc)


def make_transcript(stdout_bytes, status):
    text = text_of(stdout_bytes)
    if text and not text.endswith("\n"):
        text += "\n[no newline at end of output]\n"
    # Fold again: output that ends in a CR now has an LF after it.
    return fold_newlines(text + status + "\n")


def host_os():
    return {"nt": "windows"}.get(os.name) or (
        "darwin" if sys.platform == "darwin" else "linux"
    )


def expected_file(stem, osname):
    for cand in (stem + "." + osname + ".expected", stem + ".expected"):
        if os.path.isfile(cand):
            return cand
    return None


def find_dotnet(explicit):
    if explicit:
        if os.path.isfile(explicit):
            return explicit
        found = shutil.which(explicit)  # a bare command name such as 'dotnet'
        if found:
            return found
        raise ConfigError("dotnet host %r not found" % explicit)
    raise ConfigError("--dotnet is required to run a .dll")


def to_wsl_path(path):
    """D:\\x\\y.dll -> /mnt/d/x/y.dll (long names: WSL cannot resolve 8.3 names)."""
    full = os.path.realpath(os.path.abspath(path))
    drive, rest = os.path.splitdrive(full)
    if len(drive) != 2 or drive[1] != ":":
        raise ConfigError("cannot translate %r into a WSL path" % full)
    return "/mnt/" + drive[0].lower() + rest.replace("\\", "/")


# An absolute Windows path, alone or as the value of -OPTION=VALUE or
# -OPTION:VALUE (ilasm's -output=D:\x\y.dll).
WIN_PATH_ARG_RE = re.compile(
    r"^(?P<option>--?[A-Za-z][\w-]*[=:])?(?P<path>[A-Za-z]:[\\/].*)$"
)


def to_wsl_arg(arg):
    """A tool argument for --tool --runner wsl: absolute Windows paths become
    /mnt/<drive>/... paths, everything else is passed unchanged."""
    m = WIN_PATH_ARG_RE.match(arg)
    if not m:
        return arg
    return (m.group("option") or "") + to_wsl_path(m.group("path"))


def wsl_exe():
    return shutil.which("wsl.exe") or os.path.join(
        os.environ.get("SystemRoot", r"C:\Windows"), "System32", "wsl.exe"
    )


def check_stdin(args):
    # A configuration error (exit status 2), never a status that 'not' accepts.
    if args.stdin and not os.path.isfile(args.stdin):
        raise ConfigError("--stdin %s: no such file" % args.stdin)


def check_deps(deps, dep_dirs):
    """--dep files and --dep-dir directories must exist."""
    for dep in deps:
        if not os.path.isfile(dep):
            raise ConfigError("--dep %s: no such file" % dep)
    for d in dep_dirs:
        if not os.path.isdir(d):
            raise ConfigError("--dep-dir %s: no such directory" % d)


def copy_deps(deps, dep_dirs, directory, taken):
    """Copy each --dep file and the contents of each --dep-dir directory into
    directory, next to the files named in taken (the program; msil-verify.py
    and msil-jitall.py use this too). Two entries of one name are a
    configuration error: one would silently replace the other."""
    check_deps(deps, dep_dirs)
    names = {(n.lower() if IS_WINDOWS else n): n for n in taken}

    def claim(name, source):
        key = name.lower() if IS_WINDOWS else name
        if key in names:
            raise ConfigError(
                "dependency %s and %s have the same name %s"
                % (source, names[key], name)
            )
        names[key] = source

    try:
        for dep in deps:
            name = os.path.basename(dep)
            claim(name, dep)
            shutil.copy2(dep, os.path.join(directory, name))
        for d in dep_dirs:
            for name in sorted(os.listdir(d)):
                source = os.path.join(d, name)
                claim(name, source)
                if os.path.isdir(source):
                    shutil.copytree(source, os.path.join(directory, name))
                else:
                    shutil.copy2(source, os.path.join(directory, name))
    except OSError as e:
        raise ConfigError("cannot copy a dependency: %s" % e)


def run_local(args):
    """Run the program from a private directory; return (transcript, stderr)."""
    prog = os.path.abspath(args.program)
    if not os.path.isfile(prog):
        raise ConfigError("%s not found" % args.program)
    dotnet = None
    if not (args.native or args.netfx):
        dotnet = find_dotnet(args.dotnet)
        if not args.runtimeconfig or not os.path.isfile(args.runtimeconfig):
            raise ConfigError(
                "--runtimeconfig FILE is required to run a .dll (got %r)"
                % args.runtimeconfig
            )
    check_stdin(args)
    check_deps(args.dep, args.dep_dir)
    env = child_env(args.env)
    try:
        stdin = open(args.stdin, "rb") if args.stdin else None
    except OSError as e:
        raise ConfigError("cannot read the --stdin file: %s" % e)
    private = tempfile.mkdtemp(prefix="msil-run-")
    try:
        name = os.path.basename(prog)
        copy = os.path.join(private, name)
        shutil.copy2(prog, copy)
        if args.native and not IS_WINDOWS:
            # A program on a DrvFs mount with 'metadata' and a restrictive
            # fmask (the E7 oracle's, under --runner wsl) is not executable.
            os.chmod(copy, 0o755)
        taken = [name]
        if dotnet:
            config = os.path.join(
                private, os.path.splitext(name)[0] + ".runtimeconfig.json"
            )
            shutil.copyfile(args.runtimeconfig, config)
            taken.append(os.path.basename(config))
        copy_deps(args.dep, args.dep_dir, private, taken)
        if dotnet and not IS_WINDOWS and (args.dep or args.dep_dir):
            # Also the copy of this script inside WSL under --runner wsl,
            # which makes private there.
            prepend_library_path(env, private)
        if dotnet:
            cmd = [dotnet, "exec", "--runtimeconfig", config, copy] + args.args
        else:
            cmd = [copy] + args.args
        try:
            res = run_limited(cmd, private, env, args.timeout, args.mem_limit_mb, stdin)
        except OSError as e:
            raise ConfigError("cannot run %s: %s" % (cmd[0], e))
    finally:
        if stdin is not None:
            stdin.close()
        remove_tree(private)
    stderr_text = text_of(res.stderr)
    if dotnet and not (res.timed_out or res.memory_limit or res.stdout):
        # The program never ran: the same configuration error on every OS,
        # never an '[exit N]' that an .expected file could match. An
        # out-of-memory start-up failure is the memory cap at work.
        failure = host_startup_failure(stderr_text)
        if failure and OOM_RE.search(failure):
            res.memory_limit = True
        elif failure:
            raise ConfigError(
                "the dotnet host could not start %s (exit status %s): %s\n%s"
                % (name, describe_exit(res.returncode), failure, stderr_text)
            )
    managed = bool(dotnet) or args.netfx
    status = status_line(res, stderr_text, managed)
    return make_transcript(res.stdout, status), stderr_text


def wsl_inner_command(args):
    """The command line that run_wsl() has wsl.exe run inside WSL (after
    'wsl.exe -d <distro> --exec'), once the files are checked; raises
    ConfigError. msil-run.py --steps runs the same command."""
    if not IS_WINDOWS:
        raise ConfigError("--runner wsl needs a Windows host")
    if not args.native and (
        not args.runtimeconfig or not os.path.isfile(args.runtimeconfig)
    ):
        raise ConfigError(
            "--runtimeconfig FILE is required with --runner wsl (got %r)"
            % args.runtimeconfig
        )
    if not os.path.isfile(args.program):
        raise ConfigError("%s not found" % args.program)
    check_stdin(args)
    assignments = ["%s=%s" % kv for kv in DOTNET_DEFAULTS]
    assignments += ["%s=%s" % kv for kv in parse_env_args(args.env)]
    if args.native:
        mode = ["--native"]
    else:
        mode = [
            "--dotnet",
            args.dotnet or "dotnet",
            "--runtimeconfig",
            to_wsl_path(args.runtimeconfig),
        ]
    cmd = (
        ["env"]
        + assignments
        + [
            "python3",
            to_wsl_path(os.path.abspath(__file__)),
            "--timeout",
            repr(args.timeout),
            "--mem-limit-mb",
            str(args.mem_limit_mb),
        ]
        + mode
        + ["--show-stderr"]
    )
    if args.stdin:
        cmd += ["--stdin", to_wsl_path(args.stdin)]
    # The copy inside WSL copies them into its private Linux directory and,
    # for a .dll, puts that directory first in LD_LIBRARY_PATH (run_local):
    # the assignments above cannot name it, it does not exist yet.
    check_deps(args.dep, args.dep_dir)
    for dep in args.dep:
        cmd += ["--dep", to_wsl_path(dep)]
    for d in args.dep_dir:
        cmd += ["--dep-dir", to_wsl_path(d)]
    cmd += ["--", to_wsl_path(args.program)] + args.args
    return cmd


def wsl_env():
    env = dict(os.environ)
    set_env_var(env, "WSL_UTF8", "1")  # wsl.exe's own messages, not UTF-16
    return env


def run_wsl(args):
    """Run 'dotnet exec' (or, with --native, a Linux executable) in WSL
    through a copy of this script running there."""
    inner = wsl_inner_command(args)
    wsl = wsl_exe()
    cmd = [wsl, "-d", args.wsl_distro, "--exec"] + inner
    try:
        res = run_limited(
            cmd, None, wsl_env(), args.timeout + WSL_GRACE, args.mem_limit_mb
        )
    except OSError as e:
        raise ConfigError("cannot run %s: %s" % (wsl, e))
    return wsl_result(args, res)


def wsl_result(args, res):
    """(transcript, stderr text) of the wsl.exe run of wsl_inner_command(args)
    whose result is res; raises ConfigError when the copy inside WSL did not
    get as far as a transcript."""
    stderr_text = text_of(res.stderr)
    if res.timed_out:
        return make_transcript(b"", "[timeout]"), stderr_text + (
            "msil-run: wsl.exe did not finish within %g s\n"
            % (args.timeout + WSL_GRACE)
        )
    transcript = text_of(res.stdout)
    lines = transcript.rstrip("\n").split("\n")
    if res.returncode not in (0, 1) or not STATUS_RE.match(lines[-1]):
        raise ConfigError(
            "the WSL runner failed (wsl.exe exited with %s):\n%s%s"
            % (describe_exit(res.returncode), transcript, stderr_text)
        )
    return transcript, stderr_text


def pass_through(res, name, timeout, mem_limit_mb):
    """--tool: copy a tool's output and turn its result into our exit status."""
    emit(sys.stdout, res.stdout)
    emit(sys.stderr, res.stderr)
    if res.timed_out:
        sys.stderr.write("msil-run: %s did not finish within %g s\n" % (name, timeout))
        return 1
    if res.memory_limit:
        sys.stderr.write(
            "msil-run: %s hit the %d MiB memory cap\n" % (name, mem_limit_mb)
        )
        return 1
    rc = res.returncode
    if rc < 0:
        return 128 - rc  # POSIX signal, as a shell reports it
    if IS_WINDOWS and rc >= (1 << 31):
        rc -= 1 << 32  # sys.exit() takes a C long; Windows keeps all 32 bits
    return rc


def run_tool(args):
    """--tool: run args.program under the limits and pass everything through."""
    if args.runner == "wsl":
        return run_tool_wsl(args)
    program, private = args.program, None
    if args.stage:
        # A precaution: a DrvFs mount with 'metadata' and a restrictive fmask
        # (or a package extracted without the execute bit) makes the original
        # non-executable, so run an executable copy.
        if not os.path.isfile(program):
            raise ConfigError("%s not found" % program)
        private = tempfile.mkdtemp(prefix="msil-run-tool-")
        program = os.path.join(private, os.path.basename(args.program))
    try:
        if private:
            try:
                shutil.copyfile(args.program, program)
                os.chmod(program, 0o755)
            except OSError as e:
                raise ConfigError("cannot stage %s: %s" % (args.program, e))
        try:
            res = run_limited(
                [program] + args.args,
                None,
                dict(os.environ),
                args.timeout,
                args.mem_limit_mb,
            )
        except OSError as e:
            raise ConfigError("cannot run %s: %s" % (args.program, e))
    finally:
        if private:
            remove_tree(private)
    return pass_through(res, args.program, args.timeout, args.mem_limit_mb)


def run_tool_wsl(args):
    """--tool --runner wsl: run the Linux program args.program, which lives on
    a Windows drive, inside WSL through a copy of this script running there
    (--tool --stage), so the limits apply inside Linux."""
    if not IS_WINDOWS:
        raise ConfigError("--runner wsl needs a Windows host")
    if not os.path.isfile(args.program):
        raise ConfigError("%s not found" % args.program)
    cmd = [
        wsl_exe(),
        "-d",
        args.wsl_distro,
        "--cd",
        os.getcwd(),
        "--exec",
        "python3",
        to_wsl_path(os.path.abspath(__file__)),
        "--tool",
        "--stage",
        "--timeout",
        repr(args.timeout),
        "--mem-limit-mb",
        str(args.mem_limit_mb),
        "--",
        to_wsl_path(args.program),
    ] + [to_wsl_arg(a) for a in args.args]
    env = dict(os.environ)
    set_env_var(env, "WSL_UTF8", "1")  # wsl.exe's own messages, not UTF-16
    try:
        res = run_limited(cmd, None, env, args.timeout + WSL_GRACE, args.mem_limit_mb)
    except OSError as e:
        raise ConfigError("cannot run %s: %s" % (cmd[0], e))
    return pass_through(res, args.program, args.timeout + WSL_GRACE, args.mem_limit_mb)


def parse_expected_rule(value):
    """--expected-rule (plan 5.17.4; msil-prog.py's grammar and messages): None
    for an empty or blank value, else (the set of operating systems it names,
    the reason) for '<os>[,<os>]: <reason>'; raises ValueError otherwise."""
    text = value.strip()
    if not text:
        return None
    names, sep, reason = text.partition(":")
    if not sep:
        raise ValueError("%r is not '<os>[,<os>]: <reason>'" % text)
    oses = [n.strip() for n in names.split(",")]
    for name in oses:
        if name not in RULE_OSES:
            raise ValueError(
                "%r names %r, which is not an operating system of a rule "
                "(windows or linux)" % (text, name)
            )
    if len(set(oses)) != len(oses):
        raise ValueError("%r names an operating system twice" % text)
    reason = reason.strip()
    if not reason:
        raise ValueError("%r gives no reason" % text)
    if "'" in reason:
        raise ValueError(
            "the reason of %r holds a single quote (lit quotes the rule with "
            "single quotes)" % text
        )
    return frozenset(oses), reason


class _QuietParser(argparse.ArgumentParser):
    """A parser that raises UsageError instead of printing: --steps parses
    every step before its turn, and a step with a usage error then runs by
    itself in its turn, where main() prints the message."""

    def error(self, message):
        raise UsageError(message)

    def exit(self, status=0, message=None):
        raise UsageError(message or "exit status %d" % status)

    def print_help(self, file=None):
        raise UsageError("help")


# The options --steps refuses (dest, default): each step has its own.
_STEP_REFUSED = (
    ("native", False),
    ("netfx", False),
    ("tool", False),
    ("wsl_distro", "Ubuntu"),
    ("dotnet", None),
    ("runtimeconfig", None),
    ("env", []),
    ("stdin", None),
    ("dep", []),
    ("dep_dir", []),
    ("timeout", DEFAULT_TIMEOUT),
    ("mem_limit_mb", DEFAULT_MEM_LIMIT_MB),
    ("out", None),
    ("expect", None),
    ("expect_os", None),
    ("show_stderr", False),
    ("stage", False),
    ("stderr_out", None),
    ("skip_netfx", None),
    ("expected_rule", None),
)


def parse_args(argv, quiet=False):
    """The options of argv; quiet: raise UsageError instead of printing a
    usage error and exiting."""
    parser = _QuietParser if quiet else argparse.ArgumentParser
    p = parser(
        description=__doc__.splitlines()[0],
        usage="%(prog)s [options] [--] program [args...]",
    )
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--native", action="store_true", help="run a native executable")
    mode.add_argument(
        "--netfx", action="store_true", help="run a .exe on .NET Framework"
    )
    mode.add_argument(
        "--tool",
        action="store_true",
        help="run a tool under the limits; pass output and status through",
    )
    p.add_argument(
        "--runner",
        choices=("local", "wsl"),
        default="local",
        help="where to run a .dll, a --native program or a --tool (default: local)",
    )
    p.add_argument(
        "--wsl-distro",
        default="Ubuntu",
        help="WSL distribution for --runner wsl (default: Ubuntu)",
    )
    p.add_argument(
        "--dotnet",
        help="dotnet host (with --runner wsl: inside WSL, " "default 'dotnet')",
    )
    p.add_argument("--runtimeconfig", help="runtimeconfig.json for 'dotnet exec'")
    p.add_argument(
        "--env",
        action="append",
        default=[],
        metavar="VAR=VALUE",
        help="set an environment variable for the program (repeatable)",
    )
    p.add_argument("--stdin", help="file to feed on stdin (default: empty)")
    p.add_argument(
        "--dep",
        nargs="*",
        action="extend",
        default=[],
        metavar="FILE",
        help="copy these files next to the program (repeatable; takes every "
        "argument up to the next option, so end with '--' before the program)",
    )
    p.add_argument(
        "--dep-dir",
        action="append",
        default=[],
        metavar="DIR",
        help="copy everything in DIR next to the program (repeatable)",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        help="wall-clock limit in seconds (default %g)" % DEFAULT_TIMEOUT,
    )
    p.add_argument(
        "--mem-limit-mb",
        type=int,
        default=DEFAULT_MEM_LIMIT_MB,
        help="memory cap in MiB (default %d)" % DEFAULT_MEM_LIMIT_MB,
    )
    p.add_argument("--out", help="also write the transcript to this file")
    p.add_argument("--expect", metavar="STEM", help="compare with STEM[.<os>].expected")
    p.add_argument(
        "--expect-os",
        help="<os> for STEM.<os>.expected: the OS of the target triple "
        "(it decides sizeof(long) and the import libraries); "
        "default: linux with --runner wsl, else the host",
    )
    p.add_argument(
        "--show-stderr", action="store_true", help="always echo the program's stderr"
    )
    p.add_argument(
        "--stage",
        action="store_true",
        help="with --tool: run an executable copy of the tool from a private "
        "temporary directory (a precaution for a DrvFs mount with 'metadata' "
        "and a restrictive fmask, which makes the original non-executable)",
    )
    p.add_argument(
        "--stderr-out",
        metavar="FILE",
        help="also write the program's stderr to FILE",
    )
    p.add_argument(
        "--skip-netfx",
        metavar="REASON",
        help="with --netfx: run nothing, print 'netfx run skipped: REASON' and "
        "exit with status 0",
    )
    p.add_argument(
        "--expected-rule",
        metavar="RULE",
        help="with --native: '<os>[,<os>]: <reason>' (or empty); when it names "
        "--expect-os, skip the run (a hand-written expected file)",
    )
    p.add_argument(
        "--steps",
        metavar="FILE",
        help="with --runner wsl: run the msil-run.py and msil-jitall.py steps "
        "of FILE through one wsl.exe",
    )
    p.add_argument("--steps-inside", metavar="FILE", help=argparse.SUPPRESS)
    # Everything after the first '--' (or from the first positional on) is the
    # program and its arguments, even if it looks like an option.
    if "--" in argv:
        i = argv.index("--")
        args = p.parse_args(argv[:i])
        rest = argv[i + 1 :]
    else:
        p.add_argument("command", nargs=argparse.REMAINDER, help=argparse.SUPPRESS)
        args = p.parse_args(argv)
        rest = args.command
    args.rule = None
    if args.steps is not None or args.steps_inside is not None:
        refused = [d for d, default in _STEP_REFUSED if getattr(args, d) != default]
        if (
            rest
            or refused
            or (args.steps is not None) == (args.steps_inside is not None)
        ):
            p.error("--steps FILE takes no program and no option but --runner wsl")
        if args.steps is not None and args.runner != "wsl":
            p.error("--steps needs --runner wsl")
        args.program, args.args = None, []
        return args
    if not rest:
        if args.dep:
            p.error(
                "no program given (--dep takes every argument up to the next "
                "option: put '--' before the program)"
            )
        p.error("no program given")
    args.program, args.args = rest[0], rest[1:]
    if args.tool and (args.dep or args.dep_dir):
        p.error("--dep and --dep-dir copy files next to a program, not a --tool")
    if args.timeout <= 0 or args.mem_limit_mb <= 0:
        p.error("--timeout and --mem-limit-mb must be positive")
    if args.runner == "wsl" and args.netfx:
        p.error("--runner wsl runs .dll programs, --native programs and --tool only")
    if args.stage and not args.tool:
        p.error("--stage needs --tool")
    if args.expect_os is None:
        args.expect_os = "linux" if args.runner == "wsl" else host_os()
    if args.netfx and not IS_WINDOWS:
        p.error("--netfx is only available on Windows")
    if args.stderr_out is not None and args.tool:
        p.error("--stderr-out writes a program's stderr, not a --tool's")
    if args.skip_netfx is not None:
        if not args.netfx:
            p.error("--skip-netfx needs --netfx")
        if not args.skip_netfx.strip():
            p.error("--skip-netfx needs a reason")
    if args.expected_rule is not None:
        if not args.native:
            p.error("--expected-rule needs --native")
        try:
            args.rule = parse_expected_rule(args.expected_rule)
        except ValueError as e:
            p.error("--expected-rule: %s" % e)
    return args


def exit_status(e):
    """The status of argparse's SystemExit (2 for a usage error, 0 for --help)."""
    return e.code if isinstance(e.code, int) else (0 if e.code is None else 2)


def skip_message(args):
    """The line of a skipped run (--skip-netfx, or an --expected-rule that
    names --expect-os), without the 'msil-run: ' prefix, or None."""
    if args.program is None:
        return None
    if args.skip_netfx is not None:
        return NETFX_SKIP % args.skip_netfx
    if args.rule is not None and args.expect_os in args.rule[0]:
        return RULE_SKIP % args.rule[1]
    return None


def planned_skip(argv):
    """For msil-steps.py: (the skip line, the --stderr-out FILE or None) when
    the msil-run.py command line argv is a skipped run, else None."""
    try:
        args = parse_args(list(argv), quiet=True)
    except UsageError:
        return None
    message = skip_message(args)
    return (message, args.stderr_out) if message is not None else None


def remove_stale(*paths):
    """Remove the output files of an earlier run (best effort)."""
    for path in paths:
        if path:
            try:
                os.remove(path)
            except OSError:
                pass


def main(argv=None):
    try:
        args = parse_args(sys.argv[1:] if argv is None else list(argv))
    except SystemExit as e:
        return exit_status(e)
    if args.steps_inside is not None:
        return run_steps_inside(args.steps_inside)
    if args.steps is not None:
        try:
            steps = read_steps(args.steps)
        except ConfigError as e:
            sys.stderr.write("msil-run: error: %s\n" % e)
            return 2
        return run_steps(steps)[0]
    skip = skip_message(args)
    if skip is not None:
        remove_stale(args.out, args.stderr_out)
        emit(sys.stdout, "msil-run: %s\n" % skip)
        return 0
    remove_stale(args.stderr_out)
    try:
        if args.tool:
            return run_tool(args)
        if args.runner == "wsl":
            out, stderr_text = run_wsl(args)
        else:
            out, stderr_text = run_local(args)
    except ConfigError as e:
        sys.stderr.write("msil-run: error: %s\n" % e)
        return 2
    return finish(args, out, stderr_text)


def finish(args, out, stderr_text):
    """Report a run that produced the transcript out and the stderr text
    stderr_text (main() after the run); return the exit status."""
    if args.show_stderr and stderr_text:
        emit(sys.stderr, stderr_text)
    if args.stderr_out:
        with open(args.stderr_out, "w", encoding="utf-8", newline="\n") as f:
            f.write(stderr_text)
    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
    status = out.rstrip("\n").split("\n")[-1]
    outside = OUTSIDE_RE.match(status)
    if outside:
        sys.stderr.write(
            "msil-run: %s exited with status %s, outside 0..255; Linux would "
            "truncate it, so it never passes (plan D37)\n"
            % (args.program, outside.group(1))
        )
    if not args.expect:
        emit(sys.stdout, out)
        return 1 if outside else 0

    exp_path = expected_file(args.expect, args.expect_os)
    if exp_path is None:
        sys.stderr.write(
            "msil-run: no expected file for %s (%s.expected or %s.%s.expected)\n"
            % (args.program, args.expect, args.expect, args.expect_os)
        )
        sys.stderr.write("---- actual ----\n" + out)
        if stderr_text and not args.show_stderr:
            sys.stderr.write("---- program stderr ----\n" + stderr_text)
        return 1
    with open(exp_path, "r", encoding="utf-8", newline="") as f:
        expected = fold_newlines(f.read())
    if expected == out and not outside:
        return 0
    if expected != out:
        sys.stderr.write(
            "msil-run: output of %s does not match %s\n" % (args.program, exp_path)
        )
        sys.stderr.writelines(
            difflib.unified_diff(
                expected.splitlines(True), out.splitlines(True), exp_path, "actual"
            )
        )
    if stderr_text and not args.show_stderr:
        sys.stderr.write("---- program stderr ----\n" + stderr_text)
    return 1


# ---------------------------------------------------------------- --steps
# msil-run.py --runner wsl --steps FILE (plan 5.17.10; see the docstring).
# Each script that may be a step (this one and msil-jitall.py) provides
# batch_prepare(argv), which parses and checks the step without printing
# anything and returns a Prepared step for a run that goes into the shared
# wsl.exe (None: the step runs by itself in its turn, through main(argv)),
# and batch_report(prepared, result), which judges and reports the step from
# the result of its command inside WSL exactly as its own wsl.exe run would
# have been, and returns the exit status.
STEP_SCRIPTS = ("msil-run.py", "msil-jitall.py")


class Prepared:
    """A step of --steps that runs inside a shared wsl.exe: the command after
    'wsl.exe -d <distro> --exec', its wall-clock limit inside WSL, its memory
    cap, and when the copy inside WSL must stop after it: always (stop), or
    unless its stdout is the transcript expect (or None)."""

    def __init__(
        self, args, distro, argv, limit, mem_limit_mb, expect=None, stop=False
    ):
        self.args = args
        self.distro = distro
        self.argv = argv
        self.limit = limit
        self.mem_limit_mb = mem_limit_mb
        self.expect = expect
        self.stop = stop

    def inner_spec(self):
        return {
            "argv": self.argv,
            "limit": self.limit,
            "expect": self.expect,
            "stop": self.stop,
        }


def batch_prepare(argv):
    """--steps: the Prepared step of the msil-run.py command line argv when it
    runs a program in WSL, else None; prints nothing."""
    try:
        args = parse_args(list(argv), quiet=True)
    except UsageError:
        return None
    if (
        args.program is None
        or args.tool
        or args.runner != "wsl"
        or skip_message(args) is not None
    ):
        return None
    try:
        inner = wsl_inner_command(args)
    except ConfigError:
        return None
    expect, stop = None, False
    if args.expect:
        exp_path = expected_file(args.expect, args.expect_os)
        if exp_path is None:
            stop = True  # it fails: 'no expected file'
        else:
            try:
                with open(exp_path, "r", encoding="utf-8", newline="") as f:
                    expect = fold_newlines(f.read())
            except (OSError, ValueError):
                return None
    return Prepared(
        args,
        args.wsl_distro,
        inner,
        args.timeout + WSL_GRACE,
        args.mem_limit_mb,
        expect,
        stop,
    )


def batch_report(prepared, res):
    """--steps: report the step prepared, whose command ran inside a shared
    wsl.exe with the result res, as main() reports its own run; return the
    exit status."""
    args = prepared.args
    remove_stale(args.stderr_out)
    try:
        if res.error:
            raise ConfigError(res.error)
        out, stderr_text = wsl_result(args, res)
    except ConfigError as e:
        sys.stderr.write("msil-run: error: %s\n" % e)
        return 2
    return finish(args, out, stderr_text)


def read_steps(path):
    """The steps of --steps FILE (see the docstring); raises ConfigError."""
    try:
        with open(path, encoding="utf-8") as f:
            spec = json.load(f)
    except (OSError, ValueError) as e:
        raise ConfigError("cannot read --steps %s: %s" % (path, e))
    steps = spec.get("steps") if isinstance(spec, dict) else None
    if (
        not isinstance(spec, dict)
        or spec.get("version") != STEPS_VERSION
        or not isinstance(steps, list)
    ):
        raise ConfigError(
            "--steps %s is not a steps file of version %d" % (path, STEPS_VERSION)
        )
    for n, step in enumerate(steps, 1):
        if not (
            isinstance(step, dict)
            and step.get("script") in STEP_SCRIPTS
            and isinstance(step.get("argv"), list)
            and all(isinstance(a, str) for a in step["argv"])
            and isinstance(step.get("display", ""), str)
        ):
            raise ConfigError(
                '--steps %s: step %d is not {"script": "msil-run.py" or '
                '"msil-jitall.py", "argv": [ARG...]}' % (path, n)
            )
    return steps


class _ThisScript:
    """This module as a step script (msil-steps.py loads it under another
    name, so sys.modules cannot name it)."""

    def __init__(self):
        self.__file__ = os.path.abspath(__file__)
        self.main = main
        self.batch_prepare = batch_prepare
        self.batch_report = batch_report


_step_scripts = {}


def load_step_script(script):
    """The module of a step script of --steps: this one, or msil-jitall.py
    (loaded once; it loads its own copy of this file)."""
    if script not in _step_scripts:
        if script == "msil-run.py":
            _step_scripts[script] = _ThisScript()
        else:
            saved = sys.dont_write_bytecode  # no __pycache__ in msil/utils
            sys.dont_write_bytecode = True
            try:
                spec = importlib.util.spec_from_file_location(
                    "msil_steps_" + script.replace("-", "_").replace(".", "_"),
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), script),
                )
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
            finally:
                sys.dont_write_bytecode = saved
            _step_scripts[script] = module
    return _step_scripts[script]


@contextlib.contextmanager
def as_script(path):
    """Run a script's main() in this process under its own name (argparse
    takes the program name of its messages from sys.argv[0])."""
    saved = sys.argv
    sys.argv = [path] + list(saved[1:])
    try:
        yield
    finally:
        sys.argv = saved


def call_main(module, argv):
    """module.main(argv) under the script's own name; its exit status."""
    with as_script(module.__file__):
        try:
            status = module.main(list(argv))
        except SystemExit as e:
            status = exit_status(e)
    sys.stdout.flush()
    sys.stderr.flush()
    return 0 if status is None else status


def run_group(prepared):
    """Run the commands of the Prepared steps prepared (one distribution)
    inside one wsl.exe; return one LimitedResult per step."""
    tmp = tempfile.mkdtemp(prefix="msil-run-steps-")
    wsl = wsl_exe()
    limit = sum(p.limit for p in prepared) + WSL_GRACE
    try:
        inner = os.path.join(tmp, "steps.json")
        with open(inner, "w", encoding="utf-8", newline="\n") as f:
            json.dump(
                {"version": STEPS_VERSION, "steps": [p.inner_spec() for p in prepared]},
                f,
            )
        cmd = [
            wsl,
            "-d",
            prepared[0].distro,
            "--exec",
            "python3",
            to_wsl_path(os.path.abspath(__file__)),
            "--steps-inside",
            to_wsl_path(inner),
        ]
        res = run_limited(
            cmd, None, wsl_env(), limit, max(p.mem_limit_mb for p in prepared)
        )
    except (OSError, ConfigError) as e:
        failed = LimitedResult(None, b"", b"", False, False)
        failed.error = "cannot run %s: %s" % (wsl, e)
        return [failed] * len(prepared)
    finally:
        remove_tree(tmp)
    records, other = {}, []
    for line in res.stdout.split(b"\n"):
        text = line.rstrip(b"\r").decode("utf-8", "replace")
        if text.startswith(STEP_RESULT):
            try:
                rec = json.loads(text[len(STEP_RESULT) :])
                records[rec["step"]] = LimitedResult(
                    rec["rc"],
                    base64.b64decode(rec["stdout"]),
                    base64.b64decode(rec["stderr"]),
                    bool(rec["timed_out"]),
                    False,
                )
                continue
            except (ValueError, KeyError, TypeError):
                pass
        other.append(line)
    rest = b"\n".join(other).strip(b"\r\n")
    results = []
    for k in range(len(prepared)):
        if k in records:
            results.append(records[k])
        elif res.timed_out:
            # wsl.exe itself ran out of time; the step's own limit is the
            # one it reports.
            results.append(LimitedResult(None, b"", b"", True, False))
        else:
            # The copy inside WSL never got to this step (it failed itself,
            # or wsl.exe did): as from a wsl.exe of the step's own.
            results.append(LimitedResult(res.returncode, rest, b"", False, False))
    # wsl.exe's own messages (and those of the copy inside WSL) go with the
    # first step without a result of its own, else with the first step.
    missing = [k for k in range(len(prepared)) if k not in records]
    target = results[missing[0] if missing else 0]
    target.stderr = res.stderr + target.stderr
    return results


def run_steps(steps, header=None, load=None):
    """Run the steps of --steps (dicts with 'script' and 'argv', optionally
    'name' and 'display') in order; return (the exit status of the first
    failing step, else 0; its index, else None). header(i) prints the header
    of step i before its output (default: its 'display' line, if any);
    load(script) returns a step script's module (default: load_step_script)."""
    load = load or load_step_script
    modules = [load(s["script"]) for s in steps]
    prepared = [m.batch_prepare(list(s["argv"])) for m, s in zip(modules, steps)]
    results = {}
    for i, step in enumerate(steps):
        if prepared[i] is not None and i not in results:
            # The run of consecutive inner steps of one distribution from here.
            j = i
            while (
                j < len(steps)
                and prepared[j] is not None
                and prepared[j].distro == prepared[i].distro
            ):
                j += 1
            for k, res in zip(range(i, j), run_group(prepared[i:j])):
                results[k] = res
        if header is not None:
            header(i)
        elif step.get("display"):
            emit(sys.stdout, step["display"] + "\n")
        if prepared[i] is None:
            status = call_main(modules[i], step["argv"])
        else:
            with as_script(modules[i].__file__):
                status = modules[i].batch_report(prepared[i], results[i])
            sys.stdout.flush()
            sys.stderr.flush()
        if status != 0:
            return status, i
    return 0, None


def run_steps_inside(path):
    """--steps-inside FILE (inside WSL, for --steps): run each command of
    FILE in order under its wall-clock limit, print one result line per
    command, and stop after the first one that cannot pass (a status other
    than 0, a timeout, the step's stop flag, or stdout other than the
    expected transcript)."""
    try:
        with open(path, encoding="utf-8") as f:
            steps = json.load(f)["steps"]
    except (OSError, ValueError, KeyError, TypeError) as e:
        sys.stderr.write("msil-run: error: cannot read %s: %s\n" % (path, e))
        return 2
    for i, step in enumerate(steps):
        timed_out = False
        try:
            proc = subprocess.Popen(
                step["argv"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                start_new_session=not IS_WINDOWS,
            )
        except OSError as e:
            rc, out = 1, b""
            err = ("msil-run: cannot run %s: %s\n" % (step["argv"][0], e)).encode()
        else:
            try:
                out, err = proc.communicate(timeout=step["limit"])
            except subprocess.TimeoutExpired:
                timed_out = True
                if IS_WINDOWS:
                    proc.kill()
                else:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except OSError:
                        pass
                out, err = proc.communicate()
            rc = proc.returncode
        record = {
            "step": i,
            "rc": rc,
            "timed_out": timed_out,
            "stdout": base64.b64encode(out).decode("ascii"),
            "stderr": base64.b64encode(err).decode("ascii"),
        }
        sys.stdout.write(STEP_RESULT + json.dumps(record) + "\n")
        sys.stdout.flush()
        if timed_out or rc != 0 or step.get("stop"):
            break
        if step.get("expect") is not None and text_of(out) != step["expect"]:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
