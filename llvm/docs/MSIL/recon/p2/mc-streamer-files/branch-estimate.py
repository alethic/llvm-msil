import importlib.util, os, sys, struct

# reuse encode_method without running the comparison: exec the module up to the main loop
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'encode-check.py')).read().split('stats = Counter()')[0]
sys.argv = ['x', 'ecma-opcodes.txt', 'corpus', 'bodies']
g = {}
exec(src, g)
total = short = 0
for fn in sorted(os.listdir('corpus')):
    if not fn.endswith('.il'): continue
    text = open(os.path.join('corpus', fn), encoding='utf-8-sig').read().split('\n')
    i = 0
    while i < len(text):
        if text[i].startswith('.method') and 'pinvokeimpl' not in text[i]:
            j = i + 1
            while not text[j].startswith('} // end of method'): j += 1
            body = text[i + 2:j]
            code, toks = g['encode_method'](body)
            # walk the code to find long branches (0x38-0x44) and their offsets
            r = 0
            ops = {v: o for (v, o) in g['OPS'].values()}
            while r < len(code):
                v = code[r]; r += 1
                if v == 0xFE: v = 0xFE00 | code[r]; r += 1
                o = ops[v]
                if o == 'InlineBrTarget':
                    rel = struct.unpack('<i', code[r:r+4])[0]; r += 4; total += 1
                    if -128 <= rel <= 124: short += 1
                elif o == 'InlineSwitch':
                    n = struct.unpack('<I', code[r:r+4])[0]; r += 4 + 4*n
                else:
                    r += {'InlineNone':0,'ShortInlineVar':1,'ShortInlineI':1,'InlineVar':2,'InlineI':4,'InlineI8':8,'ShortInlineR':4,'InlineR':8}.get(o, 4)
            i = j
        i += 1
print(f'long branches {total}, offset fits a short form (conservative) {short} ({100*short/total:.1f}%), bytes saved ~{3*short}')
