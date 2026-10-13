// e17b: entrypoint returning void -> exit code 0
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly e17b_void {}
.method public static void 'main'() cil managed { .entrypoint .maxstack 1 ret }
