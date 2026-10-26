#!/usr/bin/env python3
# ===-- refrows.py - The BCL rows of an image, in first-use order ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Print the AssemblyRef rows of an image and the scope of each TypeRef, and
check that the AssemblyRefs are in first-use order (plan 5.11.6).

    refrows.py TABLES

TABLES is the output of 'llvm-cil-dump --tables' (plan 5.7.10): a line
'Table: <Name> rows=<n> rowsize=<n>' per table, then one line
'  <Name>[<row>]: <Column>=<value> ...' per row, string columns as '<text>'
and coded indexes as tokens 0x<8 hex digits>. The output is, in this order:

    assemblyref <row> <Name> <a.b.c.d> token=<16 hex digits|-> flags=0x<8X> culture='<c>' hash=<hex|->
    typeref <Namespace>.<Name> -> <AssemblyRef name>       (scope an AssemblyRef)
    typeref <Namespace>.<Name> -> nested                   (scope a TypeRef)
    typeref <Namespace>.<Name> -> <other scope token>      (anything else)
    first use: <AssemblyRef name>, ...

msil-ld emits an AssemblyRef row when it first binds a type of that assembly,
together with the TypeRef that binds it, so the AssemblyRef rows are in
first-use order exactly when the first TypeRef scoped to AssemblyRef k comes
before the first one scoped to AssemblyRef k+1, and no AssemblyRef row is
left without a TypeRef (5.11.6: nothing is emitted for a reference that bound
nothing). Exit status 0 when that holds, 1 when it does not (with a message
on stderr), 2 when the input is not a table dump.
"""

import re
import sys

ROW_RE = re.compile(r"^  (\w+)\[(\d+)\]: (.*)$")
# Column=value pairs: a quoted string (with \' and \\ escapes), or the text up
# to the next ' <Column>=' (a blob is byte pairs separated by blanks).
COL_RE = re.compile(r"(\w+)=('(?:[^'\\]|\\.)*'|.*?)(?= \w+=|$)")


def columns(text):
    cols = {}
    for name, value in COL_RE.findall(text):
        if value.startswith("'"):
            value = re.sub(r"\\(.)", r"\1", value[1:-1])
        cols[name] = value
    return cols


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: refrows.py TABLES\n")
        return 2
    with open(argv[1], encoding="utf-8") as f:
        lines = f.read().splitlines()
    if not any(line.startswith("Table: ") for line in lines):
        sys.stderr.write("refrows.py: %s is not an llvm-cil-dump --tables dump\n" % argv[1])
        return 2
    asmrefs, typerefs = {}, []
    table = None
    for line in lines:
        if line.startswith("Table: "):
            table = line.split()[1]
            continue
        m = ROW_RE.match(line)
        if not m or m.group(1) != table:
            continue
        cols = columns(m.group(3))
        if table == "AssemblyRef":
            asmrefs[int(m.group(2))] = cols
        elif table == "TypeRef":
            typerefs.append(cols)
    for row in sorted(asmrefs):
        c = asmrefs[row]
        version = ".".join(
            str(int(c[k], 16))
            for k in ("MajorVersion", "MinorVersion", "BuildNumber", "RevisionNumber")
        )
        token = c.get("PublicKeyOrToken", "-").replace(" ", "").lower()
        print(
            "assemblyref %d %s %s token=%s flags=%s culture='%s' hash=%s"
            % (
                row,
                c.get("Name", "?"),
                version,
                token or "-",
                c.get("Flags", "?"),
                c.get("Culture", "?"),
                c.get("HashValue", "?").replace(" ", "").lower(),
            )
        )
    first = []
    for c in typerefs:
        scope = int(c.get("ResolutionScope", "0"), 16)
        name = c.get("TypeName", "?")
        ns = c.get("TypeNamespace", "")
        full = ns + "." + name if ns else name
        table_id, row = scope >> 24, scope & 0xFFFFFF
        if table_id == 0x23 and row in asmrefs:
            print("typeref %s -> %s" % (full, asmrefs[row].get("Name", "?")))
            if row not in first:
                first.append(row)
        elif table_id == 0x01:
            print("typeref %s -> nested" % full)
        else:
            print("typeref %s -> 0x%08X" % (full, scope))
    print("first use: " + ", ".join(asmrefs[r].get("Name", "?") for r in first))
    expected = sorted(asmrefs)
    if first != expected:
        sys.stderr.write(
            "refrows.py: the AssemblyRef rows are not in first-use order: rows %s,"
            " first used by the TypeRefs in the order %s\n"
            % (expected, first)
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
