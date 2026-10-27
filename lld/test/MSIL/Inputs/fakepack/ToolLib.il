// ToolLib: a library built by this toolchain, as msil-ld's input
// classification sees it (ImplementationPlan.md 5.9.4, 5.11.1; 5.7.9's
// carrier): a .NET assembly whose Assembly row carries a uint8[] custom
// attribute of its own private class '<msil>SymbolTable'. Given through
// --reference it is skipped with a warning; from M22 a positional one is a
// library input (5.9.15), so the image is a valid library in the shape
// msil-ld -shared writes (M22.b item 2): the export class ToolLib with the
// function helper and its accessor '<msil>addr_helper', and '<msil>Data',
// whose '.cctor' (phase 1) sets '<msil>fn_helper'.
//
// The payload is a valid symbol table (5.9.15, SymbolTable.h): magic MSYM,
// version 1.0, no flags, ABI version 1, triple x86_64-pc-windows-msvc-msil,
// C runtime none, data model 8 4 2 8, the export class ToolLib, no data
// symbol, no flagged function. The blob is the custom-attribute prolog, the
// int32 array length 0x38, the 56 bytes and no named argument (M21's image
// had no export class, an empty C runtime name and the function 'helper'
// listed with flags 0).
// The alias 'FakeCore as mscorlib': see FakeInterop.il.
.assembly extern FakeCore as mscorlib
{
  .publickeytoken = ( B0 3F 5F 7F 11 D5 0A 3A )
  .ver 8:0:0:0
}
.assembly ToolLib
{
  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = (
    01 00 38 00 00 00 4D 53 59 4D 01 00 00 01 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73
    2D 6D 73 76 63 2D 6D 73 69 6C 04 6E 6F 6E 65 08 04 02 08 01 07 54 6F 6F 6C 4C 69 62 00 00 00 00 )
  .ver 0:0:0:0
}
.module ToolLib.dll

.class private auto ansi sealed beforefieldinit '<msil>SymbolTable' extends [mscorlib]System.Attribute
{
  .custom instance void [mscorlib]System.AttributeUsageAttribute::.ctor(valuetype [mscorlib]System.AttributeTargets)
    = ( 01 00 01 00 00 00 01 00 54 02 0D 41 6C 6C 6F 77 4D 75 6C 74 69 70 6C 65 01 )
  .method public hidebysig specialname rtspecialname instance void .ctor(uint8[] A_0) cil managed
  {
    .maxstack 1
    ldarg.0
    call instance void [mscorlib]System.Attribute::.ctor()
    ret
  }
}

.class private abstract sealed auto ansi beforefieldinit '<msil>Data' extends [mscorlib]System.Object
{
  .field assembly static initonly native int '<msil>fn_helper'
  .method private hidebysig specialname rtspecialname static void .cctor() cil managed
  {
    .maxstack 1
    ldftn int32 ToolLib::'helper'()
    stsfld native int '<msil>Data'::'<msil>fn_helper'
    ret
  }
}

.class public abstract sealed auto ansi ToolLib extends [mscorlib]System.Object
{
  .method public static int32 'helper'() cil managed
  {
    .maxstack 1
    ldc.i4.1
    ret
  }
  .method public hidebysig static native int '<msil>addr_helper'() cil managed
  {
    .maxstack 1
    ldsfld native int '<msil>Data'::'<msil>fn_helper'
    ret
  }
}
