.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly 'm-with' { .ver 0:0:0:0 }
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 1
  ldstr "*"
  call char& modreq([System.Runtime]System.Runtime.InteropServices.InAttribute) [System.Runtime.InteropServices]System.Runtime.InteropServices.Marshalling.Utf16StringMarshaller::GetPinnableReference(string)
  ldind.u2
  ret
}
