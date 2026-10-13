// e16: assorted ILAsm spellings the AsmPrinter needs:
//   exact FP constants (float64(0x...), float32(0x...), byte-list form), integer constant forms,
//   .data forms, omitted .maxstack, 'tail.' calls, long branch forms, many locals,
//   [ThreadStatic] on a <Module> field.
#include "common.il"
.assembly e16_syntax {}
.module e16_syntax.dll

.data D_INTS = { int32 (1), int32 (2), int32 (-3) }
.data D_REP  = int8 (7) [5]
.data D_I64  = int64 (0x1122334455667788)
.data D_F    = float64 (1.5)
.data D_STR  = char* ("hi")
.class private explicit ansi sealed 'b12' extends [mscorlib]System.ValueType { .pack 1 .size 12 }
.class private explicit ansi sealed 'b5'  extends [mscorlib]System.ValueType { .pack 1 .size 5 }
.class private explicit ansi sealed 'b8'  extends [mscorlib]System.ValueType { .pack 1 .size 8 }
.class private explicit ansi sealed 'b6'  extends [mscorlib]System.ValueType { .pack 1 .size 6 }
.field static assembly valuetype 'b12' 'ints' at D_INTS
.field static assembly valuetype 'b5'  'rep'  at D_REP
.field static assembly valuetype 'b8'  'i64'  at D_I64
.field static assembly valuetype 'b8'  'f'    at D_F
.field static assembly valuetype 'b6'  'str'  at D_STR

.field static assembly native int 'tls_sp'
.custom instance void [mscorlib]System.ThreadStaticAttribute::.ctor() = (01 00 00 00)
.field static assembly native int 'worker_saw'

.method assembly static void 'worker'() cil managed
{
  .maxstack 2
  ldsfld native int 'tls_sp'
  stsfld native int 'worker_saw'
  ldc.i4 222
  conv.i
  stsfld native int 'tls_sp'
  ret
}

// no .maxstack at all (ilasm default is 8)
.method assembly static int32 'nomaxstack'(int32 a) cil managed { ldarg.0 ldarg.0 ldarg.0 add add ret }

// tail calls
.method assembly static int64 'sum_tail'(int64 n, int64 acc) cil managed
{
  .maxstack 3
  ldarg.0
  brtrue.s REC
  ldarg.1
  ret
REC:
  ldarg.0
  ldc.i4.1
  conv.i8
  sub
  ldarg.1
  ldarg.0
  add
  tail. call int64 'sum_tail'(int64, int64)
  ret
}
.method assembly static int64 'sum_tail_calli'(int64 n, int64 acc) cil managed
{
  .maxstack 4
  ldarg.0
  brtrue.s REC
  ldarg.1
  ret
REC:
  ldarg.0
  ldc.i4.1
  conv.i8
  sub
  ldarg.1
  ldarg.0
  add
  ldftn int64 'sum_tail_calli'(int64, int64)
  tail. calli int64(int64, int64)
  ret
}

// long branch over > 127 bytes using the long form 'br' (ilasm -optimize may shorten)
.method assembly static int32 'longbr'(int32 c) cil managed
{
  .maxstack 1
  ldarg.0
  brtrue FAR
  ldc.i4.0
  ret
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
  nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop nop
FAR:
  ldc.i4 77
  ret
}

