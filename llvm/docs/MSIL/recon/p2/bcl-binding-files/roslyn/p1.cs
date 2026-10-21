// The Phase-1 BCL members, written in C#, to see which AssemblyRefs Roslyn emits.
unsafe struct Img { fixed byte b[16]; }
static class P
{
    static unsafe int Main()
    {
        System.IntPtr p = System.Runtime.InteropServices.Marshal.AllocHGlobal((System.IntPtr)64);
        string[] a = System.Environment.GetCommandLineArgs();
        System.IntPtr s = System.Runtime.InteropServices.Marshal.StringToHGlobalAnsi(a[0]);
        System.Buffer.MemoryCopy((void*)s, (void*)p, 8, 8);
        System.Threading.Interlocked.MemoryBarrier();
        return 42;
    }
}
