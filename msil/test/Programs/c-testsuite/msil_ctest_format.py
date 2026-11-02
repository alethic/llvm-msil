# ===-- msil_ctest_format.py - lit format of the c-testsuite tests --------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The lit test format of msil/test/Programs/c-testsuite (plan 5.12.9,
5.17.14).

Two lists name the tests of the cached tree's tests/single-exec:
libc-free.txt the tests that call no C library function, and libc.txt
(required from M26) the others. Both have the header lines
'# rule: <regex>' (the same regular expression in both) and
'# count: <n>', other '#' comment lines, and one entry per test, read with
one grammar (TestList):

    <name>
    <name> native <triple>  # <reason>
    <name> diag [<triple>] <text>  # M<n> <what>
    <name> exclude <triple> long-double|not-portable  # <text>

<triple> is linux-gnu or windows-msvc (the leg's triple, from the features
msil-triple-linux and msil-triple-windows); an entry's modifier applies on
that triple only, and on the other the test is a plain one. A diag entry
without a triple applies on every triple. A comment starts at the first
'#' after a blank, so a diag text holds no ' #'. A native or exclude entry
needs its comment; a diag entry's comment names the milestone that
implements its construct ('# M26 ...'), never a phase. The sources stay in
the corpus cache (%{msil_corpus}, filled by msil/utils/corpus/fetch.py).

The format yields one lit test per entry: 'MSIL :: Programs/c-testsuite/<name>'
for libc-free.txt and 'MSIL :: Programs/c-testsuite/libc/<name>' for
libc.txt, a CTestSuiteTest whose source path is ctest.template, so
lit.TestRunner.executeShTest runs that file's REQUIRES:, REDEFINE: and RUN:
lines for every test, with these substitutions:

    %{msil_ctest_name}    the name
    %{msil_ctest_tree}    the cached tree (c-testsuite-<ref>)
    %{msil_ctest_cflags}  libc-free.txt: -std=gnu17 -w; libc.txt:
                          -std=gnu17 -w %{msil_hosted_cflags}
    %{msil_ctest_run}     the commands below

The template builds %{msil_corpus}/<tree>/tests/single-exec/<name>.c with
%{msil_cflags} redefined to %{msil_ctest_cflags} (the recon's flags, without
the harness's -ffreestanding: several tests rely on main's implicit
'return 0'), and its expected file is %t/<name>.expected (%{msil_expect} is
%t/<name>). By the entry, on the leg's triple:

    plain    the format writes %t/<name>.expected before the test runs:
             <tree>/tests/single-exec/<name>.c.expected under msil-run.py's
             transcript rule (CR runs before LF folded, UTF-8 with
             backslashreplace, '[no newline at end of output]' when the
             output ends without one), then '[exit 0]'. %{msil_ctest_run}
             is %{msil_prog}: through the leg's pipeline at each of its
             levels, verified, force-JITted, run and compared, and the
             native oracle (the hosted one for libc.txt, whose
             %{msil_oracle_flags} is --hosted) compared with the same file.
    native   the native oracle's transcript is the expected file:
             %{msil_ctest_run} builds the native program at the leg's first
             level (msil-prog.py --mode native, with %{msil_oracle_flags}),
             runs it with msil-run.py --native --out %t/<name>.expected, then
             runs %{msil_prog}. The test REQUIRES msil-native-oracle
             (UNSUPPORTED without it; lit.cfg.py stops under
             msil_native_oracle=required when the oracle cannot run).
    diag     a named diagnostic instead of a program: %{msil_ctest_run} is,
             for each level <L> of the leg, joined with '&&',

                 %{msil_prog_fail_<L>} 2>&1 | FileCheck %S/diag.check
                     "-DDIAG=<text>" --implicit-check-not="LLVM ERROR"
                     --implicit-check-not="PLEASE submit"

             diag.check's only check is the text. A diag entry names the
             diagnostic of the object pipeline, the one c-testsuite runs
             through, so it also REQUIRES msil-prog-pipeline=obj (an LTO
             link may delete the construct first, as it deletes 00140.c's
             unused variadic definition at -O2).
    exclude  UNSUPPORTED with 'excluded on <triple>: <word> (<text>)' once
             the template's REQUIRES hold; never run, never XFAIL.

