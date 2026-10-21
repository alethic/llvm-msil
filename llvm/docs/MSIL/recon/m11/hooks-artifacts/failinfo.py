"""Print, for each FAILED lit test in the given logs, the last RUN line and the error lines."""
import re
import sys

for log in sys.argv[1:]:
    text = open(log, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"\*{20} TEST '([^']+)' FAILED \*{20}\n(.*?)\n\*{20}\n", text, re.S):
        name, body = m.group(1), m.group(2)
        runs = re.findall(r"^# RUN: at line (\d+)", body, re.M)
        errs = [l for l in body.splitlines()
                if re.search(r"command not found|error:|Assertion|LLVM ERROR|expected string|"
                             r"not found in input|No such file|unsupported|Exit Code", l)]
        print("== %s (last RUN line %s)" % (name, runs[-1] if runs else "?"))
        for l in errs[:8]:
            print("   " + l[:300])
