import sys
p = sys.argv[1]
s = open(p, encoding='utf-8', newline='').read()
old = """void MSILPassConfig::addPassesToHandleExceptions() {
  // Nothing to do: the exception model is always None (decision D8), and
  // addIRPasses has already lowered every invoke and removed the landing pads
  // that became unreachable. The base class would add LowerInvoke and
  // UnreachableBlockElim a second time.
}
"""
new = """void MSILPassConfig::addPassesToHandleExceptions() {
  // The exception model is always None (decision D8), and addIRPasses has
  // already lowered every invoke into a call and removed the landing pads
  // that became unreachable, before the target and generic IR passes.
  // TargetPassConfig would add LowerInvoke and UnreachableBlockElim a second
  // time, after CodeGenPrepare.
  assert(TM->getMCAsmInfo().getExceptionHandlingType() ==
             ExceptionHandling::None &&
         "MSIL has no exception model (decision D8)");
}
"""
assert s.count(old) == 1
s = s.replace(old, new)
inc_old = '#include "llvm/IR/Function.h"\n'
assert s.count(inc_old) == 1
s = s.replace(inc_old, inc_old + '#include "llvm/MC/MCAsmInfo.h"\n')
open(p, 'w', encoding='utf-8', newline='').write(s)
