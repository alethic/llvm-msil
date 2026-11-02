// MC-1 prototype: the stub shapes that MSILManagedCalls.cpp would write.
// Only C-side types cross a stub boundary: native int (pointers, handles), int32, int64.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly mc1proto { .ver 0:0:0:0 }
.module mc1proto.exe

// UTF-8 const char* -> string (null stays null).
.method assembly static string '<mc>utf8'(native int p) cil managed aggressiveinlining
{
  .maxstack 3
  .locals init (int32 n)
  ldarg.0
  brfalse.s NUL
LOOP:
  ldarg.0
  ldloc.0
  add
  ldind.u1
  brfalse.s DONE
  ldloc.0
  ldc.i4.1
  add
  stloc.0
  br.s LOOP
DONE:
  call class [mscorlib]System.Text.Encoding [mscorlib]System.Text.Encoding::get_UTF8()
  ldarg.0
  ldloc.0
  callvirt instance string [mscorlib]System.Text.Encoding::GetString(uint8*, int32)
  ret
NUL:
  ldnull
  ret
}

// object -> handle (null -> 0)
.method assembly static native int '<mc>new'(object o) cil managed aggressiveinlining
{
  .maxstack 2
  ldarg.0
  brfalse.s Z
  ldarg.0
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object)
  call native int [mscorlib]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype [mscorlib]System.Runtime.InteropServices.GCHandle)
  ret
Z:
  ldc.i4.0
  conv.i
  ret
}

// handle -> object (0 -> null)
.method assembly static object '<mc>get'(native int h) cil managed aggressiveinlining
{
  .maxstack 2
  .locals init (valuetype [mscorlib]System.Runtime.InteropServices.GCHandle g)
  ldarg.0
  brfalse.s Z
  ldarg.0
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int)
  stloc.0
  ldloca.s g
  call instance object [mscorlib]System.Runtime.InteropServices.GCHandle::get_Target()
  ret
Z:
  ldnull
  ret
}

// void dn_free(handle)
.method public static void dn_free(native int h) cil managed
{
  .maxstack 1
  .locals init (valuetype [mscorlib]System.Runtime.InteropServices.GCHandle g)
  ldarg.0
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int)
  stloc.0
  ldloca.s g
  call instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()
  ret
}

// int dn_file_exists(const char *u8)   [System.IO.File::Exists, "bool(string)"]
.method public static int32 dn_file_exists(native int p) cil managed
{
  .maxstack 1
  ldarg.0
  call string '<mc>utf8'(native int)
  call bool [mscorlib]System.IO.File::Exists(string)
  ret
}

// handle dn_fs_open(const char *u8, int mode, int access, int share, int *err)
//   catch FileNotFound=2, DirectoryNotFound=2, UnauthorizedAccess=13, PathTooLong=36, IOException=5
.method public static native int dn_fs_open(native int p, int32 mode, int32 access, int32 share, int32* err) cil managed
{
  .maxstack 6
  .locals init (native int R, int32 E)
  .try {
    ldarg.0
    call string '<mc>utf8'(native int)
    ldarg.1
    ldarg.2
    ldarg.3
    ldc.i4 4096
    newobj instance void [mscorlib]System.IO.FileStream::.ctor(string, valuetype [mscorlib]System.IO.FileMode, valuetype [mscorlib]System.IO.FileAccess, valuetype [mscorlib]System.IO.FileShare, int32)
    call native int '<mc>new'(object)
    stloc.0
    leave DONE
  } filter {
    dup
    isinst [mscorlib]System.IO.FileNotFoundException
    brtrue.s E2
    dup
    isinst [mscorlib]System.IO.DirectoryNotFoundException
    brtrue.s E2
    dup
    isinst [mscorlib]System.UnauthorizedAccessException
    brtrue.s E13
    dup
    isinst [mscorlib]System.IO.PathTooLongException
    brtrue.s E36
    dup
    isinst [mscorlib]System.IO.IOException
    brtrue.s E5
    pop
    ldc.i4.0
    br.s SET
E2: pop
    ldc.i4.2
    br.s SET
E13: pop
    ldc.i4.s 13
    br.s SET
E36: pop
    ldc.i4.s 36
    br.s SET
E5: pop
    ldc.i4.5
SET: dup
    stloc.1
    ldc.i4.0
    cgt.un
    endfilter
  } {
    pop
    ldarg.s err
    ldloc.1
    stind.i4
    leave DONE
  }
DONE:
  ldloc.0
  ret
}

// handle dn_stdout(void)   [System.Console::OpenStandardOutput]
.method public static native int dn_stdout() cil managed
{
  .maxstack 1
  call class [mscorlib]System.IO.Stream [mscorlib]System.Console::OpenStandardOutput()
  call native int '<mc>new'(object)
  ret
}

