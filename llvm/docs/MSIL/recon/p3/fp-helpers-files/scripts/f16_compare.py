"""Compare the f16conv matrix: per variant, which lines differ from the VS compiler-rt reference."""
import json, sys
res = json.load(open(sys.argv[1]))
ref = res[0]
print('| Variant | A s | B s | C s | D s | differs from compiler-rt (lines) |')
print('|---|---|---|---|---|---|')
for row in res:
    diffs = []
    for part in 'ABCDS':
        a = ref['parts'][part]['out'].splitlines()
        b = row['parts'][part]['out'].splitlines()
        if row['parts'][part]['rc'] != 0:
            diffs.append(f'{part}: rc={row["parts"][part]["rc"]}')
            continue
        for x, y in zip(a, b):
            if x != y:
                key = ' '.join(x.split()[:3]) if part != 'S' else x.split(' ->')[0]
                diffs.append(f'{part}:{key}')
        if len(a) != len(b):
            diffs.append(f'{part}: {len(b)} lines vs {len(a)}')
    t = row['parts']
    print(f"| {row['desc']} | {t['A']['sec']} | {t['B']['sec']} | {t['C']['sec']} | {t['D']['sec']} | {'; '.join(diffs) or 'none'} |")
print()
print('Reference output (native Windows, VS compiler-rt):')
for part in 'ABCD':
    print(ref['parts'][part]['out'].rstrip())
