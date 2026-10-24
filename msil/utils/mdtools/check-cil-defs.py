#!/usr/bin/env python3
# ===-- check-cil-defs.py - CILOpcodes.def against System.Reflection.Emit -===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Compare llvm/include/llvm/BinaryFormat/CILOpcodes.def with the opcode table
of System.Reflection.Emit.OpCodes, an independent implementation.

    python check-cil-defs.py [--def FILE] [--mdtools DIR] [--dotnet PATH]

Every CIL_OPCODE(Enum, Text, Value, Operand, Pop, Push, Flow) entry must equal
the OpCodes entry of the same name in value, operand kind, StackBehaviourPop,
StackBehaviourPush and FlowControl, as 'mdump --opcodes' prints them (plan
5.7.11). ECMA-335's 'no.' prefix (0xFE19) is expected to be missing from
OpCodes, and the reserved prefixes of OpCodes (prefix1-prefix7, prefixref,
0xF8-0xFF) are expected to be missing from the .def; any other entry missing
on either side is an error. The .def must also be in strictly increasing
value order.

--def defaults to the repository's CILOpcodes.def; --mdtools to the directory
build-mdtools.py writes (<build>/tools/msil/mdtools, where <build> is
$MSIL_BUILD_DIR or <repo>/build); dotnet is found as build-mdtools.py finds
it. The script does not build mdump; run build-mdtools.py first.

Output: one line per difference, then a summary line
'check-cil-defs.py: <n> of <m> opcodes equal ...'. Exit status: 0 when the
tables agree, 1 otherwise (2 on a usage error).
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DEFAULT_DEF = os.path.join(
    REPO, "llvm", "include", "llvm", "BinaryFormat", "CILOpcodes.def"
)

# In ECMA-335 III.2.2 but not in System.Reflection.Emit.OpCodes.
ECMA_ONLY = {"no."}
ENTRY = re.compile(
    r'^CIL_OPCODE\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(\w+)\s*,'
    r"\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)\s*$"
)


def fail(msg):
    sys.stderr.write("check-cil-defs.py: error: %s\n" % msg)
    sys.exit(1)


def find_dotnet(explicit):
    """The dotnet host, found exactly as build-mdtools.py finds it (loaded
    without writing a __pycache__ into the source tree)."""
    spec = importlib.util.spec_from_file_location(
        "build_mdtools", os.path.join(HERE, "build-mdtools.py")
    )
    build_mdtools = importlib.util.module_from_spec(spec)
    saved, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec.loader.exec_module(build_mdtools)
    finally:
        sys.dont_write_bytecode = saved
    try:
        return build_mdtools.find_dotnet(explicit)
    except build_mdtools.BuildError as e:
        fail(str(e))


def read_def(path):
    """[(text, value, operand, pop, push, flow)] in file order."""
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError as e:
        fail("cannot read %s: %s" % (path, e))
    entries = []
    for n, line in enumerate(lines, 1):
        if not line.startswith("CIL_OPCODE("):
            continue
        m = ENTRY.match(line)
        if not m:
            fail("%s:%d: cannot parse %r" % (path, n, line))
        _, text, value, operand, pop, push, flow = m.groups()
        entries.append((text, int(value, 16), operand, pop, push, flow))
    if not entries:
        fail("no CIL_OPCODE entries in %s" % path)
    return entries


def read_opcodes(dotnet, mdtools):
    dll = os.path.join(mdtools, "mdump.dll")
    config = os.path.join(mdtools, "mdump.runtimeconfig.json")
    if not os.path.isfile(dll) or not os.path.isfile(config):
        fail(
            "%s not found: build it with 'python %s' (or pass --mdtools DIR)"
            % (dll, os.path.join(HERE, "build-mdtools.py"))
        )
    cmd = [dotnet, "exec", "--runtimeconfig", config, dll, "--opcodes"]
    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=300)
    except (OSError, subprocess.SubprocessError) as e:
        fail("running mdump failed: %s" % e)
    if proc.returncode != 0:
        fail(
            "'%s' exited %d:\n%s"
            % (" ".join(cmd), proc.returncode, proc.stderr.decode("utf-8", "replace"))
        )
    ops = {}
    for line in proc.stdout.decode("utf-8").splitlines():
        f = line.split("\t")
        if len(f) != 6:
            fail("unexpected 'mdump --opcodes' line %r" % line)
        name, value, operand, pop, push, flow = f
        if name in ops:
            fail("'mdump --opcodes' lists %s twice" % name)
        ops[name] = (int(value, 16), operand, pop, push, flow)
    if not ops:
        fail("'mdump --opcodes' printed nothing")
    return ops


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument(
        "--def", dest="def_file", default=DEFAULT_DEF, help="CILOpcodes.def to check"
    )
    p.add_argument(
        "--mdtools",
        help="directory with mdump.dll (default <build>/tools/msil/mdtools)",
    )
    p.add_argument(
        "--dotnet",
        help="dotnet host (default: from PATH, DOTNET_ROOT or the standard "
        "install locations, as build-mdtools.py finds it)",
    )
    args = p.parse_args()

    build = os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")
    mdtools = args.mdtools or os.path.join(build, "tools", "msil", "mdtools")
    entries = read_def(args.def_file)
    ops = read_opcodes(find_dotnet(args.dotnet), mdtools)
    reserved = {n for n in ops if n.startswith("prefix")}

    problems = []
    prev = -1
    for text, value, *_ in entries:
        if value <= prev:
            problems.append(
                "%s (0x%04X) is not in increasing value order" % (text, value)
            )
        prev = value
    seen = set()
    equal = 0
    missing_ok = []
    for text, value, operand, pop, push, flow in entries:
        if text in seen:
            problems.append("%s is listed twice in CILOpcodes.def" % text)
            continue
        seen.add(text)
        if text not in ops:
            if text in ECMA_ONLY:
                missing_ok.append(text)
            else:
                problems.append(
                    "%s (0x%04X) is missing from System.Reflection.Emit.OpCodes"
                    % (text, value)
                )
            continue
        mine = (value, operand, pop, push, flow)
        theirs = ops[text]
        if mine != theirs:
            fields = ("value", "operand", "pop", "push", "flow")
            diffs = [
                "%s %s vs %s"
                % (
                    k,
                    ("0x%04X" % a) if k == "value" else a,
                    ("0x%04X" % b) if k == "value" else b,
                )
                for k, a, b in zip(fields, mine, theirs)
                if a != b
            ]
            problems.append("%s differs from OpCodes: %s" % (text, "; ".join(diffs)))
        else:
            equal += 1
    for name in sorted(ops, key=lambda n: ops[n][0]):
        if name not in seen and name not in reserved:
            problems.append(
                "%s (0x%04X) of OpCodes is missing from CILOpcodes.def"
                % (name, ops[name][0])
            )
    for name in sorted(ECMA_ONLY - set(missing_ok)):
        if name not in seen:
            problems.append("%s is missing from CILOpcodes.def" % name)

    for line in problems:
        print("check-cil-defs.py: " + line)
    print(
        "check-cil-defs.py: %d of %d opcodes equal to System.Reflection.Emit.OpCodes"
        " (%d there, %d reserved prefixes skipped); missing from OpCodes as expected: %s; %s"
        % (
            equal,
            len(entries),
            len(ops) - len(reserved),
            len(reserved),
            ", ".join(missing_ok) or "none",
            "PASS" if not problems else "FAIL (%d problems)" % len(problems),
        )
    )
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
