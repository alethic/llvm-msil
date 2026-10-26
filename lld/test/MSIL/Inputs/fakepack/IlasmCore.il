// IlasmCore 8.0.0.0 (bcl-binding-files/fakepack/fakecore.il under another
// name): it defines System.Object, but without '.mscorlib' ilasm derives its
// classes from [mscorlib]System.Object and so gives it an AssemblyRef. By
// Roslyn's rule (ImplementationPlan.md 5.11.3) a core library has no
// AssemblyRef rows, so this is a candidate that does not qualify.
.assembly extern mscorlib
{
  .publickeytoken = ( B7 7A 5C 56 19 34 E0 89 )
  .ver 4:0:0:0
}
.assembly IlasmCore
{
  .publickey = ( 00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00 )
  .ver 8:0:0:0
}
.module IlasmCore.dll

.class public auto ansi beforefieldinit System.Object { }
.class public abstract auto ansi System.ValueType extends System.Object { }
.class public abstract auto ansi sealed System.Runtime.InteropServices.Marshal extends System.Object
{
  .method public hidebysig static native int AllocHGlobal(native int cb) cil managed { ldnull throw }
}
