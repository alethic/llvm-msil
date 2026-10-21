// A minimal core library: defines System.Object and friends, references nothing.
.assembly FakeCore { .publickey = (00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00) .ver 8:0:0:0 }
.class public auto ansi beforefieldinit System.Object { }
.class public abstract auto ansi System.ValueType extends System.Object { }
.class public sealed System.Void extends System.ValueType { }
.class public sealed System.IntPtr extends System.ValueType { }
.class public sealed System.Int32 extends System.ValueType { }
.class public auto ansi System.String extends System.Object { }
.class public abstract sealed auto ansi System.Runtime.InteropServices.Marshal extends System.Object
{
  .method public hidebysig static native int AllocHGlobal(native int cb) cil managed { ldnull throw }
}
