// proto_tls.il - hand-written prototype of the proposed thread_local lowering (option B):
// one per-thread block per output assembly = a 32-byte header (shadow-stack state, reserved) plus a copy
// of the TLS template (.tdata image, then .tbss zeros); a [ThreadStatic] native int on a private
// beforefieldinit class points to it; '<msil>tls'() allocates it on first use; a finalizable holder
// object of the same class, kept in a second [ThreadStatic] field, frees it after the thread ends.
// C source it stands for:
//   int g = 42;
//   _Thread_local int tcount = 5;          // .tdata, block offset 32
//   _Thread_local int *tptr = &g;          // .tdata with a relocation, block offset 40
//   _Thread_local char tzero[64];          // .tbss, block offset 48
//   int bump(void) { return ++tcount; }
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly proto_tls { .ver 0:0:0:0 }
.module proto_tls.exe

.class private explicit ansi sealed '<msil>Blob16' extends [mscorlib]System.ValueType { .pack 1 .size 16 }
.data D_TDATA = bytearray (05 00 00 00 00 00 00 00   00 00 00 00 00 00 00 00)
.field static assembly valuetype '<msil>Blob16' '<msil>tdata' at D_TDATA

// Address fields of globals (D12 shape) and offset fields of TLS symbols (set by the module initializer).
.field public static initonly native int 'g'
.field public static initonly native int 'tcount'
.field public static initonly native int 'tptr'
.field public static initonly native int 'tzero'
.field assembly static initonly native int '<msil>tls_template'
.field assembly static native int 'res'
.field assembly static int32 'slot'

.class private auto ansi sealed beforefieldinit '<msil>Thread' extends [mscorlib]System.Object {
  .field public static native int 'cur'
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
  .field public static class '<msil>Thread' 'own'
  .custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = ( 01 00 00 00 )
  .field public static int32 'freed'
  .field assembly native int 'blk'
  .method public hidebysig specialname rtspecialname instance void .ctor(native int b) cil managed {
    ldarg.0
    call instance void [mscorlib]System.Object::.ctor()
    ldarg.0
    ldarg.1
    stfld native int '<msil>Thread'::'blk'
    ret
  }
  // Runs on the finalizer thread after the owning thread is gone. At process exit the .NET Framework
  // also finalizes reachable objects (HasShutdownStarted): never free a block that may still be in use.
  .method family hidebysig virtual instance void Finalize() cil managed {
    call bool [mscorlib]System.Environment::get_HasShutdownStarted()
    brtrue.s L_ret
    ldarg.0
    ldfld native int '<msil>Thread'::'blk'
    call void [mscorlib]System.Runtime.InteropServices.Marshal::FreeHGlobal(native int)
    ldsflda int32 '<msil>Thread'::'freed'
    call int32 [mscorlib]System.Threading.Interlocked::Increment(int32&)
    pop
  L_ret:
    ret
  }
  .method assembly static native int Init() cil managed noinlining {
    .maxstack 3
    .locals (native int b)
    ldc.i4 112
    conv.i
    call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    stloc.0
    ldloc.0
    ldc.i4.0
    ldc.i4.s 32
    initblk
    ldloc.0
    ldc.i4.s 32
    add
    ldsfld native int '<msil>tls_template'
    ldc.i4.s 16
    cpblk
    ldloc.0
    ldc.i4.s 48
    add
    ldc.i4.0
    ldc.i4.s 64
    initblk
    ldloc.0
    stsfld native int '<msil>Thread'::'cur'
    ldloc.0
    newobj instance void '<msil>Thread'::.ctor(native int)
    stsfld class '<msil>Thread' '<msil>Thread'::'own'
    ldloc.0
    ret
  }
}

// The base of the current thread's block (aggressiveinlining: the JIT may inline the fast path).
.method assembly static native int '<msil>tls'() cil managed aggressiveinlining {
  .maxstack 2
  ldsfld native int '<msil>Thread'::'cur'
  dup
  brtrue.s L1
  pop
  call native int '<msil>Thread'::Init()
L1:
  ret
}

.method private static void .cctor() cil managed {
  .maxstack 4
  ldc.i4.8
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int 'g'
  ldsfld native int 'g'
  ldc.i4.s 42
  stind.i4
  // the TLS template: copy the RVA image once, then apply its relocations once (tptr = &g)
  ldc.i4.s 16
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int '<msil>tls_template'
  ldsfld native int '<msil>tls_template'
  ldsflda valuetype '<msil>Blob16' '<msil>tdata'
  ldc.i4.s 16
  cpblk
  ldsfld native int '<msil>tls_template'
  ldc.i4.8
  add
  ldsfld native int 'g'
  stind.i
  // TLS symbol offsets (the linker's layout of the template, after the 32-byte header)
  ldc.i4.s 32
  conv.i
  stsfld native int 'tcount'
  ldc.i4.s 40
  conv.i
  stsfld native int 'tptr'
  ldc.i4.s 48
  conv.i
  stsfld native int 'tzero'
  ldc.i4 256
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int 'res'
  ret
}

// int bump(void) { return ++tcount; }
.method public static int32 'bump'() cil managed {
  .maxstack 3
  .locals (native int p, int32 v)
  call native int '<msil>tls'()
  ldsfld native int 'tcount'
  add
  stloc.0
  ldloc.0
  ldind.i4
  ldc.i4.1
  add
  stloc.1
  ldloc.0
  ldloc.1
  stind.i4
  ldloc.1
  ret
}

