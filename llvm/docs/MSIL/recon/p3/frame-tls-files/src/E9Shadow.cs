// E9: the proposed shadow stack for dynamic allocas (option B of the frame-tls recon), as C# that
// mirrors the private IL helpers msil-ld would emit: the state lives in the 32+8-byte header of the
// per-thread block (E2/proto_tls), the stack is a list of AllocHGlobal chunks that grows on demand,
// stacksave is a read of Sp, stackrestore a write that pops chunks when the saved pointer lies in an
// older chunk (one popped chunk is kept as a spare, so a loop that crosses a chunk boundary does not
// allocate every iteration).
//   1. a VLA in a loop, 1,000,000 iterations, size 1..4096 (clang: stacksave/alloca/stackrestore)
//   2. recursion 20,000 deep with a 1 KiB VLA per frame (20 MiB: more than any CLR thread stack here)
//   3. "longjmp": an exception thrown through frames with VLAs, caught by the setjmp caller, which
//      restores the Sp it saved at setjmp time (the sjlj lowering's catch path)
//   4. cost per (save, alloca, restore) against localloc
using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

unsafe struct Hdr { public byte* Sp; public byte* Lo; public byte* Hi; public Chunk* Cur; public Chunk* Spare; }
unsafe struct Chunk { public Chunk* Prev; public long Size; public long Pad0, Pad1; }   // 32 bytes, then the stack

unsafe static class SS {
  [ThreadStatic] static Hdr* blk;
  public static long ChunkAllocs, ChunkFrees;
  public const long MinChunk = 64 * 1024;

  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static Hdr* Tls() { Hdr* h = blk; return h != null ? h : Init(); }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static Hdr* Init() { Hdr* h = (Hdr*)Marshal.AllocHGlobal(sizeof(Hdr)); *h = default(Hdr); blk = h; return h; }

