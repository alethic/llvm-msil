"""Run a command inside a fresh Windows job object and report wall time, the job's total CPU
time (user + kernel, every process of the tree) and the number of processes it started.
usage: jobrun.py [--label L] -- <command...>"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time, json
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateJobObjectW.restype = wt.HANDLE
k32.CreateJobObjectW.argtypes = (ctypes.c_void_p, wt.LPCWSTR)
k32.GetCurrentProcess.restype = wt.HANDLE
k32.AssignProcessToJobObject.argtypes = (wt.HANDLE, wt.HANDLE)
k32.QueryInformationJobObject.argtypes = (wt.HANDLE, ctypes.c_int, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p)
class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ("ReadOperationCount","WriteOperationCount","OtherOperationCount","ReadTransferCount","WriteTransferCount","OtherTransferCount")]
class BASIC_ACCT(ctypes.Structure):
    _fields_ = [("TotalUserTime", ctypes.c_longlong), ("TotalKernelTime", ctypes.c_longlong),
                ("ThisPeriodTotalUserTime", ctypes.c_longlong), ("ThisPeriodTotalKernelTime", ctypes.c_longlong),
                ("TotalPageFaultCount", wt.DWORD), ("TotalProcesses", wt.DWORD),
                ("ActiveProcesses", wt.DWORD), ("TotalTerminatedProcesses", wt.DWORD)]
class BASIC_AND_IO(ctypes.Structure):
    _fields_ = [("BasicInfo", BASIC_ACCT), ("IoInfo", IO_COUNTERS)]
args = sys.argv[1:]
label = ""
if args[:1] == ["--label"]:
    label = args[1]; args = args[2:]
if args[:1] == ["--"]: args = args[1:]
job = k32.CreateJobObjectW(None, None)
if not job: raise OSError(ctypes.get_last_error())
# Put ourselves in the job: every child inherits it (no breakaway is requested).
if not k32.AssignProcessToJobObject(job, k32.GetCurrentProcess()):
    raise OSError("AssignProcessToJobObject failed: %d" % ctypes.get_last_error())
info0 = BASIC_AND_IO(); k32.QueryInformationJobObject(job, 8, ctypes.byref(info0), ctypes.sizeof(info0), None)
t0 = time.perf_counter()
rc = subprocess.call(args)
wall = time.perf_counter() - t0
info = BASIC_AND_IO(); k32.QueryInformationJobObject(job, 8, ctypes.byref(info), ctypes.sizeof(info), None)
b, b0 = info.BasicInfo, info0.BasicInfo
cpu = ((b.TotalUserTime - b0.TotalUserTime) + (b.TotalKernelTime - b0.TotalKernelTime)) / 1e7
res = dict(label=label, rc=rc, wall_s=round(wall, 2), cpu_s=round(cpu, 2),
           user_s=round((b.TotalUserTime - b0.TotalUserTime) / 1e7, 2),
           kernel_s=round((b.TotalKernelTime - b0.TotalKernelTime) / 1e7, 2),
           processes=b.TotalProcesses - b0.TotalProcesses)
sys.stderr.write("JOBRUN " + json.dumps(res) + "\n")
sys.exit(rc)
