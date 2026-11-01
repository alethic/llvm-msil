#!/usr/bin/env python3
# ===-- ctest-lists.py - c-testsuite list checks for ctest-lists.test ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The lists of msil/test/Programs/c-testsuite (plan 5.12.9, 5.17.14; M23,
M24), for ../ctest-lists.test. Run it with python -B: it imports
msil_ctest_format from the source tree, which must get no __pycache__.

    ctest-lists.py unit
        msil_ctest_format's list grammar, its ListError messages,
        split_tree, libc_free, expected_text and mode_of, on lists and a
        small tree in a temporary directory outside the repository:
        'ok: <what>' or 'FAIL: <what>' per check, then 'ctest-lists: <n>
        checks, <f> failed'; exit 1 on a failure.
    ctest-lists.py setup COPY CORPUS [CASE]
        Copy msil/test/Programs/c-testsuite without its lists to COPY
        (emptied first), and write the tree of the copy's lit.local.cfg
        (c-testsuite-<ref>) into CORPUS, a fake cache of make-fake-corpus.py:
        tests/single-exec/free1.c and free2.c call no C library function,
        calls.c calls printf, each with a .c.expected file. COPY/libc-free.txt
        lists free1 and free2 and COPY/libc.txt lists calls, both under the
        rule RULE. CASE then changes one thing:
          nolibc         no libc.txt (optional until M26)
          expected       free2.c.expected is missing
          libc-expected  calls.c.expected is missing
          absent         libc-free.txt also names ghost, which the tree lacks
          unlisted       the tree also has free3.c, which no list names
          libc-absent    libc.txt also names ghost2, which the tree lacks
          libc-unlisted  the tree also has calls2.c, which libc.txt lacks
          libc-free      libc.txt also names free1, which the rule does not
                         match
          rule           libc.txt's '# rule:' line differs from libc-free's
          twice          libc-free.txt names free1 twice
          grammar        libc.txt's entry of calls has an exclude word that
                         is not one of the two
          native         the tree also has natwin.c and natlin.c, which
                         libc-free.txt lists as native entries (see NATIVE)
          exec           the tree of the executing nested run (see EXEC)
