"""Replay the '# executed command:' lines of a lit -a log, one at a time, each inside its own
job object; print wall, CPU (user+kernel of the whole tree) and process count per command."""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time, json, re, os
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateJobObjectW.restype = wt.HANDLE
k32.CreateJobObjectW.argtypes = (ctypes.c_void_p, wt.LPCWSTR)
k32.AssignProcessToJobObject.argtypes = (wt.HANDLE, wt.HANDLE)
k32.QueryInformationJobObject.argtypes = (wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p)
k32.OpenProcess.restype = wt.HANDLE
k32.ResumeThread.argtypes = (wt.HANDLE,)
class BASIC(ctypes.Structure):
    _fields_ = [("TotalUserTime", ctypes.c_longlong), ("TotalKernelTime", ctypes.c_longlong),
                ("a", ctypes.c_longlong), ("b", ctypes.c_longlong), ("PF", wt.DWORD), ("TotalProcesses", wt.DWORD),
                ("Active", wt.DWORD), ("Term", wt.DWORD)]
log, bash = sys.argv[1], sys.argv[2]
reps = int(sys.argv[3]) if len(sys.argv) > 3 else 1
cmds = [l[len("# executed command: "):].rstrip("\n") for l in open(log, encoding="utf-8", errors="replace")
        if l.startswith("# executed command: ")]
def classify(c):
    if "msil-prog.py" in c and "--mode native" in c: return "native-build"
    if "msil-prog.py" in c: return "build(+equiv link)" if "--equiv-out" in c else "build"
    if "msil-run.py" in c and "--tool" in c: return "ilasm(asm ref)"
    if "msil-run.py" in c and "--native" in c: return "native-run"
    if "msil-run.py" in c and "--netfx" in c: return "netfx-run"
    if "msil-run.py" in c and "TieredCompilation=0" in c: return "run net8+tc0"
    if "msil-run.py" in c and "also-net10" in c: return "run net10"
    if "msil-run.py" in c: return "run net8"
    if "msil-jitall.py" in c and "TieredCompilation=0" in c: return "jitall net8+tc0"
    if "msil-jitall.py" in c and "10.0" in c and "runtimeconfig" in c and "also" in c: return "jitall net10"
    if "msil-jitall.py" in c: return "jitall"
    if "msil-verify.py" in c: return "verify"
    if "msil-equiv.py" in c: return "equiv O3/O4"
    if "clang.exe" in c and " -S " in c: return "clang -S (asm ref)"
    if c.startswith("mkdir"): return "mkdir"
    return "other"
rows = []
for rep in range(reps):
    for i, c in enumerate(cmds):
        job = k32.CreateJobObjectW(None, None)
        CREATE_SUSPENDED = 0x4
        t0 = time.perf_counter()
        # bash -c runs the command as lit's internal shell would (paths are absolute)
        p = subprocess.Popen([bash, "-c", c], creationflags=CREATE_SUSPENDED, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        h = k32.OpenProcess(0x1F0FFF, False, p.pid)
        k32.AssignProcessToJobObject(job, h)
        # resume the main thread: use NtResumeProcess
        ntdll = ctypes.WinDLL("ntdll"); ntdll.NtResumeProcess.argtypes = (wt.HANDLE,); ntdll.NtResumeProcess(h)
        rc = p.wait()
        wall = time.perf_counter() - t0
        b = BASIC(); k32.QueryInformationJobObject(job, 1, ctypes.byref(b), ctypes.sizeof(b), None)
        cpu = (b.TotalUserTime + b.TotalKernelTime) / 1e7
        rows.append(dict(rep=rep, i=i, step=classify(c), rc=rc, wall=round(wall, 2), cpu=round(cpu, 2), procs=b.TotalProcesses))
        print(json.dumps(rows[-1]), flush=True)
json.dump(rows, open(log + ".replay.json", "w"), indent=0)
