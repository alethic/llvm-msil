// Run by ../x86-runner.test: exits with sizeof(native int), 8 in a 64-bit
// process and 4 in a 32-bit one, so the exit status shows which host ran
// the image. An IL-only AnyCPU image (ilasm without -pe64), as msil64 images
// are (plan 5.18.7).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'pointer-size' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  sizeof native int
  ret
} // end of method '<msil>Main'
