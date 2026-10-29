import re, sys
for p in sys.argv[1:]:
    txt = open(p, encoding='utf-8', errors='replace').read()
    tot = 0; rows = []
    for m in re.finditer(r"\.method [^\n]*?\s([\w$<>'.]+)\(.*?// Code size\s+(\d+)", txt, re.S):
        rows.append((m.group(1), int(m.group(2)))); tot += int(m.group(2))
    big = sorted(rows, key=lambda r: -r[1])[:8]
    print(p, 'total IL bytes', tot, 'methods', len(rows), '|', ', '.join(f'{n}={s}' for n, s in big))
