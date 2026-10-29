import re, sys
# Print, per method of a JitDisasm file, the loop blocks: every run of blocks whose last
# instruction branches back to the first block of the run (comments dropped).
# Usage: loops.py <disasm.txt>
txt = open(sys.argv[1]).read()


def clean(text):
    out = []
    for l in text.splitlines():
        l = l.strip()
        if not l or l.startswith(';'):
            continue
        out.append(re.sub(r'\s+', ' ', re.sub(r'\s*;.*', '', l)))
    return out


for part in re.split(r'; Assembly listing for method ', txt)[1:]:
    name = part.split('(')[0]
    blocks = re.split(r'\n(G_M\d+_IG\d+):[^\n]*', part)
    labels, texts = blocks[1::2], [clean(t) for t in blocks[2::2]]
    print(name)
    for idx, ins in enumerate(texts):
        for j in range(idx + 1):
            if any(re.search(r'\b' + re.escape(labels[j]) + r'\b', i) for i in ins[-2:]):
                print('   ' + ' || '.join(labels[k] + ': ' + ' | '.join(texts[k])
                                         for k in range(j, idx + 1)))
                break
