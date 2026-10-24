// The first input of canon-listing.test: constructs that canonical listing
// v1 prints (plan 5.7.10), among them one of each that the five 5.12.6
// normalizations of an ilasm image act on; the test's second input,
// coverage.il, has the rest. The test assembles it with %ilasm -dll; the
// Framework ilasm and the NuGet ilasm give the same listing.
//
// - the Assembly row's HashAlgId 0x8004 (--assembly-hash=ignore);
// - a field with RVA data (.data);
// - a P/Invoke stub, with the Param row 'A_0' that ilasm adds
//   (--pinvoke-module=strip, --pinvoke-access=ignore, --params=drop);
// - 'pick': no .locals, so ilasm sets InitLocals anyway
//   (--initlocals=ignore-without-locals); a named parameter and an unnamed
//   [opt] one ('A_1', Flags 0x0010, which --params=drop keeps); a switch, a
//   short branch and ldc.i4.s (--forms=canon); a non-ASCII ldstr, written as
//   a byte array so that no ilasm reads the source in its own code page;
//   a call; a custom attribute;
// - 'guarded': .locals and a try/catch with leave.s;
// - a custom attribute of the assembly (a trailer line).
//
// Declaration order (puts, pick, guarded) is not the sorted order, so the
// --order=decl listing differs from the default one.

.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly canon
{
  .hash algorithm 0x00008004
  .ver 1:2:3:4
  .custom instance void [mscorlib]System.Reflection.AssemblyTitleAttribute::.ctor(string) = ( 01 00 03 61 62 63 00 00 )
}
.module canon.dll

.data cil D_1 = bytearray (01 02 03 04 05 06 07 08)
.field public static int64 'rva' at D_1

.method public static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}

.method public static int32 'pick'(int32 'sel', [opt] int32) cil managed
{
  .custom instance void [mscorlib]System.ObsoleteAttribute::.ctor(string) = ( 01 00 03 6F 6C 64 00 00 )
  .maxstack 2
  ldarg.0
  switch (L_one, L_two)
  ldarg.1
  ret
L_one:
  ldc.i4.s 100
  br.s L_end
L_two:
  ldstr bytearray (E9 00 74 00 E9 00)  // "\u00E9t\u00E9"
  pop
  call int32 'guarded'()
L_end:
  ret
}

.method public static int32 'guarded'() cil managed
{
  .maxstack 1
  .locals init (int32 V_0)
  .try
  {
    ldsfld int64 'rva'
    conv.i4
    stloc.0
    leave.s L_done
  }
  catch [mscorlib]System.Exception
  {
    pop
    ldc.i4.m1
    stloc.0
    leave.s L_done
  }
L_done:
  ldloc.0
  ret
}
