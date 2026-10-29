#!/usr/bin/env python3
"""Replace the stub methods of proto.c's -S output with the IL that the E1 design emits.

usage: patch-proto.py IN.il OUT.il [--type ext|boxed]
  __msil_sjlj_throw(native int st)      -> throw (ext: new ExternalException(msg, (int)st))
  __msil_sjlj_invoke_<f>(args..., st)   -> body in .try; filter = BCL type test + __msil_sjlj_match
"""
import re, sys

src, dst = sys.argv[1], sys.argv[2]
kind = sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--type" else "ext"
# ext-inline: the whole match in the filter (no call of __msil_sjlj_match)
inline = kind == "ext-inline"
if inline:
    kind = "ext"
il = open(src).read()

def method(name):
    m = re.search(r"^\.method [^\n]*'%s'\(([^)]*)\) cil managed\n\{.*?^\} // end of method '%s'\n"
                  % (re.escape(name), re.escape(name)), il, re.S | re.M)
    assert m, name
    return m

THROW = {
 "ext": """  ldstr "longjmp: the setjmp of this jmp_buf is not active"
  ldarg.0
  conv.i4
  newobj instance void [mscorlib]System.Runtime.InteropServices.ExternalException::.ctor(string, int32)
  throw""",
 "boxed": """  ldarg.0
  box [mscorlib]System.IntPtr
  throw""",
}[kind]
FILTER_TEST = {
 "ext": """    isinst [mscorlib]System.Runtime.InteropServices.ExternalException
    dup
    brtrue.s SJ_M
    pop
    ldc.i4.0
    br.s SJ_E
  SJ_M:
    callvirt instance int32 [mscorlib]System.Exception::get_HResult()""",
 "boxed": """    isinst [mscorlib]System.IntPtr
    dup
    brtrue.s SJ_M
    pop
    ldc.i4.0
    br.s SJ_E
  SJ_M:
    unbox.any native int
    conv.i4""",
}[kind]

m = method("__msil_sjlj_throw")
head = il[m.start():il.index("{", m.start()) + 1]
il = il[:m.start()] + head + "\n  .maxstack 3\n" + THROW + "\n} // end of method '__msil_sjlj_throw'\n" + il[m.end():]

for name in sorted(set(re.findall(r"'(__msil_sjlj_invoke_[A-Za-z0-9_]+)'\(", il))):
    m = method(name)
    params = [p for p in m.group(1).split(",") if p.strip()]
    st = len(params) - 1                       # the status block is the last argument
    body = il[m.start():m.end()]
    hdr_end = body.index("{") + 1
    lines = body[hdr_end:].splitlines()
    out = []
    for ln in lines:
        s = ln.strip()
        if s.startswith(".maxstack") or s.startswith("//") or not s or s.startswith("} // end of method"):
            continue
        if s.startswith("ldsfld native int 'stub_sink'"):
            continue
        out.append(ln)
    # drop the stand-in store (ldarg st; volatile.; stind.i) that kept st alive in C
    txt = "\n".join(out)
    txt = re.sub(r"\s*ldarg\.%d\n\s*volatile\.\n\s*stind\.i" % st, "", txt) if st <= 3 else txt
    rets = txt.count(" ret")
    void = " void " in body.split("(")[0]
    if void:
        txt = re.sub(r"(?m)^\s*ret\s*$", "    leave SJ_DONE", txt)
        tail = "  SJ_DONE:\n  ret"
        locs = ""
    else:
        txt = re.sub(r"(?m)^\s*ret\s*$", "    stloc SJ_R\n    leave SJ_DONE", txt)
        ret_t = body.split("static ")[1].split(" '")[0]
        locs = "  .locals (%s SJ_R)\n" % ret_t
        tail = "  SJ_DONE:\n  ldloc SJ_R\n  ret"
    if inline:
        match = ("\n    ldarg %d\n    conv.i4\n    bne.un.s SJ_N\n    ldarg %d\n    ldind.i4\n"
                 "    ldc.i4.0\n    cgt.un\n    br.s SJ_E\n  SJ_N:\n    ldc.i4.0\n  SJ_E:\n    endfilter\n"
                 % (st, st))
    else:
        match = ("\n    ldarg %d\n    call int32 '__msil_sjlj_match'(int32, native int)\n  SJ_E:\n"
                 "    endfilter\n" % st)
    new = (body[:hdr_end] + "\n  .maxstack 4\n" + locs + "  .try {\n" + txt + "\n  } filter {\n"
           + FILTER_TEST + match
           + "  } {\n    pop\n    leave SJ_DONE\n  }\n" + tail + "\n} // end of method '%s'\n" % name)
    il = il[:m.start()] + new + il[m.end():]

open(dst, "w").write(il)
