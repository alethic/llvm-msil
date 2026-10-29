import re
import sys

# scrub.py IN OUT: drop the local scratch prefixes from tool output (file names stay).
text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
text = re.sub(r"[A-Za-z]:[\\/]llvm-msil-scratch[\\/]p3[\\/]atomics-threads[\\/][\w.-]+[\\/]", "", text)
open(sys.argv[2], "w", encoding="utf-8").write(text)