// handle dn_newbytes(int n)   ["uint8[]", ".ctor", "instance void(int32)"] -> newarr
.method public static native int dn_newbytes(int32 n) cil managed
{
  .maxstack 1
  ldarg.0
  newarr [mscorlib]System.Byte
  call native int '<mc>new'(object)
  ret
}

// void dn_copy_in(void *src, handle arr, int off, int n)  [Marshal::Copy(native int, uint8[], int32, int32)]
.method public static void dn_copy_in(native int src, native int arr, int32 ofs, int32 n) cil managed
{
  .maxstack 4
  ldarg.0
  ldarg.1
  call object '<mc>get'(native int)
  castclass uint8[]
  ldarg.2
  ldarg.3
  call void [mscorlib]System.Runtime.InteropServices.Marshal::Copy(native int, uint8[], int32, int32)
  ret
}

// void dn_write(handle stream, handle arr, int off, int n) [System.IO.Stream::Write, "instance void(uint8[], int32, int32)"]
.method public static void dn_write(native int s, native int arr, int32 ofs, int32 n) cil managed
{
  .maxstack 4
  ldarg.0
  call object '<mc>get'(native int)
  castclass [mscorlib]System.IO.Stream
  ldarg.1
  call object '<mc>get'(native int)
  castclass uint8[]
  ldarg.2
  ldarg.3
  callvirt instance void [mscorlib]System.IO.Stream::Write(uint8[], int32, int32)
  ret
}

// void dn_dispose(handle) [System.IO.Stream::Dispose]
.method public static void dn_dispose(native int s) cil managed
{
  .maxstack 1
  ldarg.0
  call object '<mc>get'(native int)
  castclass [mscorlib]System.IO.Stream
  callvirt instance void [mscorlib]System.IO.Stream::Dispose()
  ret
}

// void dn_delete(const char *u8) [System.IO.File::Delete]
.method public static void dn_delete(native int p) cil managed
{
  .maxstack 1
  ldarg.0
  call string '<mc>utf8'(native int)
  call void [mscorlib]System.IO.File::Delete(string)
  ret
}

// long long dn_freq(void)  ["[System]System.Diagnostics.Stopwatch", "Frequency", "int64"] (static field)
.method public static int64 dn_freq() cil managed
{
  .maxstack 1
  ldsfld int64 [System]System.Diagnostics.Stopwatch::Frequency
  ret
}
.method public static int64 dn_ts() cil managed
{
  .maxstack 1
  call int64 [System]System.Diagnostics.Stopwatch::GetTimestamp()
  ret
}

// handle dn_getenv(const char *u8) [System.Environment::GetEnvironmentVariable, "string(string)"]
.method public static native int dn_getenv(native int p) cil managed
{
  .maxstack 1
  ldarg.0
  call string '<mc>utf8'(native int)
  call string [mscorlib]System.Environment::GetEnvironmentVariable(string)
  call native int '<mc>new'(object)
  ret
}
// handle dn_utf8bytes(handle str): Encoding.UTF8.GetBytes(string)
.method public static native int dn_utf8bytes(native int s) cil managed
{
  .maxstack 2
  call class [mscorlib]System.Text.Encoding [mscorlib]System.Text.Encoding::get_UTF8()
  ldarg.0
  call object '<mc>get'(native int)
  castclass [mscorlib]System.String
  callvirt instance uint8[] [mscorlib]System.Text.Encoding::GetBytes(string)
  call native int '<mc>new'(object)
  ret
}
.method public static int32 dn_arrlen(native int a) cil managed
{
  .maxstack 1
  ldarg.0
  call object '<mc>get'(native int)
  castclass [mscorlib]System.Array
  callvirt instance int32 [mscorlib]System.Array::get_Length()
  ret
}

// ---- the "C program": what libc's OS layer would do ----
.method public static void say(string s) cil managed
{
  .maxstack 1
  ldarg.0
  call void [mscorlib]System.Console::WriteLine(string)
  ret
}

