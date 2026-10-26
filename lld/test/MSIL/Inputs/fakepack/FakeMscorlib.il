// mscorlib 4.0.0.0 with the ECMA key (token b77a5c561934e089): a core
// library in one file, like the .NET Framework v4.8 reference directory's
// mscorlib, holding every Phase 1 and M20 entry of BCLMembers.def. A link
// bound to it names the assembly msil-ld's built-in table names (decision
// D10, ImplementationPlan.md 5.11.7), so its listing equals the listing of
// the same link without references; next to FakeCore it is a second core
// library (5.11.3).
.mscorlib
.assembly mscorlib
{
  .custom instance void System.Runtime.CompilerServices.ReferenceAssemblyAttribute::.ctor() = ( 01 00 00 00 )
  .publickey = ( 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 )
  .ver 4:0:0:0
}
.module mscorlib.dll

.class public auto ansi serializable beforefieldinit System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public abstract auto ansi serializable beforefieldinit System.ValueType extends System.Object { }
.class public sequential ansi sealed serializable beforefieldinit System.Void extends System.ValueType { }
.class public sequential ansi sealed serializable beforefieldinit System.Int32 extends System.ValueType { }
.class public sequential ansi sealed serializable beforefieldinit System.IntPtr extends System.ValueType { }
.class public auto ansi sealed serializable beforefieldinit System.String extends System.Object { }
.class public abstract auto ansi serializable beforefieldinit System.Attribute extends System.Object
{
  .method family hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
.class public auto ansi serializable beforefieldinit System.Exception extends System.Object
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
}
.class public auto ansi serializable beforefieldinit System.EntryPointNotFoundException extends System.Exception
{
  .method public hidebysig specialname rtspecialname instance void .ctor(string message) cil managed { ldnull throw }
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
.class public auto ansi sealed beforefieldinit System.Runtime.CompilerServices.ReferenceAssemblyAttribute
       extends System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor() cil managed { ldnull throw }
}