"""

import os
import re
import shutil
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, *[os.pardir] * 4))
CTEST = os.path.join(REPO, "msil", "test", "Programs", "c-testsuite")
RULE = r"\b(printf|malloc)\s*\("
DIAG = "error: variadic function definitions need M26"
PLAIN = "int main(void) { return 0; }\n"

# The native case: name -> the triple of its 'native' entry in libc-free.txt.
# One entry per triple of the lists, so the nested run's triple makes one of
# them a native test, which needs msil-native-oracle, and the other a plain
# one; ../ctest-lists.test runs only the native one.
NATIVE = {"natwin": "windows-msvc", "natlin": "linux-gnu"}

# The executing case (prog/Inputs/nested-ctest, whose stand-in compiler
# ../prog/Inputs/prog-cc.py reads the prog-* markers): name -> (list,
# entry modifier or '', source, .c.expected).
EXEC = {
    # plain: the stand-in prints the .c.expected text (with and without a
    # final newline), so the expected file the format writes must follow
    # msil-run.py's transcript rule
    "free1": ("free", "", "// prog-stdout: free1 ok\n" + PLAIN, "free1 ok\n"),
    "nonl": ("free", "", "// prog-stdout-nonl: tail\n" + PLAIN, "tail"),
    "crlf": ("free", "", "// prog-stdout: crlf ok\n" + PLAIN, "crlf ok\r\n"),
    # plain, but the program prints nothing: FAIL with the diff
    "out1": ("free", "", PLAIN, "out1 differs\n"),
    # native on windows-msvc: the native transcript ([exit 0]) is the
    # expected file, not the .c.expected text, so the test passes
    "nat1": ("free", "native windows-msvc  # LP64 text", PLAIN, "LP64 only\n"),
    # native on linux-gnu: plain on windows-msvc, so the text fails
    "nat2": ("free", "native linux-gnu  # LP64 text", PLAIN, "LP64 only\n"),
    # diag (no triple): the compile fails with the text: PASS
    "diag1": (
        "free",
        "diag %s  # M26 variadic definitions" % DIAG,
        "// prog-error: compile %s\nint f(int n, ...) { return n; }\n"
        "int main(void) { return f(0); }\n" % DIAG,
        "",
    ),
    # diag (no triple), but it builds: FAIL
    "diag2": ("free", "diag %s  # M26 variadic definitions" % DIAG, PLAIN, ""),
    # diag on linux-gnu: a plain test on windows-msvc
    "diag3": (
        "free",
        "diag linux-gnu %s  # M26 variadic definitions" % DIAG,
        PLAIN,
        "",
    ),
    # exclude on windows-msvc: UNSUPPORTED with the reason
    "exc1": (
        "free",
        "exclude windows-msvc not-portable  # bit-field layout",
        PLAIN,
        "",
    ),
    # exclude on linux-gnu: a plain test on windows-msvc
    "exc2": ("free", "exclude linux-gnu long-double  # 80-bit long double", PLAIN, ""),
    # libc.txt (the rule matches printf(): hosted settings
    "lib1": (
        "libc",
        "",
        '// prog-stdout: lib1 ok\nint main(void) { printf("lib1 ok\\n"); return 0; }\n',
        "lib1 ok\n",
    ),
    "lib2": (
        "libc",
        "native windows-msvc  # LP64 text",
        'int main(void) { printf(""); return 0; }\n',
        "LP64 only\n",
    ),
}


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
    with open(path, "w", encoding=encoding, newline="") as f:
        f.write(text)


def list_text(entries, count=None, rule=RULE, extra_head=()):
    head = []
    if rule is not None:
        head.append("# rule: " + rule)
    if count != -1:
        head.append("# count: %d" % (len(entries) if count is None else count))
    head.extend(extra_head)
    return "".join(x + "\n" for x in ["# a test list"] + head + entries)


GRAMMAR = (
    "is not '<name>', '<name> native <triple>  # <reason>', '<name> diag "
    "[<triple>] <text>  # M<n> ...' or '<name> exclude <triple> "
    "long-double|not-portable  # <text>' (<triple>: linux-gnu or windows-msvc)"
)
NOT_A_TRIPLE = "which is not a triple of the lists (linux-gnu or windows-msvc)"


def unit():
    fmt = load_format()
    results = []

    def check(cond, what):
        results.append(bool(cond))
        print("%s: %s" % ("ok" if cond else "FAIL", what))

    tmp = tempfile.mkdtemp(prefix="msil-ctest-lists-")
    path = os.path.join(tmp, "list.txt")

    def error_of(text):
        write(path, text)
        try:
            fmt.TestList(path)
        except fmt.ListError as e:
            return str(e)
        return None

    def entry_error(entry, line=4):
        """The error of a list with one entry, which must carry the
        '<path>:<line>: ' prefix; returns the message after it."""
        e = error_of(list_text([entry]))
        prefix = "%s:%d: " % (path, line)
        if e is None or not e.startswith(prefix):
            return "no '%s' prefix: %r" % (prefix, e)
        return e[len(prefix) :]

    try:
        # A list with every entry kind.
        write(
            path,
            list_text(
                [
                    "00001",
                    "00002 native windows-msvc  # LP64 text",
                    "00003 diag error: x y  # M26 variadic definitions",
                    "00004 diag linux-gnu error: z  # M27 longjmp",
                    "00005 exclude linux-gnu long-double  # 80-bit long double",
                    "00006 exclude windows-msvc not-portable  # bit-fields",
                    "# a comment line",
                    "",
                ],
                count=6,
            ),
        )
        lst = fmt.TestList(path)
        E = fmt.Entry
        want = [
            E("00001", None, None, None, None),
            E("00002", "native", "windows-msvc", None, "LP64 text"),
            E("00003", "diag", None, "error: x y", "M26 variadic definitions"),
            E("00004", "diag", "linux-gnu", "error: z", "M27 longjmp"),
            E("00005", "exclude", "linux-gnu", "long-double", "80-bit long double"),
            E("00006", "exclude", "windows-msvc", "not-portable", "bit-fields"),
        ]
        check(
            E._fields == ("name", "kind", "triple", "text", "comment"),
            "Entry is (name, kind, triple, text, comment): %r" % (E._fields,),
        )
        check(
            lst.names() == ["00001", "00002", "00003", "00004", "00005", "00006"]
            and list(lst.entries) == want
            and lst.count == 6
            and lst.rule_text == RULE
            and lst.rule.search("printf (x)")
            and not lst.rule.search("printx(1)"),
            "a list with every entry kind: names, entries, count, rule: %r"
            % (list(lst.entries),),
        )

        # The grammar and its messages, each '<path>:<line>: '.
        for entry in ("00001 native", "00001 foo bar", "00001 exclude"):
            e = entry_error(entry)
            check(e == "%r %s" % (entry, GRAMMAR), "the grammar (%r): %s" % (entry, e))
        for name in ("a/b", "..", "-x"):
            e = entry_error(name)
            check(
                e == "bad test name %r" % name, "a bad test name (%r): %s" % (name, e)
            )
        e = entry_error("00003 diag windows-gnu error: x  # M26 a")
        check(
            e == "the diag entry of 00003 names 'windows-gnu', %s" % NOT_A_TRIPLE,
            "a diag entry's triple that is not one of the lists: %s" % e,
        )
        for text in (
            'error: "q"',
            "error: a\\b",
            "error: 100%",
            "error: $x",
            "error: `x`",
            "",
        ):
            e = entry_error("00003 diag %s  # M26 a" % text)
            check(
                e == 'the diag text of 00003 is empty or contains one of " \\ % $ `',
                "a diag text with a character that lit or the checker would change (%r): %s"
                % (text, e),
            )
        for comment, shown in (("  # P3-B variadic", "P3-B variadic"), ("", "")):
            e = entry_error("00003 diag error: x" + comment)
            check(
                e == "the diag entry of 00003 must name the milestone that implements "
                "its construct ('# M<n> ...', no phase numbers), not %r" % shown,
                "a diag entry that names no milestone (%r): %s" % (comment, e),
            )
        for kind in ("native", "exclude"):
            word = " long-double" if kind == "exclude" else ""
            e = entry_error("00002 %s linux-musl%s  # t" % (kind, word))
            check(
                e
                == "the %s entry of 00002 names 'linux-musl', %s"
                % (kind, NOT_A_TRIPLE),
                "a %s entry's triple that is not one of the lists: %s" % (kind, e),
            )
        e = entry_error("00005 exclude linux-gnu other  # t")
        check(
            e == "the exclude entry of 00005 names 'other'; the word is one of "
            "long-double, not-portable",
            "an exclude word outside the vocabulary: %s" % e,
        )
        for entry, kind, what in (
            ("00002 native windows-msvc", "native", "reason"),
            ("00002 native windows-msvc  #", "native", "reason"),
            ("00005 exclude linux-gnu long-double", "exclude", "text"),
        ):
            e = entry_error(entry)
            check(
                e
                == "the %s entry of %s needs a comment ('# <%s>')"
                % (kind, entry.split()[0], what),
                "a %s entry without a comment (%r): %s" % (kind, entry, e),
            )
        e = error_of(list_text(["00001", "00002", "00001"]))
        check(
            e == "%s:6: 00001 is listed twice" % path,
            "a name listed twice: %s" % e,
        )

        # The header lines.
        e = error_of(list_text(["00001", "00002"], count=3))
        check(
            e == "%s: the header says 3 tests, but 2 are listed" % path,
            "a header count that differs from the entries: %s" % e,
        )
        e = error_of(list_text(["00001"], rule=None))
        check(
            e == "%s: no '# rule: <regex>' header line" % path,
            "no '# rule:' line: %s" % e,
        )
        e = error_of(list_text(["00001"], count=-1))
        check(
            e == "%s: no '# count: <n>' header line" % path,
            "no '# count:' line: %s" % e,
        )
        e = error_of(list_text(["00001"], extra_head=["# rule: x"]))
        check(
            e is not None and e.endswith(": a second '# rule:' line"),
            "a second '# rule:' line: %s" % e,
        )

        # The partition of a tree by the rule.
        tree = os.path.join(tmp, "single-exec")
        write(os.path.join(tree, "a.c"), PLAIN)
        write(os.path.join(tree, "b.c"), 'int main(void) { printf ("x"); return 0; }\n')
        write(os.path.join(tree, "c.c"), "/* printf is named, not called */ " + PLAIN)
        write(
            os.path.join(tree, "d.c"),
            "/* caf\xe9 */ int main(void) { return malloc(1) == 0; }\n",
            "latin-1",
        )
        write(os.path.join(tree, "e.c"), "/* \xe9 */ " + PLAIN, "latin-1")
        write(os.path.join(tree, "f.txt"), PLAIN)
        write(os.path.join(tree, "a.c.expected"), "")
        split = fmt.split_tree(lst.rule, tree)
        check(
            tuple(split) == (["a", "c", "e"], ["b", "d"]),
            "split_tree: (the .c files the rule matches nowhere, the others), "
            "sorted, read as Latin-1: %r" % (split,),
        )
        free = fmt.libc_free(lst.rule, tree)
        check(free == ["a", "c", "e"], "libc_free: split_tree's first list: %r" % free)

        # The expected file of a plain entry.
        for data, text in (
            (b"", "[exit 0]\n"),
            (b"hello\n", "hello\n[exit 0]\n"),
            (b"x", "x\n[no newline at end of output]\n[exit 0]\n"),
            (b"a\r\r\nb", "a\nb\n[no newline at end of output]\n[exit 0]\n"),
            (b"a\r\n", "a\n[exit 0]\n"),
            (b"a\rb\n", "a\rb\n[exit 0]\n"),
        ):
            got = fmt.expected_text(data)
            check(got == text, "expected_text(%r): %r" % (data, got))

        # The mode of each entry kind on each triple.
        modes = {
            "msil-triple-windows": [
                "plain",
                "native",
                "plain",
                "diag",
                "plain",
                "exclude",
                "plain",
            ],
            "msil-triple-linux": [
                "plain",
                "plain",
                "native",
                "diag",
                "diag",
                "plain",
                "exclude",
            ],
        }
        entries = [
            E("a", None, None, None, None),
            E("b", "native", "windows-msvc", None, "r"),
            E("c", "native", "linux-gnu", None, "r"),
            E("d", "diag", None, "error: x", "M26"),
            E("e", "diag", "linux-gnu", "error: x", "M26"),
            E("f", "exclude", "windows-msvc", "not-portable", "t"),
            E("g", "exclude", "linux-gnu", "long-double", "t"),
        ]
        for feature, want_modes in sorted(modes.items()):
            cfg = types.SimpleNamespace(available_features={feature})
            got = [fmt.mode_of(e, cfg) for e in entries]
            check(got == want_modes, "mode_of with %s: %r" % (feature, got))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("ctest-lists: %d checks, %d failed" % (len(results), results.count(False)))
    return 0 if all(results) else 1


def tree_name(copy):
    with open(os.path.join(copy, "lit.local.cfg"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("_tree = "):
                return line.split("=", 1)[1].strip().strip("\"'")
    raise SystemExit(
        "ctest-lists.py: no '_tree = ' line in %s" % os.path.join(copy, "lit.local.cfg")
    )


CASES = (
    None,
    "nolibc",
    "expected",
    "libc-expected",
    "absent",
    "unlisted",
    "libc-absent",
    "libc-unlisted",
    "libc-free",
    "rule",
    "twice",
    "grammar",
    "native",
    "exec",
)


def setup(copy, corpus, case):
    if case not in CASES:
        raise SystemExit("ctest-lists.py: unknown case %r" % case)
    if os.path.isdir(copy):
        shutil.rmtree(copy)
    os.makedirs(copy)
    for f in os.listdir(CTEST):
        p = os.path.join(CTEST, f)
        if os.path.isfile(p) and f not in ("libc-free.txt", "libc.txt"):
            shutil.copyfile(p, os.path.join(copy, f))
    tests = os.path.join(corpus, tree_name(copy), "tests", "single-exec")
    if os.path.isdir(tests):
        shutil.rmtree(tests)
    os.makedirs(tests)
    if case == "exec":
        lists = {"free": [], "libc": []}
        for name, (which, modifier, source, expected) in EXEC.items():
            write(os.path.join(tests, name + ".c"), source)
            write(os.path.join(tests, name + ".c.expected"), expected)
            lists[which].append(name + (" " + modifier if modifier else ""))
        write(os.path.join(copy, "libc-free.txt"), list_text(lists["free"]))
        write(os.path.join(copy, "libc.txt"), list_text(lists["libc"]))
        print("ctest-lists: %s and %s (exec)" % (copy, tests))
        return 0
    sources = {
        "free1": PLAIN,
        "free2": PLAIN,
        "calls": 'int main(void) { printf("x"); return 0; }\n',
    }
    if case == "unlisted":
        sources["free3"] = PLAIN
    if case == "libc-unlisted":
        sources["calls2"] = 'int main(void) { printf("y"); return 0; }\n'
    if case == "native":
        sources.update((name, PLAIN) for name in NATIVE)
    for name, text in sources.items():
        write(os.path.join(tests, name + ".c"), text)
        write(
            os.path.join(tests, name + ".c.expected"), "x" if "printf" in text else ""
        )
    free, libc, libc_rule = ["free1", "free2"], ["calls"], RULE
    if case == "expected":
        os.remove(os.path.join(tests, "free2.c.expected"))
    elif case == "libc-expected":
        os.remove(os.path.join(tests, "calls.c.expected"))
    elif case == "absent":
        free.append("ghost")
    elif case == "libc-absent":
        libc.append("ghost2")
    elif case == "libc-free":
        libc.append("free1")
    elif case == "rule":
        libc_rule = r"\bprintf\s*\("
    elif case == "twice":
        free.append("free1")
    elif case == "grammar":
        libc = ["calls exclude windows-msvc other  # t"]
    elif case == "native":
        free += ["%s native %s  # LP64 text" % (n, t) for n, t in NATIVE.items()]
    write(os.path.join(copy, "libc-free.txt"), list_text(free))
    if case != "nolibc":
        write(os.path.join(copy, "libc.txt"), list_text(libc, rule=libc_rule))
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
