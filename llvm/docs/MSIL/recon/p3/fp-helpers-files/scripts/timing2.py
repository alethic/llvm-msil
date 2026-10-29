"""Second timing batch: compiler-rt's i128 algorithm as ABI-neutral IR on the Windows triple."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tc
S = tc.SCR
JOBS = [
    ('nt128 inline (win triple)', 'net8', S + '/i128/nt-inl-win.exe', ['20']),
    ('nt128 inline (win triple)', 'netfx', S + '/i128/nt-inl-win.exe', ['20']),
    ('nt128 outlined (win triple)', 'net8', S + '/i128/nt-outl-win.exe', ['20']),
    ('nt128 outlined (win triple)', 'netfx', S + '/i128/nt-outl-win.exe', ['20']),
    ('nt128 compiler-rt IR helpers (win triple)', 'net8', S + '/i128/nt-irhelp-win.exe', ['20']),
    ('nt128 compiler-rt IR helpers (win triple)', 'netfx', S + '/i128/nt-irhelp-win.exe', ['20']),
    ('nt128 native win (VS compiler-rt)', 'native-win', S + '/i128/nt-native-vs.exe', ['20']),
]
res = []
for name, kind, exe, args in JOBS:
    runs = [tc.run(kind, exe, args) for _ in range(3)]
    best = min(runs, key=lambda r: r[2])
    res.append({'name': name, 'kind': kind, 'exe': os.path.basename(exe), 'best_s': round(best[2], 2),
                'all_s': [round(r[2], 2) for r in runs], 'out': best[0][-200:], 'rc': best[1]})
    print(f'{name:45s} {kind:10s} best {best[2]:7.2f}s  all {[round(r[2], 2) for r in runs]}  rc={best[1]}', flush=True)
json.dump(res, open(sys.argv[1], 'w'), indent=1)