libc.txt's tests run under the configuration that lit.local.cfg makes with
lit.cfg.py's config.msil_enable_hosted (plan 5.17.3): UNSUPPORTED without
msil-hosted (fatal under msil_require_hosted=1) and %{msil_oracle_flags}
--hosted; their %{msil_cflags} is %{msil_ctest_cflags}.
"""

import collections
import os
import re
import shutil

import lit.formats
import lit.Test
import lit.TestRunner

NAME_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
# The triples of the lists, with the lit feature of each.
TRIPLE_FEATURES = {
    "linux-gnu": "msil-triple-linux",
    "windows-msvc": "msil-triple-windows",
}
# The closed vocabulary of an exclude entry (plan 5.17.14).
EXCLUDE_WORDS = ("long-double", "not-portable")
# Diagnostic texts are passed to FileCheck inside double quotes on a lit RUN
# line, so they may not contain these.
DIAG_FORBIDDEN = ('"', "\\", "%", "$", "`")
# A diag entry's comment names the milestone that implements its construct
# (plan 5.17.14, BL14: no phase numbers).
MILESTONE_RE = re.compile(r"^M[0-9]+\b")
PHASE_RE = re.compile(r"\bP[0-9](?:-[A-Z])?\b|\bphase\s*[0-9]", re.IGNORECASE)
# A token after 'diag' that looks like a triple but is not one of the lists'.
TRIPLE_LIKE_RE = re.compile(r"^(?:(?:linux|windows)(?:-\S*)?|(?:x86_64|aarch64)-\S+)$")
# An entry line: the entry, then the comment, which starts at the first '#'
# after a blank.
COMMENT_RE = re.compile(r"^(.*?)(?:\s+#(.*))?$")
GRAMMAR = (
    "'<name>', '<name> native <triple>  # <reason>', "
    "'<name> diag [<triple>] <text>  # M<n> ...' or "
    "'<name> exclude <triple> long-double|not-portable  # <text>' "
    "(<triple>: linux-gnu or windows-msvc)"
)
# The transcript newline rule of msil-run.py (fold_newlines).
NEWLINE_RE = re.compile(r"\r+\n")

# One entry of a list. kind: None (a plain entry), 'native', 'diag' or
# 'exclude'; triple: None (every triple; a plain entry or a triple-less diag
# entry), 'linux-gnu' or 'windows-msvc'; text: the diag text or the exclude
# word, else None; comment: the text after '#', else None.
Entry = collections.namedtuple("Entry", "name kind triple text comment")


class ListError(ValueError):
    pass


def parse_entry(line, where):
    """The Entry of one entry line (stripped), or ListError."""
    m = COMMENT_RE.match(line)
    body, comment = m.group(1).strip(), m.group(2)
    if comment is not None:
        comment = comment.strip() or None
    words = body.split(None, 2)
    if not words:
        raise ListError("%s: %r is not %s" % (where, line, GRAMMAR))
    name = words[0]
    if not NAME_RE.match(name):
        raise ListError("%s: bad test name %r" % (where, name))
    if len(words) == 1:
        return Entry(name, None, None, None, comment)
    kind = words[1]
    args = words[2] if len(words) > 2 else ""
    if kind == "diag":
        first = args.split(None, 1)
        triple, text = None, args
        if first and first[0] in TRIPLE_FEATURES:
            triple = first[0]
            text = first[1] if len(first) > 1 else ""
        elif first and TRIPLE_LIKE_RE.match(first[0]):
            raise ListError(
                "%s: the diag entry of %s names %r, which is not a triple of the "
                "lists (linux-gnu or windows-msvc)" % (where, name, first[0])
            )
        text = text.strip()
        if not text or any(c in text for c in DIAG_FORBIDDEN):
            raise ListError(
                "%s: the diag text of %s is empty or contains one of %s"
                % (where, name, " ".join(DIAG_FORBIDDEN))
            )
        if (
            comment is None
            or not MILESTONE_RE.match(comment)
            or PHASE_RE.search(comment)
        ):
            raise ListError(
                "%s: the diag entry of %s must name the milestone that implements "
                "its construct ('# M<n> ...', no phase numbers), not %r"
                % (where, name, comment or "")
            )
        return Entry(name, "diag", triple, text, comment)
    if kind in ("native", "exclude"):
        parts = args.split()
        want = 1 if kind == "native" else 2
        if len(parts) != want:
            raise ListError("%s: %r is not %s" % (where, line, GRAMMAR))
        triple = parts[0]
        if triple not in TRIPLE_FEATURES:
            raise ListError(
                "%s: the %s entry of %s names %r, which is not a triple of the "
                "lists (linux-gnu or windows-msvc)" % (where, kind, name, triple)
            )
        word = None
        if kind == "exclude":
            word = parts[1]
            if word not in EXCLUDE_WORDS:
                raise ListError(
                    "%s: the exclude entry of %s names %r; the word is one of %s"
                    % (where, name, word, ", ".join(EXCLUDE_WORDS))
                )
        if comment is None:
            raise ListError(
                "%s: the %s entry of %s needs a comment ('# <%s>')"
                % (where, kind, name, "reason" if kind == "native" else "text")
            )
        return Entry(name, kind, triple, word, comment)
    raise ListError("%s: %r is not %s" % (where, line, GRAMMAR))


class TestList:
    """A list of the c-testsuite tests (libc-free.txt or libc.txt): header
    lines '# rule: <regex>' and '# count: <n>', other '#' comment lines,
    then one entry per line (see the module docstring)."""

    def __init__(self, path):
        self.path = path
        self.rule = None
        self.rule_text = None
        self.count = None
        self.entries = []  # Entry tuples, in the list's order
        seen = set()
        with open(path, encoding="utf-8") as f:
            for number, line in enumerate(f, 1):
                where = "%s:%d" % (path, number)
                stripped = line.rstrip("\r\n").strip()
                if not stripped:
                    continue
                if stripped.startswith("#"):
                    body = stripped[1:].strip()
                    if body.startswith("rule:"):
                        if self.rule is not None:
                            raise ListError("%s: a second '# rule:' line" % where)
                        self.rule_text = body[len("rule:") :].strip()
                        try:
                            self.rule = re.compile(self.rule_text)
                        except re.error as e:
                            raise ListError("%s: bad rule: %s" % (where, e))
                    elif body.startswith("count:"):
                        if self.count is not None:
                            raise ListError("%s: a second '# count:' line" % where)
                        try:
                            self.count = int(body[len("count:") :].strip())
                        except ValueError:
                            raise ListError("%s: '# count:' needs a number" % where)
                    continue
                entry = parse_entry(stripped, where)
                if entry.name in seen:
                    raise ListError("%s: %s is listed twice" % (where, entry.name))
                seen.add(entry.name)
                self.entries.append(entry)
        if self.rule is None:
            raise ListError("%s: no '# rule: <regex>' header line" % path)
        if self.count is None:
            raise ListError("%s: no '# count: <n>' header line" % path)
        if self.count != len(self.entries):
            raise ListError(
                "%s: the header says %d tests, but %d are listed"
                % (path, self.count, len(self.entries))
            )

    def names(self):
        return [e.name for e in self.entries]


def split_tree(rule, tests_dir):
    """(libc-free, matched): the names of the tests of tests_dir whose source
    the rule matches nowhere, and of the others, each sorted (sources read
    as Latin-1, as the recon's ctest.py reads them)."""
    free, matched = [], []
    for f in sorted(os.listdir(tests_dir)):
        if not f.endswith(".c"):
            continue
        with open(os.path.join(tests_dir, f), encoding="latin-1") as src:
            (matched if rule.search(src.read()) else free).append(f[: -len(".c")])
    return free, matched


def libc_free(rule, tests_dir):
    """The names of the tests of tests_dir whose source does not match rule."""
    return split_tree(rule, tests_dir)[0]


def expected_text(data):
    """The expected file of a plain entry: the bytes of <name>.c.expected
    under msil-run.py's transcript rule (make_transcript), with '[exit 0]'."""
    text = NEWLINE_RE.sub("\n", data.decode("utf-8", errors="backslashreplace"))
    if text and not text.endswith("\n"):
        text += "\n[no newline at end of output]\n"
    return NEWLINE_RE.sub("\n", text + "[exit 0]\n")


# The script of every test (see the module docstring).
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ctest.template")


class CTestSuiteTest(lit.Test.Test):
    """A test of a list: lit reads its script from template, since the test
    has no file of its own. entry: its Entry; mode: what it does on the
    leg's triple ('plain', 'native', 'diag' or 'exclude'); cflags: its
    %{msil_ctest_cflags}."""

    def __init__(self, suite, path_in_suite, config, template, entry, mode, cflags):
        super().__init__(suite, path_in_suite, config)
        self.template = template
        self.entry = entry
        self.mode = mode
        self.cflags = cflags

    def getSourcePath(self):
        return self.template


def mode_of(entry, config):
    """What the entry does under config: its kind on its triple (every
    triple when it names none), else 'plain'."""
    if entry.kind is None:
        return "plain"
    if entry.triple is not None:
        if TRIPLE_FEATURES[entry.triple] not in config.available_features:
            return "plain"
    return entry.kind


class CTestSuiteFormat(lit.formats.ShTest):
    """One lit test per entry of libc-free.txt and of libc.txt (see the
    module docstring). tests_dir: the cached tree's tests/single-exec, or
    None without the corpus; native_ext: the native program's extension
    (elf for the E7 oracles of msil_runner=wsl, else exe)."""

    def __init__(self, entries, tree, levels, cflags, tests_dir=None, native_ext="exe"):
        super().__init__()
        self.tree = tree
        self.levels = tuple(levels)
        self.tests_dir = tests_dir
        self.native_ext = native_ext
        # (path components below the directory, entries, cflags, config or
        # None for the directory's own)
        self.lists = [((), tuple(entries), cflags, None)]

    def add_libc(self, entries, cflags, config):
        """libc.txt's entries, as tests below 'libc/' under config."""
        self.lists.append((("libc",), tuple(entries), cflags, config))

    def getTestsForPath(self, testSuite, path_in_suite, litConfig, localConfig):
        # The files of the directory are not tests.
        return iter(())

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        for sub, entries, cflags, config in self.lists:
            config = config if config is not None else localConfig
            for entry in entries:
                mode = mode_of(entry, config)
                test = CTestSuiteTest(
                    testSuite,
                    path_in_suite + sub + (entry.name,),
                    config,
                    TEMPLATE,
                    entry,
                    mode,
                    cflags,
                )
                if mode == "diag":
                    test.requires.append("msil-prog-pipeline=obj")
                elif mode == "native":
                    test.requires.append("msil-native-oracle")
                yield test

    def run_command(self, test):
        """%{msil_ctest_run} of test."""
        entry = test.entry
        if test.mode == "diag":
            return " && ".join(
                '%%{msil_prog_fail_%s} 2>&1 | FileCheck %%S/diag.check "-DDIAG=%s"'
                ' --implicit-check-not="LLVM ERROR"'
                ' --implicit-check-not="PLEASE submit"' % (level, entry.text)
                for level in self.levels
            )
        if test.mode == "native":
            oracle = ""
            if any(k == "%{msil_oracle_flags}" for k, _ in test.config.substitutions):
                oracle = " %{msil_oracle_flags}"
            work = "%t.expected.d"
            prog = "%s/prog.%s" % (work, self.native_ext)
            return (
                "%%msil_prog_build --mode native%s --level %s --workdir %s --out %s"
                " --src %%{msil_srcs} --stock-src %%{msil_stock_srcs}"
                " --archives='%%{msil_archives}' --native-lib='%%{msil_native_libs}'"
                " && %%msil_exec --native --out %%t/%%{msil_ctest_name}.expected %s"
                " && %%{msil_prog}" % (oracle, self.levels[0], work, prog, prog)
            )
        return "%{msil_prog}"

    def prepare(self, test, litConfig):
        """Before the run of a plain or native entry: an empty %t, and a plain
        entry's expected file in it. A lit Result when the test does not
        run or its expected file cannot be written, else None."""
        parsed = lit.TestRunner.parseIntegratedTestScript(test)
        if isinstance(parsed, lit.Test.Result):
            return parsed
        if litConfig.noExecute:
            return None
        tmp = lit.TestRunner.getTempPaths(test)[1] + ".tmp"
        shutil.rmtree(tmp, ignore_errors=True)
        try:
            os.makedirs(tmp, exist_ok=True)
        except OSError as e:
            return lit.Test.Result(
                lit.Test.FAIL, "msil: cannot create %s: %s" % (tmp, e)
            )
        if test.mode != "plain":
            return None
        name = test.entry.name
        if self.tests_dir is None:
            return lit.Test.Result(
                lit.Test.FAIL, "msil: no cached tree for %s.c.expected" % name
            )
        src = os.path.join(self.tests_dir, name + ".c.expected")
        dst = os.path.join(tmp, name + ".expected")
        try:
            with open(src, "rb") as f:
                data = f.read()
            with open(dst, "w", encoding="utf-8", newline="\n") as f:
                f.write(expected_text(data))
        except OSError as e:
            return lit.Test.Result(
                lit.Test.FAIL, "msil: cannot write %s from %s: %s" % (dst, src, e)
            )
        return None

    def execute(self, test, litConfig):
        if test.config.unsupported:
            return lit.Test.Result(lit.Test.UNSUPPORTED, "Test is unsupported")
        entry = test.entry
        if test.mode == "exclude":
            parsed = lit.TestRunner.parseIntegratedTestScript(test)
            if isinstance(parsed, lit.Test.Result):
                return parsed
            return lit.Test.Result(
                lit.Test.UNSUPPORTED,
                "excluded on %s: %s (%s)" % (entry.triple, entry.text, entry.comment),
            )
        if test.mode in ("plain", "native"):
            early = self.prepare(test, litConfig)
            if early is not None:
                return early
        return lit.TestRunner.executeShTest(
            test,
            litConfig,
            self.execute_external,
            extra_substitutions=[
                ("%{msil_ctest_name}", entry.name),
                ("%{msil_ctest_tree}", self.tree),
                ("%{msil_ctest_cflags}", test.cflags),
                ("%{msil_ctest_run}", self.run_command(test)),
            ],
            preamble_commands=self.preamble_commands,
        )
