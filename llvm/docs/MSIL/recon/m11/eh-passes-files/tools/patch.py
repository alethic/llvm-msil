import sys
d = sys.argv[1]
def sub(path, old, new):
    s = open(path, encoding='utf-8', newline='').read()
    assert s.count(old) == 1, (path, old)
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
A = d + '/build/A/MSILTargetMachine.cpp'
B = d + '/build/B/MSILTargetMachine.cpp'
nl = '\r\n' if '\r\n' in open(A, encoding='utf-8', newline='').read() else '\n'
def L(*lines): return nl.join(lines)
sub(A, L('  void addIRPasses() override;', '  void addISelPrepare() override;'),
       L('  void addIRPasses() override;', '  void addPassesToHandleExceptions() override;',
         '  void addISelPrepare() override;'))
sub(A, L('  TargetPassConfig::addIRPasses();', '}', '', 'void MSILPassConfig::addISelPrepare() {'),
       L('  TargetPassConfig::addIRPasses();', '}', '',
         'void MSILPassConfig::addPassesToHandleExceptions() {',
         '  // Nothing to do: the exception model is always None (decision D8), and',
         '  // addIRPasses has already lowered every invoke and removed the landing pads',
         '  // that became unreachable. The base class would add LowerInvoke and',
         '  // UnreachableBlockElim a second time.',
         '}', '', 'void MSILPassConfig::addISelPrepare() {'))
sub(B, L('  // There is no exception model: lower invokes into calls and delete the',
         '  // unreachable landing pads.',
         '  addPass(createLowerInvokePass());',
         '  addPass(createUnreachableBlockEliminationPass());', '', ''), '')
print('patched, newline', repr(nl))
