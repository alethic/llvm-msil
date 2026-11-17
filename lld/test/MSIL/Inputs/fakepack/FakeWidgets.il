// FakeWidgets 1.0.0.0: the members that lld/test/MSIL/managed-calls*.test
// call through managed-call stubs (ImplementationPlan.md 5.20, MC-1.g),
// over the core library FakeRuntime ('FakeRuntime as mscorlib', see
// FakeInterop.il). One member per form of 5.20.2 MA1-MA3:
//   Contoso.Widget   a class: static methods over every listing primitive,
//                    float32/float64, pointers, string, object, class,
//                    arrays, value types and GCHandle; a constructor; an
//                    instance method, a virtual property getter; a static
//                    and an instance field; Measure() returns int64, so a
//                    request for int32 Measure() lists it as an overload;
//                    enum results (GetMode(), GetWide(), GetSmall(),
//                    GetShort()) and a static field of an enum type
//                    (Default), for the result check of 5.20.7 MK2; a
//                    literal field (Max), for MK7; two parameters of one
//                    struct (Pair(Point, Point)) and of one enum
//                    (SetPair(Mode, Mode)), for MK2's check of each
//                    parameter (a boxed handle beside a C integer); a
//                    static void() method (Reset()), for the exported and
//                    startup managed calls; a uint8 parameter and result
//                    (Octet(uint8)), for a C _Bool (i1)
//   Contoso.Point    a value type (struct): a constructor, an instance
//                    method, an instance field, a static method
//                    (Twice(int32)) and a static field (Total), for MK3's
//                    static members of a value type; it inherits
//                    GetHashCode() from System.ValueType and GetType() from
//                    System.Object (FakeRuntime.il)
//   Contoso.Mode     an enum over int32
//   Contoso.WideMode an enum over int64
//   Contoso.SmallMode an enum over uint8
//   Contoso.ShortMode an enum over int16
//   Contoso.Outer/Inner a nested public class
//   Contoso.WidgetException, Contoso.BrokenWidgetException
//                    exceptions (directly from System.Exception, and through
//                    WidgetException)
//   Contoso.NotAnException a class that is not an exception
//   Contoso.IWidget  an interface with an abstract instance method (Score()),
//                    a reference type without a base (the Interface branch
//                    of the type-kind query, 5.20.7 MK3)
//   Contoso.Color, Contoso.Extent
//                    an enum over int32 and a struct that declare no
//                    method, so each static method called through them is
//                    one they inherit: System.Enum::GetNames(class
//                    System.Type) and System.Object::ReferenceEquals(object,
//                    object) (FakeRuntime.il; managed-calls-inherited.test)
// Bodies throw, as in a reference assembly.
.assembly extern FakeRuntime as mscorlib
{
  .ver 8:0:0:0
}
.assembly FakeWidgets
{
  .custom instance void [mscorlib]System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .ver 1:0:0:0
}
.module FakeWidgets.dll

