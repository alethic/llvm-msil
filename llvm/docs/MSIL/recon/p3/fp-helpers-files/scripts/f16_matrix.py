"""Run every f16conv variant part by part and record outputs and wall times (JSON)."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tc

os.chdir(tc.SCR + '/f16')
VARIANTS = [
    ('native-win', 'f16-native-vs.exe', 'native Windows, VS compiler-rt'),
    ('native-win', 'f16-native-crt.exe', 'native Windows, this tree compiler-rt'),
    ('native-lin', 'f16-native-libgcc', 'native Linux, libgcc_s'),
    ('native-lin', 'f16-native-lin-crt', 'native Linux, this tree compiler-rt'),
    ('net8', 'f16-win.exe', 'MSIL win, compiler-rt helpers, .NET 8'),
    ('net10', 'f16-win.exe', 'MSIL win, compiler-rt helpers, .NET 10'),
    ('netfx', 'f16-win.exe', 'MSIL win, compiler-rt helpers, .NET Framework 4.8'),
    ('wsl8', 'f16-lin.exe', 'MSIL lin, compiler-rt helpers, WSL .NET 8'),
    ('wsl10', 'f16-lin.exe', 'MSIL lin, compiler-rt helpers, WSL .NET 10'),
    ('net8', 'f16-win-half.exe', 'MSIL win, System.Half, .NET 8'),
    ('net10', 'f16-win-half.exe', 'MSIL win, System.Half, .NET 10'),
    ('wsl10', 'f16-lin-half.exe', 'MSIL lin, System.Half, WSL .NET 10'),
]
out = sys.argv[1]
res = []
for kind, exe, desc in VARIANTS:
    row = {'kind': kind, 'exe': exe, 'desc': desc, 'parts': {}}
    for part in 'ABCDS':
        o, rc, t, err = tc.run(kind, exe, [part], timeout=3600)
        row['parts'][part] = {'out': o, 'rc': rc, 'sec': round(t, 2), 'err': err[-300:]}
        print(desc, part, rc, round(t, 1), flush=True)
    res.append(row)
    json.dump(res, open(out, 'w'), indent=1)
