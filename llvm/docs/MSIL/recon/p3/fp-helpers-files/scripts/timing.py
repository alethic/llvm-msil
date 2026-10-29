"""Timing batch (best of N whole-process wall times) for the i128 and complex variants."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tc

N = 3
S = tc.SCR
JOBS = [
    # i128 workload, 20 repetitions
    ('nt128 inline (M12)', 'wsl8', S + '/i128/nt-inl.exe', ['20']),
    ('nt128 inline (M12)', 'wsl10', S + '/i128/nt-inl.exe', ['20']),
    ('nt128 compiler-rt helper', 'wsl8', S + '/i128/nt-helper.exe', ['20']),
    ('nt128 compiler-rt helper', 'wsl10', S + '/i128/nt-helper.exe', ['20']),
    ('nt128 UInt128/Int128 helper', 'wsl8', S + '/i128/nt-int128.exe', ['20']),
    ('nt128 UInt128/Int128 helper', 'wsl10', S + '/i128/nt-int128.exe', ['20']),
    ('nt128 outlined linkonce_odr', 'wsl8', S + '/i128/nt-outl.exe', ['20']),
    ('nt128 outlined linkonce_odr', 'wsl10', S + '/i128/nt-outl.exe', ['20']),
    ('nt128 inline (win triple)', 'net8', S + '/i128/nt-inl-win.exe', ['20']),
    ('nt128 inline (win triple)', 'netfx', S + '/i128/nt-inl-win.exe', ['20']),
    ('nt128 outlined (win triple)', 'net8', S + '/i128/nt-outl-win.exe', ['20']),
    ('nt128 outlined (win triple)', 'netfx', S + '/i128/nt-outl-win.exe', ['20']),
    ('many36 outlined linkonce_odr', 'wsl8', S + '/i128/many36-o.exe', ['1']),
    ('many36 outlined linkonce_odr', 'wsl10', S + '/i128/many36-o.exe', ['1']),
    ('nt128 native (libgcc)', 'native-lin', S + '/i128/nt-native-libgcc', ['20']),
    ('nt128 inline -O0', 'wsl10', S + '/i128/nt-inl-O0.exe', ['20']),
    # one function with 20 / 36 i128 divisions, called 200,000 times
    ('many20 inline', 'wsl8', S + '/i128/many20.exe', ['1']),
    ('many20 inline', 'wsl10', S + '/i128/many20.exe', ['1']),
    ('many20 helper', 'wsl8', S + '/i128/many20-h.exe', ['1']),
    ('many20 helper', 'wsl10', S + '/i128/many20-h.exe', ['1']),
    ('many36 inline (MinOpts)', 'wsl8', S + '/i128/many36.exe', ['1']),
    ('many36 inline (MinOpts)', 'wsl10', S + '/i128/many36.exe', ['1']),
    ('many36 helper', 'wsl8', S + '/i128/many36-h.exe', ['1']),
    ('many36 helper', 'wsl10', S + '/i128/many36-h.exe', ['1']),
    ('many36 native', 'native-lin', S + '/i128/many36-native', ['1']),
    # f16 conversion throughput (2^28 each)
    ('f16 trunc native (VS compiler-rt)', 'native-win', S + '/f16/f16b-native.exe', ['t']),
    ('f16 trunc native -mf16c', 'native-win', S + '/f16/f16b-native-f16c.exe', ['t']),
    ('f16 trunc MSIL compiler-rt', 'net8', S + '/f16/f16b-win.exe', ['t']),
    ('f16 trunc MSIL compiler-rt', 'net10', S + '/f16/f16b-win.exe', ['t']),
    ('f16 trunc MSIL compiler-rt', 'netfx', S + '/f16/f16b-win.exe', ['t']),
    ('f16 trunc MSIL System.Half', 'net8', S + '/f16/f16b-win-half.exe', ['t']),
    ('f16 trunc MSIL System.Half', 'net10', S + '/f16/f16b-win-half.exe', ['t']),
    ('f16 ext native (VS compiler-rt)', 'native-win', S + '/f16/f16b-native.exe', ['e']),
    ('f16 ext native -mf16c', 'native-win', S + '/f16/f16b-native-f16c.exe', ['e']),
    ('f16 ext MSIL compiler-rt', 'net8', S + '/f16/f16b-win.exe', ['e']),
    ('f16 ext MSIL compiler-rt', 'net10', S + '/f16/f16b-win.exe', ['e']),
    ('f16 ext MSIL compiler-rt', 'netfx', S + '/f16/f16b-win.exe', ['e']),
    ('f16 ext MSIL System.Half', 'net8', S + '/f16/f16b-win-half.exe', ['e']),
    ('f16 ext MSIL System.Half', 'net10', S + '/f16/f16b-win-half.exe', ['e']),
    # complex
    ('cxbench compiler-rt helpers', 'net8', S + '/cx/cxb-crt.exe', []),
    ('cxbench compiler-rt helpers', 'net10', S + '/cx/cxb-crt.exe', []),
    ('cxbench compiler-rt helpers', 'netfx', S + '/cx/cxb-crt.exe', []),
    ('cxbench -fcx-limited-range', 'net8', S + '/cx/cxb-limited.exe', []),
    ('cxbench -fcomplex-arithmetic=improved', 'net8', S + '/cx/cxb-improved.exe', []),
    ('cxbench native (VS compiler-rt)', 'native-win', S + '/cx/cxb-native.exe', []),
    ('cxbench C# System.Numerics.Complex', 'native-win', S + '/cx/csb/bin/Release/net8.0/csb.exe', []),
    ('cxbench C# System.Numerics.Complex net10', 'native-win', S + '/cx/csb/bin/Release/net10.0/csb.exe', []),
    ('cxbench C# System.Numerics.Complex net48', 'native-win', S + '/cx/csb/bin/Release/net48/csb.exe', []),
]
res = []
for name, kind, exe, args in JOBS:
    runs = [tc.run(kind, exe, args) for _ in range(N)]
    best = min(runs, key=lambda r: r[2])
    res.append({'name': name, 'kind': kind, 'exe': os.path.basename(exe), 'best_s': round(best[2], 2),
                'all_s': [round(r[2], 2) for r in runs], 'out': best[0][-200:], 'rc': best[1]})
    print(f'{name:45s} {kind:10s} best {best[2]:7.2f}s  all {[round(r[2], 2) for r in runs]}  rc={best[1]}', flush=True)
json.dump(res, open(sys.argv[1], 'w'), indent=1)
