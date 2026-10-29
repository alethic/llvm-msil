"""Print the timing tables of the report from timing.json."""
import json, sys
res = json.load(open(sys.argv[1]))
by = {(r['name'], r['kind']): r for r in res}


def row(name, kinds):
    cells = []
    for k in kinds:
        r = by.get((name, k))
        cells.append('-' if r is None else f"{r['best_s']:.2f} ({', '.join(f'{x:.1f}' for x in r['all_s'])})")
    return cells


def table(title, names, kinds):
    print(f'### {title}')
    print('| Variant | ' + ' | '.join(kinds) + ' |')
    print('|---|' + '---|' * len(kinds))
    for n in names:
        print(f'| {n} | ' + ' | '.join(row(n, kinds)) + ' |')
    print()


table('i128 nt128 (20 reps)', ['nt128 inline (M12)', 'nt128 compiler-rt helper', 'nt128 UInt128/Int128 helper',
      'nt128 outlined linkonce_odr', 'nt128 inline -O0', 'nt128 native (libgcc)'], ['wsl8', 'wsl10', 'native-lin'])
table('i128 nt128 win triple', ['nt128 inline (win triple)', 'nt128 outlined (win triple)'], ['net8', 'netfx'])
table('many', ['many20 inline', 'many20 helper', 'many36 inline (MinOpts)', 'many36 helper', 'many36 outlined linkonce_odr',
      'many36 native'], ['wsl8', 'wsl10', 'native-lin'])
table('f16', ['f16 trunc native (VS compiler-rt)', 'f16 trunc native -mf16c', 'f16 trunc MSIL compiler-rt',
      'f16 trunc MSIL System.Half', 'f16 ext native (VS compiler-rt)', 'f16 ext native -mf16c', 'f16 ext MSIL compiler-rt',
      'f16 ext MSIL System.Half'], ['native-win', 'net8', 'net10', 'netfx'])
table('complex', ['cxbench compiler-rt helpers', 'cxbench -fcx-limited-range', 'cxbench -fcomplex-arithmetic=improved',
      'cxbench native (VS compiler-rt)', 'cxbench C# System.Numerics.Complex', 'cxbench C# System.Numerics.Complex net10',
      'cxbench C# System.Numerics.Complex net48'], ['native-win', 'net8', 'net10', 'netfx'])
for r in res:
    if r['rc'] != 0:
        print('NONZERO', r['name'], r['kind'], r['rc'])
outs = {}
for r in res:
    outs.setdefault(r['name'].split(' ')[0], set()).add(r['out'].strip().splitlines()[-1] if r['out'].strip() else '')
print('distinct last output lines per program:', {k: sorted(v) for k, v in outs.items()})
