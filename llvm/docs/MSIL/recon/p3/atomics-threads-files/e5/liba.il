.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly liba { }
.module liba.dll
.class public auto ansi abstract sealed La extends [mscorlib]System.Object {
  .field static native int table
  // '<msil>atomic_lock_table'(): one process-wide lock table, found by name
  .method public static native int Table() cil managed {
    .locals init (object key, bool taken, object o)
    ldsfld native int La::table brfalse.s SLOW
    ldsfld native int La::table ret
  SLOW:
    ldstr "msil.atomic.locks.v1" call string [mscorlib]System.String::Intern(string) stloc.0
    .try {
      ldloc.0 ldloca.s 1 call void [mscorlib]System.Threading.Monitor::Enter(object, bool&)
      call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
      ldstr "msil.atomic.locks.v1" callvirt instance object [mscorlib]System.AppDomain::GetData(string) stloc.2
      ldloc.2 brtrue.s HAVE
      ldc.i4 8192 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
      dup ldc.i4.0 ldc.i4 8192 initblk
      box [mscorlib]System.IntPtr stloc.2
      call class [mscorlib]System.AppDomain [mscorlib]System.AppDomain::get_CurrentDomain()
      ldstr "msil.atomic.locks.v1" ldloc.2 callvirt instance void [mscorlib]System.AppDomain::SetData(string, object)
    HAVE:
      ldloc.2 unbox.any [mscorlib]System.IntPtr volatile. stsfld native int La::table
      leave.s DONE
    } finally {
      ldloc.1 brfalse.s NOEXIT ldloc.0 call void [mscorlib]System.Threading.Monitor::Exit(object)
    NOEXIT: endfinally
    }
  DONE: ldsfld native int La::table ret
  }
}