// 300 locals; access by index > 255 (ilasm must pick the long ldloc/stloc form)
.method assembly static int32 'manylocals'() cil managed
{
  .maxstack 2
  .locals init (int32 v0, int32 v1, int32 v2, int32 v3, int32 v4, int32 v5, int32 v6, int32 v7, int32 v8, int32 v9, int32 v10, int32 v11, int32 v12, int32 v13, int32 v14, int32 v15, int32 v16, int32 v17, int32 v18, int32 v19, int32 v20, int32 v21, int32 v22, int32 v23, int32 v24, int32 v25, int32 v26, int32 v27, int32 v28, int32 v29, int32 v30, int32 v31, int32 v32, int32 v33, int32 v34, int32 v35, int32 v36, int32 v37, int32 v38, int32 v39, int32 v40, int32 v41, int32 v42, int32 v43, int32 v44, int32 v45, int32 v46, int32 v47, int32 v48, int32 v49, int32 v50, int32 v51, int32 v52, int32 v53, int32 v54, int32 v55, int32 v56, int32 v57, int32 v58, int32 v59, int32 v60, int32 v61, int32 v62, int32 v63, int32 v64, int32 v65, int32 v66, int32 v67, int32 v68, int32 v69, int32 v70, int32 v71, int32 v72, int32 v73, int32 v74, int32 v75, int32 v76, int32 v77, int32 v78, int32 v79, int32 v80, int32 v81, int32 v82, int32 v83, int32 v84, int32 v85, int32 v86, int32 v87, int32 v88, int32 v89, int32 v90, int32 v91, int32 v92, int32 v93, int32 v94, int32 v95, int32 v96, int32 v97, int32 v98, int32 v99, int32 v100, int32 v101, int32 v102, int32 v103, int32 v104, int32 v105, int32 v106, int32 v107, int32 v108, int32 v109, int32 v110, int32 v111, int32 v112, int32 v113, int32 v114, int32 v115, int32 v116, int32 v117, int32 v118, int32 v119, int32 v120, int32 v121, int32 v122, int32 v123, int32 v124, int32 v125, int32 v126, int32 v127, int32 v128, int32 v129, int32 v130, int32 v131, int32 v132, int32 v133, int32 v134, int32 v135, int32 v136, int32 v137, int32 v138, int32 v139, int32 v140, int32 v141, int32 v142, int32 v143, int32 v144, int32 v145, int32 v146, int32 v147, int32 v148, int32 v149, int32 v150, int32 v151, int32 v152, int32 v153, int32 v154, int32 v155, int32 v156, int32 v157, int32 v158, int32 v159, int32 v160, int32 v161, int32 v162, int32 v163, int32 v164, int32 v165, int32 v166, int32 v167, int32 v168, int32 v169, int32 v170, int32 v171, int32 v172, int32 v173, int32 v174, int32 v175, int32 v176, int32 v177, int32 v178, int32 v179, int32 v180, int32 v181, int32 v182, int32 v183, int32 v184, int32 v185, int32 v186, int32 v187, int32 v188, int32 v189, int32 v190, int32 v191, int32 v192, int32 v193, int32 v194, int32 v195, int32 v196, int32 v197, int32 v198, int32 v199, int32 v200, int32 v201, int32 v202, int32 v203, int32 v204, int32 v205, int32 v206, int32 v207, int32 v208, int32 v209, int32 v210, int32 v211, int32 v212, int32 v213, int32 v214, int32 v215, int32 v216, int32 v217, int32 v218, int32 v219, int32 v220, int32 v221, int32 v222, int32 v223, int32 v224, int32 v225, int32 v226, int32 v227, int32 v228, int32 v229, int32 v230, int32 v231, int32 v232, int32 v233, int32 v234, int32 v235, int32 v236, int32 v237, int32 v238, int32 v239, int32 v240, int32 v241, int32 v242, int32 v243, int32 v244, int32 v245, int32 v246, int32 v247, int32 v248, int32 v249, int32 v250, int32 v251, int32 v252, int32 v253, int32 v254, int32 v255, int32 v256, int32 v257, int32 v258, int32 v259, int32 v260, int32 v261, int32 v262, int32 v263, int32 v264, int32 v265, int32 v266, int32 v267, int32 v268, int32 v269, int32 v270, int32 v271, int32 v272, int32 v273, int32 v274, int32 v275, int32 v276, int32 v277, int32 v278, int32 v279, int32 v280, int32 v281, int32 v282, int32 v283, int32 v284, int32 v285, int32 v286, int32 v287, int32 v288, int32 v289, int32 v290, int32 v291, int32 v292, int32 v293, int32 v294, int32 v295, int32 v296, int32 v297, int32 v298, int32 v299)
  ldc.i4 5
  stloc 299
  ldloc 299
  ldc.i4 1
  add
  ret
}