// Records &tcount, tcount, *tptr and the sum of tzero[] into res[slot*4 ..].
.method public static void 'record'(int32 slot) cil managed {
  .maxstack 4
  .locals (native int r, native int t, int32 i, int32 s)
  ldsfld native int 'res'
  ldarg.0
  ldc.i4.s 32
  mul
  add
  stloc.0
  call native int '<msil>tls'()
  stloc.1
  ldloc.0
  ldloc.1
  ldsfld native int 'tcount'
  add
  stind.i
  ldloc.0
  ldc.i4.8
  add
  ldloc.1
  ldsfld native int 'tcount'
  add
  ldind.i4
  conv.i
  stind.i
  ldloc.0
  ldc.i4.s 16
  add
  ldloc.1
  ldsfld native int 'tptr'
  add
  ldind.i
  ldind.i4
  conv.i
  stind.i
  ldc.i4.0
  stloc.2
  ldc.i4.0
  stloc.3
L_loop:
  ldloc.3
  ldloc.1
  ldsfld native int 'tzero'
  add
  ldloc.2
  add
  ldind.u1
  add
  stloc.3
  ldloc.1
  ldsfld native int 'tzero'
  add
  ldloc.2
  add
  ldc.i4.s 99
  stind.i1
  ldloc.2
  ldc.i4.1
  add
  dup
  stloc.2
  ldc.i4.s 64
  blt.s L_loop
  ldloc.0
  ldc.i4.s 24
  add
  ldloc.3
  conv.i
  stind.i
  ret
}

.method public static void 'worker'() cil managed {
  .maxstack 2
  .locals (int32 i, int32 s)
  ldsflda int32 'slot'
  call int32 [mscorlib]System.Threading.Interlocked::Increment(int32&)
  stloc.1
  ldc.i4.0
  stloc.0
L0:
  call int32 'bump'()
  pop
  ldloc.0
  ldc.i4.1
  add
  dup
  stloc.0
  ldc.i4 1000
  blt.s L0
  ldloc.1
  call void 'record'(int32)
  ret
}

.method public static int32 'Main'() cil managed {
  .entrypoint
  .maxstack 6
  .locals (class [mscorlib]System.Threading.Thread[] ts, int32 i, int32 bad, native int r)
  call int32 'bump'()
  pop
  ldc.i4.0
  call void 'record'(int32)
  ldc.i4.4
  newarr [mscorlib]System.Threading.Thread
  stloc.0
  ldc.i4.0
  stloc.1
L_start:
  ldloc.0
  ldloc.1
  ldnull
  ldftn void 'worker'()
  newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
  newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart)
  stelem.ref
  ldloc.0
  ldloc.1
  ldelem.ref
  callvirt instance void [mscorlib]System.Threading.Thread::Start()
  ldloc.1
  ldc.i4.1
  add
  dup
  stloc.1
  ldc.i4.4
  blt.s L_start
  ldc.i4.0
  stloc.1
L_join:
  ldloc.0
  ldloc.1
  ldelem.ref
  callvirt instance void [mscorlib]System.Threading.Thread::Join()
  ldloc.1
  ldc.i4.1
  add
  dup
  stloc.1
  ldc.i4.4
  blt.s L_join
  ldc.i4.0
  stloc.1
L_print:
  ldsfld native int 'res'
  ldloc.1
  ldc.i4.s 32
  mul
  add
  stloc.3
  ldstr "thread slot {0}: &tcount=0x{1:x} tcount={2} *tptr={3} sum(tzero)={4}"
  ldc.i4.5
  newarr [mscorlib]System.Object
  dup
  ldc.i4.0
  ldloc.1
  box [mscorlib]System.Int32
  stelem.ref
  dup
  ldc.i4.1
  ldloc.3
  ldind.i8
  box [mscorlib]System.Int64
  stelem.ref
  dup
  ldc.i4.2
  ldloc.3
  ldc.i4.8
  add
  ldind.i8
  box [mscorlib]System.Int64
  stelem.ref
  dup
  ldc.i4.3
  ldloc.3
  ldc.i4.s 16
  add
  ldind.i8
  box [mscorlib]System.Int64
  stelem.ref
  dup
  ldc.i4.4
  ldloc.3
  ldc.i4.s 24
  add
  ldind.i8
  box [mscorlib]System.Int64
  stelem.ref
  call void [mscorlib]System.Console::WriteLine(string, object[])
  ldloc.1
  ldc.i4.1
  add
  dup
  stloc.1
  ldc.i4.5
  blt L_print
  call void [mscorlib]System.GC::Collect()
  call void [mscorlib]System.GC::WaitForPendingFinalizers()
  call void [mscorlib]System.GC::Collect()
  call void [mscorlib]System.GC::WaitForPendingFinalizers()
  ldstr "blocks freed by finalizers: {0} (main thread's block still live: tcount={1})"
  ldsfld int32 '<msil>Thread'::'freed'
  box [mscorlib]System.Int32
  call int32 'bump'()
  box [mscorlib]System.Int32
  call void [mscorlib]System.Console::WriteLine(string, object, object)
  ldc.i4.0
  ret
}