.class public auto ansi beforefieldinit Contoso.Widget extends [mscorlib]System.Object
{
  .field public static int32 Count
  .field public int64 Serial
  .field public static valuetype Contoso.WideMode Default
  .field public static literal int32 Max = int32(7)
  .method public hidebysig specialname rtspecialname instance void .ctor(string name, int32 size) cil managed { ldnull throw }
  .method public hidebysig static void Prims(bool a, char b, int8 c, uint8 d, int16 e, uint16 f, int32 g, uint32 h,
                                             int64 i, uint64 j, float32 k, float64 l, native int m,
                                             native uint n) cil managed { ldnull throw }
  .method public hidebysig static float64 Mix(float32 a, float64 b) cil managed { ldnull throw }
  .method public hidebysig static void* Ptrs(uint8* a, void* b, int32* c) cil managed { ldnull throw }
  .method public hidebysig static string Describe(object a, string b, class Contoso.Widget c, int32[] d,
                                                  string[] e) cil managed { ldnull throw }
  .method public hidebysig static int32 Length(string s) cil managed { ldnull throw }
  .method public hidebysig static bool Check(bool b) cil managed { ldnull throw }
  .method public hidebysig static char Letter(char c) cil managed { ldnull throw }
  .method public hidebysig static object Wrap(valuetype Contoso.Point p) cil managed { ldnull throw }
  .method public hidebysig static valuetype Contoso.Point Origin() cil managed { ldnull throw }
  .method public hidebysig static void SetMode(valuetype Contoso.Mode m) cil managed { ldnull throw }
  .method public hidebysig static void SetWide(valuetype Contoso.WideMode m) cil managed { ldnull throw }
  .method public hidebysig static void SetSmall(valuetype Contoso.SmallMode m) cil managed { ldnull throw }
  .method public hidebysig static void SetModes(int32 a, valuetype Contoso.Mode b,
                                                valuetype Contoso.WideMode c) cil managed { ldnull throw }
  .method public hidebysig static void Place(valuetype Contoso.Point p) cil managed { ldnull throw }
  .method public hidebysig instance void Tune(valuetype Contoso.SmallMode m) cil managed { ldnull throw }
  .method public hidebysig static void Pin(valuetype [mscorlib]System.Runtime.InteropServices.GCHandle h) cil managed { ldnull throw }
  .method public hidebysig static valuetype [mscorlib]System.Runtime.InteropServices.GCHandle Keep(object o) cil managed { ldnull throw }
  .method public hidebysig specialname newslot virtual instance int32 get_Size() cil managed { ldnull throw }
  .method public hidebysig instance void Resize(int32 size) cil managed { ldnull throw }
  .method public hidebysig static int64 Measure() cil managed { ldnull throw }
  .method public hidebysig static int32 Guarded(int32 x) cil managed { ldnull throw }
  .method public hidebysig static valuetype Contoso.Mode GetMode() cil managed { ldnull throw }
  .method public hidebysig static valuetype Contoso.WideMode GetWide() cil managed { ldnull throw }
  .method public hidebysig static valuetype Contoso.SmallMode GetSmall() cil managed { ldnull throw }
  .method public hidebysig static valuetype Contoso.ShortMode GetShort() cil managed { ldnull throw }
  .method public hidebysig static int32 Pair(valuetype Contoso.Point a, valuetype Contoso.Point b) cil managed { ldnull throw }
  .method public hidebysig static void SetPair(valuetype Contoso.Mode a, valuetype Contoso.Mode b) cil managed { ldnull throw }
  .method public hidebysig static void Reset() cil managed { ldnull throw }
  .method public hidebysig static uint8 Octet(uint8 b) cil managed { ldnull throw }
  .property instance int32 Size()
  {
    .get instance int32 Contoso.Widget::get_Size()
  }
}
.class public sequential ansi sealed beforefieldinit Contoso.Point extends [mscorlib]System.ValueType
{
  .field public int32 X
  .field public int32 Y
  .field public static int32 Total
  .method public hidebysig specialname rtspecialname instance void .ctor(int32 x, int32 y) cil managed { ldnull throw }
  .method public hidebysig instance int32 Sum() cil managed { ldnull throw }
  .method public hidebysig static int32 Twice(int32 x) cil managed { ldnull throw }
}
.class public auto ansi sealed Contoso.Mode extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname int32 value__
  .field public static literal valuetype Contoso.Mode Off = int32(0)
  .field public static literal valuetype Contoso.Mode On = int32(1)
}
.class public auto ansi sealed Contoso.WideMode extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname int64 value__
  .field public static literal valuetype Contoso.WideMode Far = int64(0x100000000)
}
.class public auto ansi sealed Contoso.SmallMode extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname uint8 value__
  .field public static literal valuetype Contoso.SmallMode Tiny = uint8(1)
}
.class public auto ansi sealed Contoso.ShortMode extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname int16 value__
  .field public static literal valuetype Contoso.ShortMode Low = int16(-1)
}
.class public auto ansi beforefieldinit Contoso.Outer extends [mscorlib]System.Object
{
  .class nested public auto ansi beforefieldinit Inner extends [mscorlib]System.Object
  {
    .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
    .method public hidebysig static int32 Depth(class Contoso.Outer/Inner i) cil managed { ldnull throw }
  }
}
.class public auto ansi beforefieldinit Contoso.WidgetException extends [mscorlib]System.Exception
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit Contoso.BrokenWidgetException extends Contoso.WidgetException
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit Contoso.NotAnException extends [mscorlib]System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class interface public abstract auto ansi Contoso.IWidget
{
  .method public hidebysig newslot abstract virtual instance int32 Score() cil managed { }
}
.class public auto ansi sealed Contoso.Color extends [mscorlib]System.Enum
{
  .field public specialname rtspecialname int32 value__
  .field public static literal valuetype Contoso.Color Red = int32(0)
}
.class public sequential ansi sealed beforefieldinit Contoso.Extent extends [mscorlib]System.ValueType
{
  .field public int32 Width
}
