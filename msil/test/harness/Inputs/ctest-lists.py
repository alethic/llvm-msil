#!/usr/bin/env python3
# ===-- ctest-lists.py - c-testsuite list checks for ctest-lists.test ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The configuration errors of msil/test/Programs/c-testsuite (plan 5.12.9;
M23), for ../ctest-lists.test. Run it with python -B: it imports
msil_ctest_format from the source tree, which must get no __pycache__.

    ctest-lists.py unit
        TestList's configuration errors (a diag entry whose item is not
        P3-A or P3-B, a diag entry without '# <item>', a header count that
        differs from the entries, a name listed twice, no '# rule:' line)
        and libc_free on a small tree in a temporary directory outside the
        repository: 'ok: <what>' or 'FAIL: <what>' per check, then
        'ctest-lists: <n> checks, <f> failed'; exit 1 on a failure.
    ctest-lists.py setup COPY CORPUS [CASE]
        Copy msil/test/Programs/c-testsuite, without libc-free.txt, to COPY
        (emptied first), and write the tree of the copy's lit.local.cfg
        (c-testsuite-<ref>) into CORPUS, a fake cache of make-fake-corpus.py:
        tests/single-exec/free1.c and free2.c call no C library function and
        have empty .c.expected files, calls.c calls printf. COPY/libc-free.txt
        lists free1 and free2 under a rule that matches 'printf('. CASE then
        breaks one thing: 'expected' (free2.c.expected is not empty),
        'absent' (the list also names ghost, which the tree lacks),
        'unlisted' (the tree also has free3.c, which the list lacks) or
        'twice' (the list names free1 twice); or CASE 'exec' writes, for the
        executing nested run (prog/Inputs/nested-ctest, whose stand-in
        compiler ../prog/Inputs/prog-cc.py reads the markers), free1.c (a
        program that exits 0), diag1.c (its compile fails with the message
        DIAG) and diag2.c (it builds) in place of free1.c and free2.c, and
        lists free1, then diag1 and diag2 as diag entries of DIAG (P3-B).
