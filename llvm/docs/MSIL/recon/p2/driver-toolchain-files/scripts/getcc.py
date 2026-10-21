import json, sys
BS = chr(92)
cc = json.load(open(r'D:/llvm-msil/build/compile_commands.json'))
for e in cc:
    f = e['file'].replace(BS, '/')
    for want in sys.argv[1:]:
        if f.endswith(want):
            print(e['directory']); print(e['command']); print(e.get('output', '')); print()
