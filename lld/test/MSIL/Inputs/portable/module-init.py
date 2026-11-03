"""Helpers for the module initializer of msil64-unknown-dotnet outputs, for
lld/test/MSIL's portable-*.test files (ImplementationPlan.md 5.18.7 PE5,
PE6).

Usage:
  module-init.py strip-guard <listing>
      Print llvm-cil-dump's canonical listing <listing> unchanged except in
      '<Module>'::'.cctor': its first ten instructions must be the width and
      byte-order guard of PE6,
        ldc.i4.m1; conv.i; conv.u8; ldc.i8 -1; bne.un <fail>;
        ldc.i4.1; call uint8[](int32) [mscorlib]System.BitConverter::'GetBytes';
        ldc.i4.0; ldelem.u1; brfalse <fail>
      (short or long branch forms, scope [mscorlib] or any other), and are
      left out, and so is every 'ldstr "<T-P10 text>"; newobj
      PlatformNotSupportedException::.ctor(string); throw' triple. The rest
      is what Inputs/cctor.py summarizes for host outputs, so the portable
      tests read their constructor calls and address fields with it. A
      '.cctor' that does not start with the guard prints
      'module-init.py: no guard: <first instruction>' on standard error and
      exits 1 (a missing '.cctor' prints 'module-init.py: no .cctor').
  module-init.py bounds <summary> <section>
      Read Inputs/cctor.py's summary lines and print
      '<section>: start=data+<a> stop=data+<b> size=<b - a>' from the lines
      'field __start_<section> = data+<a>' and
      'field __stop_<section> = data+<b>', or '<section>: no bounds'.
"""

import re
import sys

INSTR = re.compile(r"^    L(\d+): (\S+)(?: (.*))?$")
GUARD = [
    ("ldc.i4.m1", None),
    ("conv.i", None),
    ("conv.u8", None),
    ("ldc.i8", r"-1"),
    ("bne.un", r"L\d+"),
    ("ldc.i4.1", None),
    ("call", r"uint8\[\]\(int32\) (\[[^\]]+\])?System\.BitConverter::'GetBytes'"),
    ("ldc.i4.0", None),
    ("ldelem.u1", None),
    ("brfalse", r"L\d+"),
]
MESSAGE = re.compile(r'^"[^"]* is built for msil64-unknown-dotnet and needs a ')
BRANCHES = ("bne.un.s", "brfalse.s")


def norm(op):
    return op[:-2] if op in BRANCHES else op


def strip_guard(path):
    lines = open(path, encoding="utf-8").read().splitlines()
    start = None
    in_module = False
    for i, line in enumerate(lines):
        if line.startswith(".class "):
            in_module = line.startswith(".class '<Module>' ")
        elif in_module and line.startswith("  .method '.cctor' "):
            start = i + 1
            break
    if start is None:
        sys.stderr.write("module-init.py: no .cctor\n")
        sys.exit(1)
    end = start
    while end < len(lines) and lines[end].startswith("    "):
        end += 1
    body = lines[start:end]
    instrs = [(k, INSTR.match(l)) for k, l in enumerate(body)]
    instrs = [(k, m) for k, m in instrs if m]
    drop = set()
    for n, (op, arg) in enumerate(GUARD):
        if n >= len(instrs):
            sys.stderr.write("module-init.py: no guard: the body ends early\n")
            sys.exit(1)
        k, m = instrs[n]
        got_op, got_arg = norm(m.group(2)), m.group(3) or ""
        ok = got_op == op and (arg is None and not got_arg or
                               arg is not None and re.fullmatch(arg, got_arg))
        if not ok:
            sys.stderr.write("module-init.py: no guard: %s\n" % body[k].strip())
            sys.exit(1)
        drop.add(k)
    for n in range(len(instrs) - 2):
        (k0, m0), (k1, m1), (k2, m2) = instrs[n:n + 3]
        if (m0.group(2) == "ldstr" and MESSAGE.match(m0.group(3) or "") and
                m1.group(2) == "newobj" and
                "System.PlatformNotSupportedException::'.ctor'" in (m1.group(3) or "") and
                m2.group(2) == "throw"):
            drop.update((k0, k1, k2))
    kept = [l for k, l in enumerate(body) if k not in drop]
    sys.stdout.write("\n".join(lines[:start] + kept + lines[end:]) + "\n")


def bounds(path, section):
    start = stop = None
    for line in open(path, encoding="utf-8").read().splitlines():
        m = re.match(r"^field __(start|stop)_(\S+) = data\+(\d+)$", line)
        if m and m.group(2) == section:
            if m.group(1) == "start":
                start = int(m.group(3))
            else:
                stop = int(m.group(3))
    if start is None or stop is None:
        print("%s: no bounds" % section)
    else:
        print("%s: start=data+%d stop=data+%d size=%d" % (section, start, stop, stop - start))


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "strip-guard":
        strip_guard(args[1])
    elif len(args) == 3 and args[0] == "bounds":
        bounds(args[1], args[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