"""

import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 4))
CTEST = os.path.join(REPO, "msil", "test", "Programs", "c-testsuite")
RULE = r"\b(printf|malloc)\s*\("
DIAG = "error: variadic function definitions are not supported for MSIL yet"


def load_format():
    sys.dont_write_bytecode = True
    try:
        import lit  # noqa: F401
    except ImportError:
        sys.path.insert(0, os.path.join(REPO, "llvm", "utils", "lit"))
    sys.path.insert(0, CTEST)
    import msil_ctest_format

    return msil_ctest_format


def write(path, text, encoding="utf-8"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding=encoding, newline="\n") as f:
        f.write(text)


def list_text(entries, count=None, rule=RULE):
    head = []
    if rule is not None:
        head.append("# rule: " + rule)
    head.append("# count: %d" % (len(entries) if count is None else count))
    return "".join(x + "\n" for x in ["# a test list"] + head + entries)


def unit():
    fmt = load_format()
    results = []

    def check(cond, what):
        results.append(bool(cond))
        print("%s: %s" % ("ok" if cond else "FAIL", what))

    def error_of(text):
        path = os.path.join(tmp, "list.txt")
        write(path, text)
        try:
            fmt.TestList(path)
        except fmt.ListError as e:
            return str(e)
        return None

    tmp = tempfile.mkdtemp(prefix="msil-ctest-lists-")
    try:
        path = os.path.join(tmp, "ok.txt")
        write(path, list_text(["00001", "00140 diag error: variadic  # P3-B variadic definitions"]))
        lst = fmt.TestList(path)
        check(
            lst.names() == ["00001", "00140"]
            and lst.entries[1] == ("00140", "error: variadic", "P3-B variadic definitions")
            and lst.rule.search("printf (x)") and not lst.rule.search("printx(1)"),
            "a valid list: names, the diag entry and its item, the rule",
        )
        e = error_of(list_text(["00140 diag error: variadic  # P2-C later"]))
        check(e and "must name its Phase 3 item" in e and "P2-C later" in e,
              "a diag entry whose item is not P3-A or P3-B: %s" % e)
        e = error_of(list_text(["00140 diag error: variadic"]))
        check(e and "is neither '<name>' nor '<name> diag <text>  # <P3 item>'" in e,
              "a diag entry without '# <item>': %s" % e)
        e = error_of(list_text(["00001", "00002"], count=3))
        check(e and "the header says 3 tests, but 2 are listed" in e,
              "a header count that differs from the entries: %s" % e)
        e = error_of(list_text(["00001", "00002", "00001"]))
        check(e and "00001 is listed twice" in e, "a name listed twice: %s" % e)
        e = error_of(list_text(["00001"], rule=None))
        check(e and "no '# rule: <regex>' header line" in e, "no '# rule:' line: %s" % e)

        tree = os.path.join(tmp, "single-exec")
        write(os.path.join(tree, "a.c"), "int main(void) { return 0; }\n")
        write(os.path.join(tree, "b.c"), 'int main(void) { printf ("x"); return 0; }\n')
        write(os.path.join(tree, "c.c"), "/* printf is named, not called */ int main(void) { return 0; }\n")
        write(os.path.join(tree, "d.c"), "/* caf\xe9 */ int main(void) { return malloc(1) == 0; }\n", "latin-1")
        write(os.path.join(tree, "e.c"), "/* \xe9 */ int main(void) { return 0; }\n", "latin-1")
        write(os.path.join(tree, "f.txt"), "int main(void) { return 0; }\n")
        write(os.path.join(tree, "a.c.expected"), "")
        free = fmt.libc_free(lst.rule, tree)
        check(free == ["a", "c", "e"],
              "libc_free: the .c files that the rule matches nowhere, sorted, read as Latin-1: %s" % free)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ctest-lists: %d checks, %d failed" % (len(results), results.count(False)))
    return 0 if all(results) else 1


def tree_name(copy):
    with open(os.path.join(copy, "lit.local.cfg"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("_tree = "):
                return line.split("=", 1)[1].strip().strip("\"'")
    raise SystemExit("ctest-lists.py: no '_tree = ' line in %s" % os.path.join(copy, "lit.local.cfg"))


def setup(copy, corpus, case):
    if case not in (None, "expected", "absent", "unlisted", "twice", "exec"):
        raise SystemExit("ctest-lists.py: unknown case %r" % case)
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    os.makedirs(copy)
    for f in os.listdir(CTEST):
        p = os.path.join(CTEST, f)
        if os.path.isfile(p) and f != "libc-free.txt":
            shutil.copyfile(p, os.path.join(copy, f))
    tests = os.path.join(corpus, tree_name(copy), "tests", "single-exec")
    if os.path.isdir(tests):
        shutil.rmtree(tests)
    if case == "exec":
        sources = {
            "free1": "int main(void) { return 0; }\n",
            "diag1": "// prog-error: compile %s\n"
            "int f(int n, ...) { return n; }\n"
            "int main(void) { return f(0); }\n" % DIAG,
            "diag2": "int main(void) { return 0; }\n",
        }
    else:
        sources = {n: "int main(void) { return 0; }\n" for n in ("free1", "free2")}
    for name, text in sources.items():
        write(os.path.join(tests, name + ".c"), text)
        write(os.path.join(tests, name + ".c.expected"), "")
    write(os.path.join(tests, "calls.c"), 'int main(void) { printf("x"); return 0; }\n')
    write(os.path.join(tests, "calls.c.expected"), "x")
    names = ["free1", "free2"]
    if case == "exec":
        names = ["free1"] + [
            "%s diag %s  # P3-B variadic definitions" % (n, DIAG)
            for n in ("diag1", "diag2")
        ]
    elif case == "expected":
        write(os.path.join(tests, "free2.c.expected"), "free2 prints\n")
    elif case == "absent":
        names.append("ghost")
    elif case == "unlisted":
        write(os.path.join(tests, "free3.c"), "int main(void) { return 0; }\n")
        write(os.path.join(tests, "free3.c.expected"), "")
    elif case == "twice":
        names.append("free1")
    write(os.path.join(copy, "libc-free.txt"), list_text(names))
    print("ctest-lists: %s and %s (%s)" % (copy, tests, case or "consistent"))
    return 0


def main(argv):
    if argv == ["unit"]:
        return unit()
    if len(argv) in (3, 4) and argv[0] == "setup":
        return setup(argv[1], argv[2], argv[3] if len(argv) == 4 else None)
    sys.stderr.write("usage: ctest-lists.py unit | setup COPY CORPUS [CASE]\n")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
