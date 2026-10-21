"""Checks the CIL opcode values written in llvm/lib/Target/MSIL/MSILInstr*.td
(the 'cilop' argument after each asm string) against the ECMA-335 table.
Usage: check_td_opcodes.py <dir of MSILInstr*.td>"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cilopcodes as K  # noqa: E402

byname = {n: v for n, v, _ in K.OPCODES}
bad = checked = 0
for td in sorted(glob.glob(os.path.join(sys.argv[1], "MSILInstr*.td"))):
    text = open(td).read()
    # Records: a string literal (possibly with $operands, possibly a class
    # template parameter) followed by ', 0x....>' . Multiclass calls pass
    # '<"name", 0xNN>'.
    for m in re.finditer(r'"([^"]*)"\s*,\s*(0x[0-9A-Fa-f]+)\s*>', text):
        asm, val = m.group(1), int(m.group(2), 16)
        mnem = asm.replace("${flags}", "").split()[0] if asm.strip() else ""
        line = text.count("\n", 0, m.start()) + 1
        if asm.startswith("MemoryBarrier()"):
            mnem = "call"  # second half of "call void [mscorlib]...::" "MemoryBarrier()"
        if mnem not in byname:
            print("%s:%d: unknown mnemonic %r (0x%X)" % (os.path.basename(td), line, mnem, val))
            bad += 1
            continue
        checked += 1
        if byname[mnem] != val:
            print("%s:%d: %s is 0x%X in ECMA-335, the .td says 0x%X"
                  % (os.path.basename(td), line, mnem, byname[mnem], val))
            bad += 1
    # Multiline asm strings: '"call void ..."\n "MemoryBarrier()", 0x28>'
print("checked %d opcode values, %d problems" % (checked, bad))
