# ===-- msil_ctest_format.py - lit format of the c-testsuite tests --------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The lit test format of msil/test/Programs/c-testsuite (plan 5.12.9, M23).

libc-free.txt lists the c-testsuite tests that call no C library function;
the sources stay in the corpus cache (%{msil_corpus}, filled by
msil/utils/corpus/fetch.py). This format yields one lit test per listed name
('MSIL :: Programs/c-testsuite/<name>'), a CTestSuiteTest whose source path
is ctest.template, so lit.TestRunner.executeShTest runs that file's
REQUIRES:, REDEFINE: and RUN: lines for every test, with these substitutions:

    %{msil_ctest_name}    the name
    %{msil_ctest_tree}    the cached tree (c-testsuite-<ref>)
    %{msil_ctest_cflags}  -std=gnu17 -w
    %{msil_ctest_run}     %{msil_prog}

The test is then built from %{msil_corpus}/<tree>/tests/single-exec/<name>.c
through the leg's pipeline at each of its levels, verified, force-JITted and
run against exit0.expected ('[exit 0]': every listed test has an empty
<name>.c.expected), with the native oracle building the same source.
%{msil_cflags} is the recon's flags, without the harness's -ffreestanding:
several tests rely on main's implicit 'return 0'. A name listed as
'<name> diag <text>' is a test that Phase 2 stops with a named diagnostic of
a Phase 3 feature instead; its %{msil_ctest_run} is, for each level <L> of
the leg, joined with '&&':

    %{msil_prog_fail_<L>} 2>&1 | FileCheck %S/diag.check "-DDIAG=<text>"
        --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"

diag.check's only check is the text. A diag entry names the diagnostic of
the object pipeline, the one c-testsuite runs through, so it also REQUIRES
msil-prog-pipeline=obj (an LTO link may delete the construct first, as it
deletes 00140.c's unused variadic definition at -O2).
"""

import os
import re

import lit.formats
import lit.Test
import lit.TestRunner

NAME_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
# '<name>' or '<name> diag <text>  # <P3 item>'.
ENTRY_RE = re.compile(r"^(\S+)(?:\s+diag\s+(.*?)\s+#\s*(.*?))?\s*$")
# Diagnostic texts are passed to FileCheck inside double quotes on a lit RUN
# line, so they may not contain these.
DIAG_FORBIDDEN = ('"', "\\", "%", "$", "`")
# The Phase 3 work that a diag entry waits for (plan 5.12.9: a variadic
# definition, P3-B; dynamic alloca, P3-A).
P3_ITEM_RE = re.compile(r"^P3-[AB]\b")


class ListError(ValueError):
    pass


class TestList:
    """libc-free.txt: header lines '# rule: <regex>' and '# count: <n>',
    other '#' comment lines, then one entry per line."""

    def __init__(self, path):
        self.path = path
        self.rule = None
        self.count = None
        self.entries = []  # (name, diag text or None, P3 item or None)
        seen = set()
        with open(path, encoding="utf-8") as f:
            for number, line in enumerate(f, 1):
                where = "%s:%d" % (path, number)
                text = line.rstrip("\r\n")
                stripped = text.strip()
                if not stripped:
                    continue
                if stripped.startswith("#"):
                    body = stripped[1:].strip()
                    if body.startswith("rule:"):
                        if self.rule is not None:
                            raise ListError("%s: a second '# rule:' line" % where)
                        try:
                            self.rule = re.compile(body[len("rule:") :].strip())
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
                m = ENTRY_RE.match(stripped)
                if not m:
                    raise ListError(
                        "%s: %r is neither '<name>' nor "
                        "'<name> diag <text>  # <P3 item>'" % (where, stripped)
                    )
                name, diag, item = m.group(1), m.group(2), m.group(3)
                if not NAME_RE.match(name):
                    raise ListError("%s: bad test name %r" % (where, name))
                if name in seen:
                    raise ListError("%s: %s is listed twice" % (where, name))
                seen.add(name)
                if diag is not None:
                    if not diag or any(c in diag for c in DIAG_FORBIDDEN):
                        raise ListError(
                            "%s: the diag text of %s is empty or contains one of %s"
                            % (where, name, " ".join(DIAG_FORBIDDEN))
                        )
                    if not P3_ITEM_RE.match(item):
                        raise ListError(
                            "%s: the diag entry of %s must name its Phase 3 item "
                            "('# P3-A ...' or '# P3-B ...'), not %r" % (where, name, item)
                        )
                self.entries.append((name, diag, item))
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
        return [name for name, _, _ in self.entries]


def libc_free(rule, tests_dir):
    """The names of the tests of tests_dir whose source does not match rule
    (read as Latin-1, as the recon's ctest.py reads them)."""
    names = []
    for f in sorted(os.listdir(tests_dir)):
        if not f.endswith(".c"):
            continue
        with open(os.path.join(tests_dir, f), encoding="latin-1") as src:
            if not rule.search(src.read()):
                names.append(f[: -len(".c")])
    return names


# The script of every test (see the module docstring).
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ctest.template")


class CTestSuiteTest(lit.Test.Test):
    """A test of libc-free.txt: lit reads its script from template, since the
    test has no file of its own."""

    def __init__(self, suite, path_in_suite, config, template):
        super().__init__(suite, path_in_suite, config)
        self.template = template

    def getSourcePath(self):
        return self.template


class CTestSuiteFormat(lit.formats.ShTest):
    """One lit test per entry of libc-free.txt (see the module docstring)."""

    def __init__(self, entries, tree, levels, cflags):
        super().__init__()
        self.entries = tuple(entries)
        self.diags = {name: diag for name, diag, _ in entries}
        self.tree = tree
        self.levels = tuple(levels)
        self.cflags = cflags

    def getTestsForPath(self, testSuite, path_in_suite, litConfig, localConfig):
        # The files of the directory are not tests.
        return iter(())

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        for name, diag, _ in self.entries:
            test = CTestSuiteTest(
                testSuite, path_in_suite + (name,), localConfig, template=TEMPLATE
            )
            if diag is not None:
                test.requires.append("msil-prog-pipeline=obj")
            yield test

    def run_command(self, name):
        """%{msil_ctest_run} of test name."""
        diag = self.diags[name]
        if diag is None:
            return "%{msil_prog}"
        return " && ".join(
            '%%{msil_prog_fail_%s} 2>&1 | FileCheck %%S/diag.check "-DDIAG=%s"'
            ' --implicit-check-not="LLVM ERROR"'
            ' --implicit-check-not="PLEASE submit"' % (level, diag)
            for level in self.levels
        )

    def execute(self, test, litConfig):
        name = test.path_in_suite[-1]
        return lit.TestRunner.executeShTest(
            test,
            litConfig,
            self.execute_external,
            extra_substitutions=[
                ("%{msil_ctest_name}", name),
                ("%{msil_ctest_tree}", self.tree),
                ("%{msil_ctest_cflags}", self.cflags),
                ("%{msil_ctest_run}", self.run_command(name)),
            ],
            preamble_commands=self.preamble_commands,
        )
