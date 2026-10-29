// Does a vectored exception handler (AddVectoredExceptionHandler, as Rust std registers for its
// stack-overflow detection) see managed throws? On which runtimes, and does calling back into
// managed code from it (a delegate thunk, as Phase 3 would give a C/Rust handler) survive?
using System;
using System.Runtime.InteropServices;
using System.Threading;

static class P
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    delegate int Handler(IntPtr exceptionPointers);

    [DllImport("kernel32.dll")] static extern IntPtr AddVectoredExceptionHandler(uint first, Handler h);
    [DllImport("kernel32.dll")] static extern uint RemoveVectoredExceptionHandler(IntPtr h);

    static int calls;
    static uint lastCode;
    static Handler keep;

    static int OnException(IntPtr ep)
    {
        Interlocked.Increment(ref calls);
        // EXCEPTION_POINTERS { EXCEPTION_RECORD* ExceptionRecord; CONTEXT* ContextRecord; }
        IntPtr rec = Marshal.ReadIntPtr(ep);
        lastCode = (uint)Marshal.ReadInt32(rec);
        return 0; // EXCEPTION_CONTINUE_SEARCH, as Rust std's handler does for codes it does not own
    }

    static int Main(string[] args)
    {
        string mode = args.Length > 0 ? args[0] : "both";
        uint first = args.Length > 1 ? uint.Parse(args[1]) : 1u;
        keep = OnException;
        IntPtr h = AddVectoredExceptionHandler(first, keep);
        int caught = 0;
        for (int i = 0; i < (mode == "nre" ? 0 : 5); i++)
        {
            try { throw new InvalidOperationException("x" + i); }
            catch (InvalidOperationException) { caught++; }
        }
        int nre = 0;
        if (mode != "throw") { try { object o = null; o.GetHashCode(); } catch (NullReferenceException) { nre++; } }
        RemoveVectoredExceptionHandler(h);
        Console.WriteLine($"mode={mode} first={first} {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription}: caught={caught} nre={nre} handlerCalls={calls} lastCode=0x{lastCode:X8}");
        return 0;
    }
}
