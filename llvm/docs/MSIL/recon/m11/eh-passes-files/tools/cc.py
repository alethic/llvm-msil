import json, sys
BS = chr(92)
db = json.load(open(r'D:/llvm-msil/build/compile_commands.json'))
for e in db:
    f = e['file'].replace(BS, '/').lower()
    if f.endswith(sys.argv[1].lower()):
        print(e['directory']); print(e['command']); print(e.get('output'))
