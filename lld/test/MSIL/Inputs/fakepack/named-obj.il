// named-obj.o: a CIL object (ImplementationPlan.md 5.8.2-5.8.4) in
// object-form text, with BCL references llc never writes. Its one function,
// 'void named()', calls, in this order:
//   - Fake.Outer/Inner::Ping() through a nested TypeRef,
//     [msil.bcl]Fake.Outer/Inner (Inner scoped to the TypeRef of Fake.Outer);
//   - System.Threading.Interlocked::MemoryBarrier() through a TypeRef scoped
//     to an AssemblyRef other than the placeholders, [Contoso], which msil-ld
//     binds by name like a [msil.bcl] one (M21.b item 6).
// The record is the one llc -msil-object-form writes for
// 'define void @named()' on x86_64-pc-windows-msvc-msil (one FUNCTION symbol,
// MethodDef 0x06000001); BCL references have no symbol. gen.py assembles it
// with '-dll -pe64 -x64', as %ilasm-verify does.
//
// The variant VARARG (named-obj-vararg.o, the same record) calls instead
//   - vararg void [msil.bcl]System.Threading.Interlocked::MemoryBarrier(),
//     a vararg-convention request with no argument after the sentinel.
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly extern 'msil.bcl' { .ver 0:0:0:0 }
.assembly extern 'msil.link' { .ver 0:0:0:0 }
#ifndef VARARG
.assembly extern Contoso { .ver 1:0:0:0 }
#endif

.method public static void 'named'() cil managed
{
  .maxstack 0
#ifdef VARARG
  call vararg void [msil.bcl]System.Threading.Interlocked::MemoryBarrier()
#else
  call void [msil.bcl]Fake.Outer/Inner::Ping()
  call void [Contoso]System.Threading.Interlocked::MemoryBarrier()
#endif
  ret
} // end of method 'named'

.module 'named.ll'
.custom instance void [msil.link]'<msil>LinkInfo'::.ctor(uint8[]) = ( 01 00 39 00 00 00 4D 53 4C 4B 01 00 00 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73 2D 6D 73 76 63 2D 6D 73 69 6C 01 00 00 08 04 02 08 01 0D 01 00 00 05 6E 61 6D 65 64 01 00 00 06 00 00 )