.method public static int32 main(string[] args) cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (native int sout, native int buf, native int fs, int32 err, native int mem, native int env, native int bytes, int32 n, native int path)
  // 1. "hello, world" through Console.OpenStandardOutput + byte[] + Marshal.Copy
  ldc.i4 256
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc mem
  call class [mscorlib]System.Text.Encoding [mscorlib]System.Text.Encoding::get_UTF8()
  ldstr "hello, world (stdout stream)\n"
  callvirt instance uint8[] [mscorlib]System.Text.Encoding::GetBytes(string)
  dup
  ldlen
  conv.i4
  stloc n
  ldc.i4.0
  ldloc mem
  ldloc n
  call void [mscorlib]System.Runtime.InteropServices.Marshal::Copy(uint8[], int32, native int, int32)
  call native int dn_stdout()
  stloc sout
  ldloc n
  call native int dn_newbytes(int32)
  stloc buf
  ldloc mem
  ldloc buf
  ldc.i4.0
  ldloc n
  call void dn_copy_in(native int, native int, int32, int32)
  ldloc sout
  ldloc buf
  ldc.i4.0
  ldloc n
  call void dn_write(native int, native int, int32, int32)
  // 2. a file with a non-ASCII UTF-8 name ("mc1-ete.tmp" with e-acute): create, write, exists, delete
  call class [mscorlib]System.Text.Encoding [mscorlib]System.Text.Encoding::get_UTF8()
  ldstr bytearray (6D 00 63 00 31 00 2D 00 E9 00 74 00 E9 00 2E 00 74 00 6D 00 70 00)
  callvirt instance uint8[] [mscorlib]System.Text.Encoding::GetBytes(string)
  dup
  ldlen
  conv.i4
  stloc n
  ldloc n
  ldc.i4.1
  add
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stloc path
  ldc.i4.0
  ldloc path
  ldloc n
  call void [mscorlib]System.Runtime.InteropServices.Marshal::Copy(uint8[], int32, native int, int32)
  ldloc path
  ldloc n
  add
  ldc.i4.0
  stind.i1
  ldstr "UTF-8 path bytes: {0} (13 = e-acute twice as 2 bytes)"
  ldloc n
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object)
  call void say(string)
  ldloc path
  ldc.i4.1
  ldc.i4.2
  ldc.i4.0
  ldloca err
  call native int dn_fs_open(native int, int32, int32, int32, int32*)
  stloc fs
  ldstr "open CreateNew: handle!=0 {0}, err {1}"
  ldloc fs
  ldc.i4.0
  conv.i
  cgt.un
  box [mscorlib]System.Int32
  ldloc err
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object, object)
  call void say(string)
  ldloc fs
  ldloc buf
  ldc.i4.0
  ldc.i4.5
  call void dn_write(native int, native int, int32, int32)
  ldloc fs
  call void dn_dispose(native int)
  ldloc fs
  call void dn_free(native int)
  ldstr "exists after create: {0}"
  ldloc path
  call int32 dn_file_exists(native int)
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object)
  call void say(string)
  ldloc path
  call void dn_delete(native int)
  ldstr "exists after delete: {0}"
  ldloc path
  call int32 dn_file_exists(native int)
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object)
  call void say(string)
  // 3. open the (now missing) file -> err 2, no exception escapes
  ldc.i4.0
  stloc err
  ldloc path
  ldc.i4.3
  ldc.i4.1
  ldc.i4.1
  ldloca err
  call native int dn_fs_open(native int, int32, int32, int32, int32*)
  stloc fs
  ldstr "open missing: handle {0}, err {1}"
  ldloc fs
  box [mscorlib]System.IntPtr
  ldloc err
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object, object)
  call void say(string)
  // 4. Stopwatch through [System]
  ldstr "Stopwatch.Frequency > 0: {0}, GetTimestamp > 0: {1}"
  call int64 dn_freq()
  ldc.i4.0
  conv.i8
  cgt
  box [mscorlib]System.Int32
  call int64 dn_ts()
  ldc.i4.0
  conv.i8
  cgt
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object, object)
  call void say(string)
  // 5. getenv("PATH") -> string handle -> UTF-8 byte[] handle -> length
  ldstr "PATH"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call native int dn_getenv(native int)
  stloc env
  ldloc env
  call native int dn_utf8bytes(native int)
  stloc bytes
  ldstr "getenv(PATH) UTF-8 length > 0: {0}"
  ldloc bytes
  call int32 dn_arrlen(native int)
  ldc.i4.0
  cgt
  box [mscorlib]System.Int32
  call string [mscorlib]System.String::Format(string, object)
  call void say(string)
  // 6. loud cases, selected by argument
  ldarg.0
  ldlen
  brfalse END
  ldarg.0
  ldc.i4.0
  ldelem.ref
  ldstr "unlisted"
  call bool [mscorlib]System.String::op_Equality(string, string)
  brfalse CAST
  // empty path: ArgumentException is not listed -> escapes loudly
  ldloc mem
  ldc.i4.0
  stind.i1
  ldloc mem
  ldc.i4.3
  ldc.i4.1
  ldc.i4.1
  ldloca err
  call native int dn_fs_open(native int, int32, int32, int32, int32*)
  pop
  br END
CAST:
  // a string handle used as a Stream: InvalidCastException, never type confusion
  ldloc env
  ldloc buf
  ldc.i4.0
  ldc.i4.1
  call void dn_write(native int, native int, int32, int32)
END:
  ldc.i4.s 42
  ret
}
