// ToolLib: what a library built by this toolchain looks like to msil-ld's
// input classification (ImplementationPlan.md 5.9.4, 5.11.1; 5.7.9's
// carrier): a .NET assembly whose Assembly row carries a uint8[] custom
// attribute of its own private class '<msil>SymbolTable'. M21 does not link
// such libraries: given positionally it is
// '<path>: linking .NET libraries built by this toolchain is not supported yet',
// and given through --reference it is skipped with a warning.
//
// The payload is a valid symbol table (5.7.9, SymbolTable.h): magic MSYM,
// version 1.0, no flags, ABI version 1, triple x86_64-pc-windows-msvc-msil,
// no C runtime name, data model 8 4 2 8, no export class, no data symbol,
// one function 'helper' without flags. The blob is the custom-attribute
// prolog, the int32 array length 0x34, the 52 bytes and no named argument.
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly ToolLib
{
  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = (
    01 00 34 00 00 00 4D 53 59 4D 01 00 00 01 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73
    2D 6D 73 76 63 2D 6D 73 69 6C 00 08 04 02 08 00 00 01 06 68 65 6C 70 65 72 00 00 00 )
  .ver 0:0:0:0
}
.module ToolLib.dll

.class private auto ansi sealed beforefieldinit '<msil>SymbolTable' extends [mscorlib]System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor(uint8[] table) cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Attribute::.ctor()
    ret
  }
}

.method public static int32 'helper'() cil managed
{
  .maxstack 1
  ldc.i4.1
  ret
}
