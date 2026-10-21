import sys
BS = chr(92)
NL = chr(10)
p = sys.argv[1]
s = open(p, encoding='utf-8', newline='').read()
def sub(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
def L(*lines): return NL.join(lines) + NL
sub(L('; DEFINE:   --implicit-check-not="Windows exception handling preparation"'),
    L('; DEFINE:   --implicit-check-not="Windows exception handling preparation" ' + BS,
      '; DEFINE:   --implicit-check-not="Lower invoke and unwind" ' + BS,
      '; DEFINE:   --implicit-check-not="Remove unreachable blocks from the CFG"'))
sub(L('; CHECK:       Lower invoke and unwind, for unwindless code generators',
      '; CHECK:       Remove unreachable blocks from the CFG',
      '; CHECK:       Expand indirectbr instructions',
      '; O2:          CodeGen Prepare',
      '; CHECK:       Lower invoke and unwind, for unwindless code generators',
      '; CHECK:       Remove unreachable blocks from the CFG',
      '; CHECK:       Expand Atomic instructions'),
    L('; CHECK:       Lower invoke and unwind, for unwindless code generators',
      '; CHECK:       Remove unreachable blocks from the CFG',
      '; CHECK:       Expand indirectbr instructions',
      '; CHECK:       Shadow Stack GC Lowering',
      '; CHECK:       Remove unreachable blocks from the CFG',
      '; O2:          CodeGen Prepare',
      '; CHECK:       Expand Atomic instructions'))
open(p, 'w', encoding='utf-8', newline='').write(s)
