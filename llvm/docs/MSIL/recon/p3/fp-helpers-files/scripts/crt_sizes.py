import glob, os, re, subprocess, sys
D = sys.argv[1]
rows = []
for o in sorted(glob.glob(D + '/*.o')):
    out = subprocess.run(['D:/llvm-msil/build/bin/llvm-cil-dump.exe', o], capture_output=True, text=True).stdout
    for m in re.finditer(r"\.method '([^']+)' ([^\n]*?) flags=[^\n]*\n(.*?)(?=\n  \.method|\Z)", out, re.S):
        body = m.group(3)
        n = len(re.findall(r'^\s+L\d+:', body, re.M))
        loc = 'localloc' in body
        calls = sorted(set(re.findall(r"call [^\n]*?'([^']+)'", body)))
        rows.append((m.group(1), m.group(2), n, loc, calls))
for r in rows:
    print(f'{r[0]:16s} {r[2]:5d} {"localloc" if r[3] else "        "} {r[1][:60]:60s} {",".join(r[4])}')
print('total IL instructions', sum(r[2] for r in rows))
