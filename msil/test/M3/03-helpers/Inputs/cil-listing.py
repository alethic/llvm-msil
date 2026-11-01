#!/usr/bin/env python3
# ===-- cil-listing.py - Query llvm-cil-dump's canonical listing ----------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Print parts of a canonical listing (llvm-cil-dump) for FileCheck.

    cil-listing.py members LISTING CLASS        each member of CLASS
    cil-listing.py methods LISTING CLASS NAME...  what each method NAME is
    cil-listing.py calls LISTING                every call target
    cil-listing.py --self-test

The listing has one '.class '<name>' ...' line per type, its members
indented by two blanks ('  .method '<name>' <signature> flags=...',
'  .field '<name>' ...') and their contents by four: a P/Invoke method's
'    .pinvokeimpl '<module>' entry '<entry>' flags=...', a body's
'    L<n>: <instruction>' lines.

members prints 'method <name>' or 'field <name>' for each member of CLASS
in listing order. methods prints one line per NAME, for the method of that
name in CLASS: '<name>: absent', '<name>: pinvoke '<module>' entry '<entry>'',
or '<name>: body', followed by ' calls <target>, <target>...' (each distinct
target of its call, callvirt, ldftn, ldvirtftn, jmp and newobj
instructions, in order of first use) when there is any. calls prints every
distinct call target of the listing, sorted. A target is printed as the
listing writes it: '<scope>::'<name>'', where the scope is a class (quoted
or not) with an optional '[<assembly>]' prefix: '<Module>'::'sqrt',
[lib]lib::'twice'. A CLASS that the listing does not have is an error
(exit 1); a usage error exits with 2.
"""

import re
import sys

CLASS_RE = re.compile(r"\.class '((?:[^'\\]|\\.)*)'")
MEMBER_RE = re.compile(r"  \.(method|field) '((?:[^'\\]|\\.)*)'")
PINVOKE_RE = re.compile(r"    \.pinvokeimpl ('(?:[^'\\]|\\.)*') entry ('(?:[^'\\]|\\.)*')")
CALL_RE = re.compile(r"    L\d+: (?:call|callvirt|ldftn|ldvirtftn|jmp|newobj) (.*)$")
TARGET_RE = re.compile(r"((?:\[[^\]]*\])?(?:'(?:[^'\\]|\\.)*'|[^\s'\[]+)::'(?:[^'\\]|\\.)*')\s*$")


def parse(text):
    """Return {class: [(kind, name, [content lines])]} in listing order."""
    classes, members = {}, None
    for line in text.splitlines():
        m = CLASS_RE.match(line)
        if m:
            members = classes.setdefault(m.group(1), [])
            continue
        if members is None:
            continue
        if line and not line.startswith(" "):
            members = None  # the end of the class (an 'assembly' line, ...)
            continue
        m = MEMBER_RE.match(line)
        if m:
            members.append((m.group(1), m.group(2), []))
        elif line.startswith("    ") and members:
            members[-1][2].append(line)
    return classes


def targets(lines):
    out = []
    for line in lines:
        m = CALL_RE.match(line)
        if not m:
            continue
        t = TARGET_RE.search(m.group(1))
        if t and t.group(1) not in out:
            out.append(t.group(1))
    return out


def describe(members, name):
    for kind, mname, lines in members:
        if kind != "method" or mname != name:
            continue
        for line in lines:
            m = PINVOKE_RE.match(line)
            if m:
                return "%s: pinvoke %s entry %s" % (name, m.group(1), m.group(2))
        calls = targets(lines)
        return "%s: body%s" % (name, " calls " + ", ".join(calls) if calls else "")
    return "%s: absent" % name


def run(argv, text, out, err):
    if len(argv) < 2:
        print("usage: cil-listing.py members|methods|calls LISTING ...", file=err)
        return 2
    mode = argv[0]
    classes = parse(text)
    if mode == "calls" and len(argv) == 2:
        found = set()
        for members in classes.values():
            for _, _, lines in members:
                found.update(targets(lines))
        for t in sorted(found):
            print(t, file=out)
        return 0
    if mode in ("members", "methods") and len(argv) >= 3:
        cls = argv[2]
        if cls not in classes:
            print("cil-listing.py: error: no class '%s' in %s" % (cls, argv[1]), file=err)
            return 1
        if mode == "members":
            if len(argv) != 3:
                print("usage: cil-listing.py members LISTING CLASS", file=err)
                return 2
            for kind, name, _ in classes[cls]:
                print("%s %s" % (kind, name), file=out)
            return 0
        if len(argv) == 3:
            print("usage: cil-listing.py methods LISTING CLASS NAME...", file=err)
            return 2
        for name in argv[3:]:
            print(describe(classes[cls], name), file=out)
        return 0
    print("usage: cil-listing.py members|methods|calls LISTING ...", file=err)
    return 2


SAMPLE = """\
pe PE32+ machine=0x8664 exe subsystem=3
module 'prog.exe'

