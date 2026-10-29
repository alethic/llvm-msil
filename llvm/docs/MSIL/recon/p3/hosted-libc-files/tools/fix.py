#!/usr/bin/env python3
"""IR fix-ups for the hosted-libc simulation: fix.py <set> in.ll out.ll
Set 'vaaddr': an address of a native variadic function in a global
initializer (SQLite's aSyscall table: fcntl64, mremap) is replaced by the
address of a C variadic adapter in sim-va.c, msil_sim_va_<name>, which
reads the variadic arguments with va_arg and calls the native function with
fixed arity (the 'expanded-to-fixed adapter' of the variadics recon)."""
import re, sys
which, src, dst = sys.argv[1:4]
text = open(src, encoding="utf-8").read()
if "vaaddr" in which.split(","):
    for name in ("fcntl64", "fcntl", "mremap", "ioctl", "open64", "open"):
        m = re.search(r"^declare ([^@]*)@%s\(([^)]*)\)" % name, text, re.M)
        if not m or "..." not in m.group(2):
            continue
        new = "msil_sim_va_" + name
        lines = text.split("\n")
        changed = False
        for i, l in enumerate(lines):
            if l.startswith("@") and ("ptr @%s," % name in l or "ptr @%s " % name in l or "ptr @%s}" % name in l):
                lines[i] = re.sub(r"ptr @%s\b" % name, "ptr @" + new, l)
                changed = True
        text = "\n".join(lines)
        if changed and ("@" + new + "(") not in text:
            text += "\ndeclare %s@%s(%s)\n" % (m.group(1), new, m.group(2))
open(dst, "w", encoding="utf-8").write(text)
