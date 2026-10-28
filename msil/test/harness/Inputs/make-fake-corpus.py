#!/usr/bin/env python3
# ===-- make-fake-corpus.py - A fake exit-corpus cache for corpus-param.test ===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Write a cache that looks to lit.cfg.py like one that
msil/utils/corpus/fetch.py verified (plan 5.12.9), without any source:

    make-fake-corpus.py MANIFEST CACHE [--omit NAME]... [--stale]

CACHE gets an empty directory <name>-<ref> per entry of MANIFEST and the
stamp msil-corpus.stamp, {"manifest_sha256": <sha256 of MANIFEST's bytes>,
"trees": {<name>: <tree_sha256>}} (indent 2, sorted keys, as fetch.py writes
it). --omit leaves an entry out of the stamp (its directory stays), as
'fetch.py --skip' does; --stale writes another manifest_sha256, as a stamp
of an older manifest. CACHE is emptied first."""

import argparse
import hashlib
import json
import os
import shutil

p = argparse.ArgumentParser()
p.add_argument("manifest")
p.add_argument("cache")
p.add_argument("--omit", action="append", default=[])
p.add_argument("--stale", action="store_true")
args = p.parse_args()

with open(args.manifest, "rb") as f:
    data = f.read()
entries = json.loads(data.decode("utf-8"))["programs"]
if os.path.isdir(args.cache):
    shutil.rmtree(args.cache)
os.makedirs(args.cache)
trees = {}
for e in entries:
    os.makedirs(os.path.join(args.cache, "%s-%s" % (e["name"], e["ref"])))
    if e["name"] not in args.omit:
        trees[e["name"]] = e["tree_sha256"]
sha = hashlib.sha256(data).hexdigest()
if args.stale:
    sha = hashlib.sha256(b"an older manifest").hexdigest()
with open(os.path.join(args.cache, "msil-corpus.stamp"), "w", encoding="utf-8", newline="\n") as f:
    f.write(json.dumps({"manifest_sha256": sha, "trees": trees}, indent=2, sort_keys=True) + "\n")
print("make-fake-corpus: %s (%d of %d trees%s)" % (args.cache, len(trees), len(entries), ", stale" if args.stale else ""))
