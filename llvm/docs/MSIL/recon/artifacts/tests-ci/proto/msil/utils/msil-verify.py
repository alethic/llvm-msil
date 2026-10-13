#!/usr/bin/env python3
"""Check an assembly with ILVerify (and optionally PEVerify) for stack-shape errors.

Design.md section 12: "ilverify reports no stack-shape errors (unverifiable
pointer use is expected)". The output of the backend is unverifiable by design,
so this wrapper fails only on error codes that are NOT in the allow list.

    msil-verify.py --ilverify PATH --runtime-dir DIR --allow FILE prog.dll
    msil-verify.py --skip prog.dll            (no verifier available: no-op)

ILVerify 10.0.12 notes (measured):
  * Error lines look like "[IL]: Error [Code]: [file : Type::Method(...)][offset 0x..] text".
  * A method containing `calli` yields "Error []: [...] ImportCalli not implemented"
    and the rest of that method is not verified. Reported here as a note, not a failure.
  * It does NOT flag `native int` + `int64` arithmetic; PEVerify does
    ("Unexpected type on the stack"). Pass --peverify on Windows to cover that.
PEVerify reports only the first error per method.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

IL_ERROR_RE = re.compile(r"^\[IL\]: Error \[(?P<code>\w*)\]: \[(?P<where>[^\]]*)\](?P<rest>.*)$")
PEVERIFY_ERROR_RE = re.compile(r"^\[IL\]: Error: \[(?P<where>[^\]]*)\](?P<rest>.*)$")

# PEVerify has no codes; these message fragments are the unverifiable-by-design ones.
PEVERIFY_ALLOWED = (
    "Instruction cannot be verified.",
    "Expected ByRef on the stack.",
    "initlocals must be set",
    "Unmanaged pointers are not a verifiable type.",
)


def load_allow(path):
    pats = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                pats.append(re.compile(line))
    return pats


def short(where):
    # "C:\...\x.dll : <Module>::f(int32)" -> "<Module>::f(int32)"
    return where.split(" : ", 1)[-1]


def run_ilverify(args, allow):
    cmd = [args.ilverify, args.assembly]
    for d in args.runtime_dir:
        cmd += ["-r", os.path.join(d, "*.dll")]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    text = proc.stdout.decode("utf-8", errors="backslashreplace")
    bad, notes = [], []
    for line in text.splitlines():
        m = IL_ERROR_RE.match(line.strip())
        if not m:
            continue
        code, where, rest = m.group("code"), short(m.group("where")), m.group("rest").strip()
        if code == "" and "ImportCalli not implemented" in rest:
            notes.append("ilverify: %s contains calli; rest of method not verified" % where)
        elif any(p.fullmatch(code) for p in allow):
            continue
        else:
            bad.append("ilverify: [%s] %s %s" % (code or "?", where, rest))
    if proc.returncode not in (0, 2) and not bad:
        bad.append("ilverify: exited with %d:\n%s" % (proc.returncode, text))
    return bad, notes


def run_peverify(args):
    proc = subprocess.run(
        [args.peverify, "/nologo", "/il", args.assembly],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
    )
    text = proc.stdout.decode("utf-8", errors="backslashreplace")
    bad = []
    for line in text.splitlines():
        m = PEVERIFY_ERROR_RE.match(line.strip())
        if m and not any(a in m.group("rest") for a in PEVERIFY_ALLOWED):
            bad.append("peverify: %s %s" % (short(m.group("where")), m.group("rest").strip()))
    return bad


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--ilverify")
    p.add_argument("--runtime-dir", action="append", default=[],
                   help="directory of reference assemblies (repeatable)")
    p.add_argument("--allow", help="file of allowed ILVerify error-code regexes")
    p.add_argument("--peverify", help="also run PEVerify (Windows)")
    p.add_argument("--skip", action="store_true", help="no verifier available; succeed")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("assembly")
    args = p.parse_args()

    if args.skip:
        print("msil-verify: skipped (no ilverify configured)")
        return 0
    allow = load_allow(args.allow) if args.allow else []
    bad, notes = run_ilverify(args, allow)
    if args.peverify:
        bad += run_peverify(args)
    for n in notes:
        print(n)
    for b in bad:
        print(b, file=sys.stderr)
    if bad:
        print("msil-verify: %d disallowed verifier error(s) in %s"
              % (len(bad), os.path.basename(args.assembly)), file=sys.stderr)
        return 1
    if args.verbose:
        print("msil-verify: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
