// FakeRuntime 8.0.0.0: the core library of the managed-call tests
// (lld/test/MSIL/managed-calls*.test; ImplementationPlan.md 5.20, MC-1.g).
// It defines top-level System.Object and has no AssemblyRef row ('.mscorlib',
// see FakeCore.il), and it holds everything a managed-call stub or an image
// asks the BCL for when a link has references (5.20.5 ML2, 5.20.10, 5.20.13):
//   - the Phase 1 and M20 members of BCLMembers.def (System.ValueType,
//     Environment::GetCommandLineArgs, Buffer::MemoryCopy, Marshal's
//     AllocHGlobal, StringToHGlobalAnsi and StringToHGlobalUni,
//     Interlocked::MemoryBarrier, EntryPointNotFoundException and its
//     .ctor(string), System.Object), and P-1a's guard rows for
//     msil64-unknown-dotnet images (PlatformNotSupportedException::
//     .ctor(string), BitConverter::GetBytes(int32), System.Byte);
//   - MC-1's stub-internal members: GCHandle with Alloc(object),
//     ToIntPtr, FromIntPtr, get_Target() and Free(), Encoding::GetString(
//     uint8*, int32), UTF8Encoding::.ctor(bool, bool), UTF32Encoding::
//     .ctor(bool, bool, bool), String::.ctor(char*), and
//     DecoderFallbackException, which derives from System.Exception through
//     ArgumentException and SystemException, as in the BCL;
//   - M27's System.Exception::get_HResult() and ExternalException.
//   - M22's AttributeUsageAttribute (.ctor(AttributeTargets) and the
//     AllowMultiple property) and the enum AttributeTargets, which msil-ld
//     -shared asks for the '<msil>SymbolTable' attribute (5.9.15).
//   - the members a value type inherits, for the access forms of 5.20.3
//     MS5 as amended in section 12 (2026-11-04): System.Object's virtual
//     GetHashCode() and non-virtual GetType() (returning System.Type, a
//     class), and System.ValueType's override of GetHashCode(), so that
//     the binder's base-chain search finds GetHashCode on System.ValueType
//     and GetType on System.Object for a struct that declares neither.
//   - the static methods a value type inherits: System.Object's
//     ReferenceEquals(object, object) and System.Enum's
//     GetNames(class System.Type), which a struct or an enum that declares
//     neither reaches only through its base chain
//     (managed-calls-inherited.test).
// GCHandle is a value type and Encoding an abstract class, as in the BCL.
// Bodies throw, as in a reference assembly.
.mscorlib
.assembly FakeRuntime
{
  .custom instance void System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .ver 8:0:0:0
}
.module FakeRuntime.dll

