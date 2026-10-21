import json, sys
cc = json.load(open(r"D:\llvm-msil\build\compile_commands.json"))
for e in cc:
    f = e["file"].replace("\\", "/")
    if f.endswith(sys.argv[1]):
        print(e["directory"])
        print(e["command"])
