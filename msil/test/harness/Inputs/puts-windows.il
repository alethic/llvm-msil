// Output through the Phase-1 'puts' import as the backend declares it for
// windows-msvc triples: pinvokeimpl("ucrtbase" nomangle cdecl), ImplMap flags
// 0x0201 (plan D14 as amended for M18, 5.10.11). The C strings come from
// Marshal::StringToHGlobalAnsi, so the module needs no data segment.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'puts-windows' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldstr "hello from puts"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call int32 'puts'(native int)
  pop
  ldstr "second line"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call int32 'puts'(native int)
  pop
  ldc.i4.7
  ret
} // end of method 'main'

// ---- undefined external functions: P/Invoke ----
.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'
