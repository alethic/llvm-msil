#!/usr/bin/env python3
# ===-- probe-cache-edit.py - Check and edit lit.cfg.py's probe cache -----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Checks and edits the probe cache of msil/test/lit.cfg.py (plan 5.17.10)
for ../probe-cache.test.

    probe-cache-edit.py check FILE     FILE is {"version": 1, "key":
                                       {"lit_cfg_sha256": <64 hex digits>},
                                       "probes": {NAME: {"key": {...},
                                       "value": ...}, ...}} with a probe of
                                       'dotnet --list-sdks'; prints
                                       'probe cache ok: <n> probes'
    probe-cache-edit.py EDIT FILE      rewrites FILE with one change:
        garbage     text that is not JSON
        version     version 2
        sha         another SHA-256 of lit.cfg.py
        probekey    the size of dotnet in the key of 'dotnet --list-sdks'
        dropprobe   no 'dotnet --list-sdks' probe
        poison      the SDK version 10.0.999 in the value of that probe, with
                    its key untouched, so a configuration that reads the
                    cache reports that SDK
    probe-cache-edit.py listdir DIR    prints the names in DIR, sorted, one
                                       per line

Exits 1 with a message when FILE is not as 'check' describes it.
"""

import json
import os
import re
import sys

SDK_PROBE = "dotnet --list-sdks "


def fail(msg):
    sys.stderr.write("probe-cache-edit: %s\n" % msg)
    return 1


def sdk_probe(cache):
    names = [n for n in cache["probes"] if n.startswith(SDK_PROBE)]
    if len(names) != 1:
        raise ValueError("%d probes named '%s...'" % (len(names), SDK_PROBE))
    return cache["probes"][names[0]]


def check(cache):
    if not isinstance(cache, dict) or set(cache) != {"version", "key", "probes"}:
        raise ValueError("the top level is not {version, key, probes}")
    if cache["version"] != 1:
        raise ValueError("version %r" % (cache["version"],))
    sha = cache["key"].get("lit_cfg_sha256")
    if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{64}", sha):
        raise ValueError("key.lit_cfg_sha256 is %r" % (sha,))
    if not cache["probes"]:
        raise ValueError("no probes")
    for name, probe in cache["probes"].items():
        if not isinstance(probe, dict) or set(probe) != {"key", "value"}:
            raise ValueError("probe %r is not {key, value}" % name)
        if not isinstance(probe["key"], dict):
            raise ValueError("the key of probe %r is not an object" % name)
    sdk_probe(cache)
    return len(cache["probes"])


def main(argv):
    if len(argv) != 2:
        return fail("usage: probe-cache-edit.py check|EDIT FILE")
    what, path = argv
    if what == "listdir":
        for name in sorted(os.listdir(path)):
            sys.stdout.buffer.write(name.encode("utf-8") + b"\n")
        return 0
    if what == "garbage":
        with open(path, "w", encoding="utf-8") as f:
            f.write("this is not JSON {\n")
        return 0
    try:
        with open(path, encoding="utf-8") as f:
            cache = json.load(f)
        n = check(cache)
    except (OSError, ValueError, KeyError, TypeError) as e:
        return fail("%s: %s" % (path, e))
    if what == "check":
        print("probe cache ok: %d probes" % n)
        return 0
    probe = sdk_probe(cache)
    if what == "version":
        cache["version"] = 2
    elif what == "sha":
        cache["key"]["lit_cfg_sha256"] = "0" * 64
    elif what == "probekey":
        tool = probe["key"].get("tools", {}).get("dotnet")
        if not isinstance(tool, list) or len(tool) != 3:
            return fail(
                "%s: the SDK probe's key names no dotnet [path, size, mtime]" % path
            )
        tool[1] += 1
    elif what == "dropprobe":
        del cache["probes"][[n for n in cache["probes"] if n.startswith(SDK_PROBE)][0]]
    elif what == "poison":
        value = probe["value"]
        if not isinstance(value, list) or not value:
            return fail("%s: the SDK probe's value is not [version, ...]" % path)
        value[0] = "10.0.999"
    else:
        return fail("unknown edit %r" % what)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
