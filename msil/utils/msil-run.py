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
    msil-run.py [OPTIONS] --runner wsl [--dotnet D] --runtimeconfig C [--] prog.dll
    msil-run.py [OPTIONS] --netfx [--] prog.exe [ARGS...]      (.NET Framework)
    msil-run.py [OPTIONS] --native [--] prog.exe [ARGS...]     (native oracle)
    msil-run.py [OPTIONS] --native --runner wsl [--] prog.elf  (E7 Linux oracle)
    msil-run.py --tool [--timeout S] [--mem-limit-mb N] -- TOOL [ARGS...]
    msil-run.py --tool --runner wsl [--wsl-distro D] [LIMITS] -- TOOL [ARGS...]

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
    [signal N]                    killed by signal N (POSIX)
    [timeout]                     still running after --timeout seconds
    [memory limit]                hit the --mem-limit-mb memory cap

'[no newline at end of output]' comes first when stdout does not end in a
newline. Linux keeps only the low 8 bits of an exit status, so a status above
255 that Windows reports in full is never accepted (plan D37). N is decimal
(signed on Windows), except for Windows exception and HRESULT codes
(0x80000000..0xEFFFFFFF, for example 0xC0000005), which are hexadecimal.

The program runs from a fresh private directory that holds only a copy of it
(and of its runtimeconfig.json): two assemblies with the same name in one
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

With --expect STEM the transcript is compared with STEM.<os>.expected if that
file exists, else STEM.expected; <os> (windows, linux or darwin) comes from
--expect-os, which lit sets from the target triple (default: linux with
--runner wsl, else the host's). A mismatch prints a unified diff and exits 1.
--out FILE also writes the transcript to FILE. Without --expect the
transcript goes to stdout.

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

Exit status: 0 on success; 1 on a mismatch or when the program's exit status
is outside 0..255 (the transcript is still written); 2 on a usage or
configuration error. With --tool, the tool's own status (1 when it hit the
timeout or the memory cap, 2 when it could not be started).
"""

import argparse
import difflib
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
    r"|timeout|memory limit)\]$"
)
OUTSIDE_RE = re.compile(r"^\[exit (-?\w+): outside 0\.\.255\]$")
NEWLINE_RE = re.compile(r"\r+\n")


class ConfigError(Exception):
    pass


class LimitedResult:
    def __init__(self, returncode, stdout, stderr, timed_out, memory_limit):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        self.timed_out = timed_out
        self.memory_limit = memory_limit


def host_startup_failure(stderr_text):
    """The message with which the dotnet host or CoreCLR refused to start the
    program (it never ran), or None. msil-jitall.py uses it too."""
    m = HOST_STARTUP_RE.search(stderr_text)
    return m.group(0).strip() if m else None


# run_limited(), OOM_RE, host_startup_failure(), child_env(), remove_tree(),
# to_wsl_path(), text_of(), fold_newlines() (the transcript newline rule) and
# WSL_GRACE are shared: msil-verify.py, msil-jitall.py and
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


def describe_exit(rc):
    """An exit status as text: decimal, except that Windows NTSTATUS and HRESULT
    style codes (0x80000000..0xEFFFFFFF, for example 0xC0000005 or the CLR's
    0xE0434352) are hexadecimal; 0xF0000000 and above are small negative ints."""
    if IS_WINDOWS and rc >= (1 << 31):
        if rc < 0xF0000000:
            return "0x%08X" % rc
        return "%d" % (rc - (1 << 32))
    return "%d" % rc


def status_line(res, stderr_text):
    if res.timed_out:
        return "[timeout]"
    if res.memory_limit:
        return "[memory limit]"
    m = UNHANDLED_RE.search(stderr_text)
    if m:
        return "[unhandled exception: %s]" % m.group(1)
    rc = res.returncode
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
        if dotnet:
            config = os.path.join(
                private, os.path.splitext(name)[0] + ".runtimeconfig.json"
            )
            shutil.copyfile(args.runtimeconfig, config)
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
    return make_transcript(res.stdout, status_line(res, stderr_text)), stderr_text


def run_wsl(args):
    """Run 'dotnet exec' (or, with --native, a Linux executable) in WSL
    through a copy of this script running there."""
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
    wsl = wsl_exe()
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
        [wsl, "-d", args.wsl_distro, "--exec", "env"]
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
    cmd += ["--", to_wsl_path(args.program)] + args.args
    env = dict(os.environ)
    set_env_var(env, "WSL_UTF8", "1")  # wsl.exe's own messages, not UTF-16
    try:
        res = run_limited(cmd, None, env, args.timeout + WSL_GRACE, args.mem_limit_mb)
    except OSError as e:
        raise ConfigError("cannot run %s: %s" % (wsl, e))
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


def parse_args(argv):
    p = argparse.ArgumentParser(
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
    if not rest:
        p.error("no program given")
    args.program, args.args = rest[0], rest[1:]
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
    return args


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
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

    if args.show_stderr and stderr_text:
        emit(sys.stderr, stderr_text)
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


if __name__ == "__main__":
    sys.exit(main())