.method public static int32 'main'() cil managed
{
  .entrypoint
  .maxstack 8
  .locals init (class [mscorlib]System.Threading.Thread t)
  ldstr "ldc.r8 float64(0x3FF8000000000000)"  ldc.r8 float64(0x3FF8000000000000)  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  ldstr "ldc.r8 float64(0x7FF0000000000000) inf"  ldc.r8 float64(0x7FF0000000000000)  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)  ldc.i8 0x7FF0000000000000  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.r8 float64(0x8000000000000000) -0"  ldc.r8 float64(0x8000000000000000)  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)  ldc.i8 0x8000000000000000  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.r8 float64(0x7FF8000000000123) NaN payload"  ldc.r8 float64(0x7FF8000000000123)  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)  ldc.i8 0x7FF8000000000123  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.r4 float32(0x3FC00000)"  ldc.r4 float32(0x3FC00000)  conv.r8  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  ldstr "ldc.r8 (00 00 00 00 00 00 F8 3F) byte list, little-endian"  ldc.r8 (00 00 00 00 00 00 F8 3F)  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  // old MSILWriter spelling 'ldc.r8 ( 3FF8000000000000)' -- how is it parsed?
  ldstr "OBSERVE old-writer spelling ldc.r8 ( 3FF8000000000000) bits = "  call void [mscorlib]System.Console::Write(string)
  ldc.r8 ( 3FF8000000000000)  call int64 [mscorlib]System.BitConverter::DoubleToInt64Bits(float64)  call void [mscorlib]System.Console::WriteLine(int64)
  ldstr "ldc.i8 hex"  ldc.i8 0xFFFFFFFFFFFFFFFF  ldc.i8 -1  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.i4 0xFFFFFFFF == -1"  ldc.i4 0xFFFFFFFF  conv.i8  ldc.i8 -1  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.i4 -2147483648"  ldc.i4 -2147483648  conv.i8  ldc.i8 0xFFFFFFFF80000000  call void 'check.i8'(string, int64, int64)
  ldstr "ldc.i8 -9223372036854775808"  ldc.i8 -9223372036854775808  ldc.i8 0x8000000000000000  call void 'check.i8'(string, int64, int64)
  // .data forms
  ldstr ".data { int32(1), int32(2), int32(-3) }[2]"  ldsflda valuetype 'b12' 'ints'  ldc.i4.8  add  ldind.i4  conv.i8  ldc.i8 -3  call void 'check.i8'(string, int64, int64)
  ldstr ".data int8 (7) [5]"  ldsflda valuetype 'b5' 'rep'  ldc.i4.4  add  ldind.u1  conv.i8  ldc.i8 7  call void 'check.i8'(string, int64, int64)
  ldstr ".data int64"  ldsflda valuetype 'b8' 'i64'  ldind.i8  ldc.i8 0x1122334455667788  call void 'check.i8'(string, int64, int64)
  ldstr ".data float64 (1.5)"  ldsflda valuetype 'b8' 'f'  ldind.r8  ldc.r8 1.5  call void 'check.r8'(string, float64, float64)
  ldstr ".data char* (\"hi\") is UTF-16 ('h',0,'i',0)"  ldsflda valuetype 'b6' 'str'  ldind.i4  conv.i8  ldc.i8 0x00690068  call void 'check.i8'(string, int64, int64)
  // misc
  ldstr "no .maxstack (default 8)"  ldc.i4.3  call int32 'nomaxstack'(int32)  conv.i8  ldc.i8 9  call void 'check.i8'(string, int64, int64)
  ldstr "tail. call depth 1e6"  ldc.i8 1000000  ldc.i8 0  call int64 'sum_tail'(int64, int64)  ldc.i8 500000500000  call void 'check.i8'(string, int64, int64)
  ldstr "tail. calli depth 1e6"  ldc.i8 1000000  ldc.i8 0  call int64 'sum_tail_calli'(int64, int64)  ldc.i8 500000500000  call void 'check.i8'(string, int64, int64)
  ldstr "long branch"  ldc.i4.1  call int32 'longbr'(int32)  conv.i8  ldc.i8 77  call void 'check.i8'(string, int64, int64)
  ldstr "300 locals"  call int32 'manylocals'()  conv.i8  ldc.i8 6  call void 'check.i8'(string, int64, int64)
  // ThreadStatic global: main thread sets 111, worker must see 0 and its write must not leak back
  ldc.i4 111  conv.i  stsfld native int 'tls_sp'
  ldnull
  ldftn void 'worker'()
  newobj instance void [mscorlib]System.Threading.ThreadStart::.ctor(object, native int)
  newobj instance void [mscorlib]System.Threading.Thread::.ctor(class [mscorlib]System.Threading.ThreadStart)
  stloc.0
  ldloc.0  callvirt instance void [mscorlib]System.Threading.Thread::Start()
  ldloc.0  callvirt instance void [mscorlib]System.Threading.Thread::Join()
  ldstr "ThreadStatic: worker saw 0"  ldsfld native int 'worker_saw'  conv.i8  ldc.i8 0  call void 'check.i8'(string, int64, int64)
  ldstr "ThreadStatic: main still 111"  ldsfld native int 'tls_sp'  conv.i8  ldc.i8 111  call void 'check.i8'(string, int64, int64)
  call int32 'report'()
  ret
}