.class '<Module>' flags=0x00000000
  .field 'x' native int flags=0x0036
  .method 'main' int32() flags=0x0016 impl=0x0000
    .maxstack 3
    L1: volatile.
    L3: call float64(float64) '<Module>'::'sqrtl'
    L5: call float64(float64) '<Module>'::'sqrtl'
    L7: call void(native int, float64) [lib]lib::'twice'
    L9: ldftn int32() '<Module>'::'main'
  .method 'sqrt' float64(float64) flags=0x2093 impl=0x0080
    .pinvokeimpl 'libm.so.6' entry 'sqrt' flags=0x0201
  .method 'sqrtl' float64(float64) flags=0x0016 impl=0x0000
    .maxstack 1
    L1: call float64(float64) '<Module>'::'sqrt'
  .method 'leaf' void() flags=0x0016 impl=0x0000
    .maxstack 1
    L0: localloc

.class 'lib' flags=0x00000181 extends [mscorlib]System.Object
  .method 'api' void() flags=0x0016 impl=0x0000
    .maxstack 1
    L0: call native int(native int) [mscorlib]System.Runtime.InteropServices.Marshal::'AllocHGlobal'
assembly .custom instance void(uint8[]) '<msil>SymbolTable'::'.ctor' = 01 00
"""


def self_test():
    from io import StringIO
    cases = [
        (["members", "L", "<Module>"], 0,
         "field x\nmethod main\nmethod sqrt\nmethod sqrtl\nmethod leaf\n"),
        (["members", "L", "lib"], 0, "method api\n"),
        (["methods", "L", "<Module>", "sqrtl", "sqrt", "main", "leaf", "gone"], 0,
         "sqrtl: body calls '<Module>'::'sqrt'\n"
         "sqrt: pinvoke 'libm.so.6' entry 'sqrt'\n"
         "main: body calls '<Module>'::'sqrtl', [lib]lib::'twice', '<Module>'::'main'\n"
         "leaf: body\n"
         "gone: absent\n"),
        (["calls", "L"], 0,
         "'<Module>'::'main'\n'<Module>'::'sqrt'\n'<Module>'::'sqrtl'\n"
         "[lib]lib::'twice'\n"
         "[mscorlib]System.Runtime.InteropServices.Marshal::'AllocHGlobal'\n"),
        (["members", "L", "nope"], 1, "cil-listing.py: error: no class 'nope' in L\n"),
        (["methods", "L", "<Module>"], 2, None),
        (["bogus", "L"], 2, None),
    ]
    failures = 0
    for args, status, want in cases:
        out, err = StringIO(), StringIO()
        got = run(args, SAMPLE, out, err)
        text = out.getvalue() + err.getvalue()
        ok = got == status and (want is None or text == want)
        print("self-test: %s: %s" % ("ok" if ok else "FAIL", " ".join(args)))
        if not ok:
            failures += 1
            print(text, end="")
    print("self-test: %s" % ("FAILED" if failures else "passed"))
    return 1 if failures else 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) < 3:
        return run(argv[1:], "", sys.stdout, sys.stderr)
    try:
        with open(argv[2], encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        print("cil-listing.py: error: cannot read %s: %s" % (argv[2], e.strerror),
              file=sys.stderr)
        return 1
    return run(argv[1:], text, sys.stdout, sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