.class public auto ansi beforefieldinit System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
  .method public hidebysig newslot virtual instance int32 GetHashCode() cil managed { ldnull throw }
  .method public hidebysig instance class System.Type GetType() cil managed { ldnull throw }
  .method public hidebysig static bool ReferenceEquals(object objA, object objB) cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.ValueType extends System.Object
{
  .method public hidebysig virtual instance int32 GetHashCode() cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.Type extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Enum extends System.ValueType
{
  .method public hidebysig static string[] GetNames(class System.Type enumType) cil managed { ldnull throw }
}
.class public sequential ansi sealed beforefieldinit System.Void extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Boolean extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Char extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.SByte extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Byte extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int16 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt16 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int32 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt32 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Int64 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UInt64 extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Single extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.Double extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.IntPtr extends System.ValueType { }
.class public sequential ansi sealed beforefieldinit System.UIntPtr extends System.ValueType { }
.class public auto ansi sealed beforefieldinit System.String extends System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor(char* 'value') cil managed { ldnull throw }
}
.class public abstract auto ansi beforefieldinit System.Array extends System.Object { }
.class public abstract auto ansi beforefieldinit System.Attribute extends System.Object
{
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
// The attribute class '<msil>SymbolTable' of a library that msil-ld -shared
// writes extends System.Attribute and carries
// AttributeUsage(AttributeTargets.Assembly) { AllowMultiple = true }
// (ImplementationPlan.md 5.9.15, M22.a item 6), as in FakeCore.il and the
// .NET 8 pack: managed-calls.test links a library against this pack.
.class public auto ansi sealed beforefieldinit System.AttributeUsageAttribute extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor(valuetype System.AttributeTargets validOn)
          cil managed { ldnull throw }
  .method public hidebysig specialname instance bool get_AllowMultiple() cil managed { ldnull throw }
  .method public hidebysig specialname instance void set_AllowMultiple(bool 'value') cil managed { ldnull throw }
  .property instance bool AllowMultiple()
  {
    .get instance bool System.AttributeUsageAttribute::get_AllowMultiple()
    .set instance void System.AttributeUsageAttribute::set_AllowMultiple(bool)
  }
}
.class public auto ansi sealed System.AttributeTargets extends System.Enum
{
  .field public specialname rtspecialname int32 value__
  .field public static literal valuetype System.AttributeTargets Assembly = int32(1)
}
.class public auto ansi beforefieldinit System.Exception extends System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
  .method public hidebysig specialname instance int32 get_HResult() cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.SystemException extends System.Exception
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.ArgumentException extends System.SystemException
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.Text.DecoderFallbackException extends System.ArgumentException
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.EntryPointNotFoundException extends System.Exception
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.PlatformNotSupportedException extends System.SystemException
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi beforefieldinit System.Runtime.InteropServices.ExternalException extends System.SystemException
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message, int32 errorCode) cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.Environment extends System.Object
{
  .method public hidebysig static string[] GetCommandLineArgs() cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.Buffer extends System.Object
{
  .method public hidebysig static void MemoryCopy(void* source, void* destination, int64 destinationSizeInBytes,
                                                  int64 sourceBytesToCopy) cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.BitConverter extends System.Object
{
  .method public hidebysig static uint8[] GetBytes(int32 'value') cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.Runtime.InteropServices.Marshal extends System.Object
{
  .method public hidebysig static native int AllocHGlobal(native int cb) cil managed { ldnull throw }
  .method public hidebysig static native int StringToHGlobalAnsi(string s) cil managed { ldnull throw }
  .method public hidebysig static native int StringToHGlobalUni(string s) cil managed { ldnull throw }
}
.class public abstract auto ansi sealed beforefieldinit System.Threading.Interlocked extends System.Object
{
  .method public hidebysig static void MemoryBarrier() cil managed { ldnull throw }
}
.class public sequential ansi sealed beforefieldinit System.Runtime.InteropServices.GCHandle extends System.ValueType
{
  .field private native int _handle
  .method public hidebysig static valuetype System.Runtime.InteropServices.GCHandle Alloc(object 'value') cil managed { ldnull throw }
  .method public hidebysig static native int ToIntPtr(valuetype System.Runtime.InteropServices.GCHandle 'value') cil managed { ldnull throw }
  .method public hidebysig static valuetype System.Runtime.InteropServices.GCHandle FromIntPtr(native int 'value') cil managed { ldnull throw }
  .method public hidebysig specialname instance object get_Target() cil managed { ldnull throw }
  .method public hidebysig instance void Free() cil managed { ldnull throw }
}
.class public abstract auto ansi serializable beforefieldinit System.Text.Encoding extends System.Object
{
  .method public hidebysig newslot virtual instance string GetString(uint8* bytes, int32 byteCount) cil managed { ldnull throw }
}
.class public auto ansi serializable beforefieldinit System.Text.UTF8Encoding extends System.Text.Encoding
{
  .method public hidebysig specialname rtspecialname instance void .ctor(bool encoderShouldEmitUTF8Identifier,
                                                                       bool throwOnInvalidBytes) cil managed { ldnull throw }
}
.class public auto ansi sealed serializable beforefieldinit System.Text.UTF32Encoding extends System.Text.Encoding
{
  .method public hidebysig specialname rtspecialname instance void .ctor(bool bigEndian, bool byteOrderMark,
                                                                       bool throwOnInvalidCharacters) cil managed { ldnull throw }
}
.class public auto ansi sealed beforefieldinit System.Runtime.CompilerServices.ReferenceAssemblyAttribute
       extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