  // llvm.stacksave
  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static byte* Save() { return Tls()->Sp; }
  // dynamic alloca of n bytes aligned to a (a power of two)
  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static byte* Alloc(long n, long a) {
    Hdr* h = Tls();
    if ((ulong)(h->Sp - h->Lo) < (ulong)(n + a - 1)) return AllocSlow(h, n, a);   // also when no chunk (Sp = Lo = 0)
    byte* p = (byte*)((long)(h->Sp - n) & -a); h->Sp = p; return p;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static byte* AllocSlow(Hdr* h, long n, long a) {
    long need = n + a - 1 + sizeof(Chunk);
    Chunk* c = h->Spare;
    if (c != null && c->Size >= need) h->Spare = null;
    else {
      long size = Math.Max(need, Math.Max(MinChunk, h->Cur != null ? 2 * h->Cur->Size : 0));
      c = (Chunk*)Marshal.AllocHGlobal((IntPtr)size); c->Size = size; ChunkAllocs++;
      if (size > 1L << 40) Environment.FailFast("shadow stack overflow");
    }
    c->Prev = h->Cur; h->Cur = c;
    h->Lo = (byte*)c + sizeof(Chunk); h->Hi = (byte*)c + c->Size; h->Sp = h->Hi;
    byte* p = (byte*)((long)(h->Sp - n) & -a); h->Sp = p; return p;
  }
  // llvm.stackrestore
  [MethodImpl(MethodImplOptions.AggressiveInlining)]
  public static void Restore(byte* p) {
    Hdr* h = Tls();
    if (p >= h->Lo && p <= h->Hi && h->Cur != null) h->Sp = p; else RestoreSlow(h, p);
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static void RestoreSlow(Hdr* h, byte* p) {
    // p == null: the Sp saved before the first chunk existed. Keep the bottom chunk (freed with the
    // block at thread exit) and make it empty, so later saves are inside it and take the fast path.
    while (h->Cur != null && (p == null ? h->Cur->Prev != null : !(p >= (byte*)h->Cur + sizeof(Chunk) && p <= (byte*)h->Cur + h->Cur->Size))) {
      Chunk* c = h->Cur; h->Cur = c->Prev;
      if (h->Spare == null || h->Spare->Size < c->Size) { if (h->Spare != null) { Marshal.FreeHGlobal((IntPtr)h->Spare); ChunkFrees++; } h->Spare = c; }
      else { Marshal.FreeHGlobal((IntPtr)c); ChunkFrees++; }
    }
    if (h->Cur == null) { h->Sp = h->Lo = h->Hi = null; return; }   // no chunk yet
    h->Lo = (byte*)h->Cur + sizeof(Chunk); h->Hi = (byte*)h->Cur + h->Cur->Size; h->Sp = p == null ? h->Hi : p;
  }
  public static int Chunks() { int k = 0; for (Chunk* c = Tls()->Cur; c != null; c = c->Prev) k++; return k; }
}

sealed class LongJmp : Exception { public IntPtr Buf; public int Val; public LongJmp(IntPtr b, int v) { Buf = b; Val = v; } }

unsafe static class E9Shadow {
  // void f(int n) { for (i...) { char vla[n_i]; use(vla); } }  -- clang -O0 shape with save/restore per scope
  static long VlaLoop(int iters) {
    byte* fsp = SS.Save(); long sum = 0;
    for (int i = 0; i < iters; i++) {
      byte* s = SS.Save();
      int n = 1 + (int)((i * 2654435761u) % 4096);
      byte* v = SS.Alloc(n, 16); v[0] = (byte)i; v[n - 1] = 1; sum += v[0] + v[n - 1];
      SS.Restore(s);
    }
    SS.Restore(fsp); return sum;
  }
  static long Recurse(int d) {
    byte* fsp = SS.Save();
    byte* v = SS.Alloc(1024, 16); v[0] = (byte)d; v[1023] = 1;
    long r = d == 0 ? 0 : Recurse(d - 1);
    r += v[0] + v[1023];
    SS.Restore(fsp); return r;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static void Thrower(int d, IntPtr jb) {
    byte* fsp = SS.Save();
    byte* v = SS.Alloc(3000, 16); v[0] = 1;
    if (d == 0) throw new LongJmp(jb, 7);        // longjmp(jb, 7): no stackrestore on this path
    Thrower(d - 1, jb);
    SS.Restore(fsp);
  }
  static int SetjmpCaller(int depth, bool restoreOnCatch) {
    byte* fsp = SS.Save();
    byte* mine = SS.Alloc(100, 16);              // a VLA of the setjmp caller, live across the setjmp
    IntPtr jb = (IntPtr)mine;
    byte* atSetjmp = SS.Save();                   // saved by the setjmp lowering
    int val;
    try { Thrower(depth, jb); val = 0; }
    catch (LongJmp e) when (e.Buf == jb) { val = e.Val; if (restoreOnCatch) SS.Restore(atSetjmp); }
    bool ok = SS.Save() == atSetjmp;
    SS.Restore(fsp);
    return ok ? val : -1;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int OneLocalloc(int n) { byte* p = stackalloc byte[n]; p[0] = 1; p[n - 1] = 2; return p[0] + p[n - 1]; }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int OneShadow(int n) { byte* s = SS.Save(); byte* p = SS.Alloc(n, 16); p[0] = 1; p[n - 1] = 2; int r = p[0] + p[n - 1]; SS.Restore(s); return r; }

  static int Main() {
    // Run on a thread with a 256 MiB stack, so that only the shadow stack limits the recursion test.
    int rc = 0; var t = new Thread(() => { rc = Body(); }, 256 << 20); t.Start(); t.Join(); return rc;
  }
  [MethodImpl(MethodImplOptions.NoInlining)]
  static int OneShadowHoisted(int n) {   // the header read once per function (TP-style), fast paths inline
    Hdr* h = SS.Tls(); byte* s = h->Sp; byte* p;
    if ((ulong)(h->Sp - h->Lo) < (ulong)(n + 15)) p = SS.Alloc(n, 16); else { p = (byte*)((long)(h->Sp - n) & -16L); h->Sp = p; }
    p[0] = 1; p[n - 1] = 2; int r = p[0] + p[n - 1];
    if (s >= h->Lo && s <= h->Hi && h->Cur != null) h->Sp = s; else SS.Restore(s);
    return r;
  }
  static int Body() {
    Console.WriteLine("runtime {0}", Environment.Version);
    long t0 = Environment.TickCount;
    long s1 = VlaLoop(1000000);
    Console.WriteLine("1. VLA loop: 1,000,000 iterations, sizes 1..4096: chunk allocations {0}, chunks live after {1} (the bottom chunk is kept), Sp back to empty: {2} ({3} ms)",
      SS.ChunkAllocs, SS.Chunks(), SS.Save() == (byte*)SS.Tls()->Hi, Environment.TickCount - t0);
    long a0 = SS.ChunkAllocs; t0 = Environment.TickCount;
    long s2 = Recurse(8000);
    long first = SS.ChunkAllocs - a0; int live = SS.Chunks(); bool empty = SS.Save() == SS.Tls()->Hi;
    long b0 = SS.ChunkAllocs; Recurse(8000); long again = SS.ChunkAllocs - b0;
    Console.WriteLine("2. recursion 8,000 x 1 KiB (8 MiB): chunk allocations {0}, live after {1}, Sp empty {2}; again: +{3} allocations ({4} ms)",
      first, live, empty, again, Environment.TickCount - t0);
    Console.WriteLine("3. longjmp through 50 VLA frames: with the catch-path restore -> {0}; without -> {1} (-1: Sp left below the setjmp point)",
      SetjmpCaller(50, true), SetjmpCaller(50, false));
    // 4. cost
    int sink = 0; double bl = 1e9, bs = 1e9, bh = 1e9;
    for (int r = 0; r < 7; r++) {
      var sw = Stopwatch.StartNew(); for (int i = 0; i < 3000000; i++) sink += OneLocalloc(64); bl = Math.Min(bl, sw.Elapsed.TotalMilliseconds * 1e6 / 3000000);
      sw = Stopwatch.StartNew(); for (int i = 0; i < 3000000; i++) sink += OneShadow(64); bs = Math.Min(bs, sw.Elapsed.TotalMilliseconds * 1e6 / 3000000);
      sw = Stopwatch.StartNew(); for (int i = 0; i < 3000000; i++) sink += OneShadowHoisted(64); bh = Math.Min(bh, sw.Elapsed.TotalMilliseconds * 1e6 / 3000000);
    }
    Console.WriteLine("4. per call: localloc(64) {0:F2} ns, shadow save+alloca+restore {1:F2} ns (3 header lookups), {2:F2} ns (1 lookup)", bl, bs, bh);
    return (int)((s1 + s2 + sink) & 0) ;
  }
}
