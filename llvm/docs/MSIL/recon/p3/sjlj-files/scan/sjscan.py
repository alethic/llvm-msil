#!/usr/bin/env python3
"""Count, per function that calls setjmp, the calls an E1-style pass would wrap.

usage: sjscan.py FILE.ll...   (clang -O2 -S -emit-llvm output)
A call is 'after setjmp' when its block is reachable from a block holding a setjmp call
(or follows the setjmp call in that block). Intrinsics (@llvm.*) are never wrapped;
'willreturn' callees are reported separately (a pass may skip them)."""
import re, sys
SJ = re.compile(r"call [^@]*@(setjmp|_setjmp|__sigsetjmp|sigsetjmp|_setjmpex)\(")
for path in sys.argv[1:]:
    text = open(path).read()
    attrs = dict(re.findall(r"^attributes (#\d+) = \{([^}]*)\}", text, re.M))
    decl_attr = {}
    for m in re.finditer(r"^(?:declare|define)[^@]*@([\w.$]+)\([^\n]*?\)\s*([^{\n]*)", text, re.M):
        decl_attr[m.group(1)] = " ".join(attrs.get(a, "") for a in re.findall(r"#\d+", m.group(2)))
    for fm in re.finditer(r"^define [^@]*@([\w.$]+)\(.*?^\}", text, re.M | re.S):
        body = fm.group(0)
        if not SJ.search(body):
            continue
        blocks, order, cur = {}, [], "entry0"
        for ln in body.splitlines()[1:]:
            m = re.match(r"^([\w.$-]+):", ln)
            if m:
                cur = m.group(1); order.append(cur); blocks[cur] = []; continue
            if cur == "entry0" and cur not in blocks:
                order.append(cur); blocks[cur] = []
            blocks.setdefault(cur, []).append(ln)
        succ = {b: set(re.findall(r"label %([\w.$-]+)", "\n".join(ls))) for b, ls in blocks.items()}
        start, seeds = [], set()
        for b in order:
            ls = blocks[b]
            for i, ln in enumerate(ls):
                if SJ.search(ln):
                    start.append((b, i)); seeds |= succ[b]
        seen, todo = set(), list(seeds)
        while todo:
            b = todo.pop()
            if b in seen or b not in blocks: continue
            seen.add(b); todo += succ[b]
        def calls(lines):
            out = []
            for ln in lines:
                m = re.search(r"\b(?:tail |musttail |notail )?(call|invoke) [^@%]*?([@%][\w.$\"-]+)\(", ln)
                if not m or SJ.search(ln): continue
                callee = m.group(2)
                if callee.startswith("@llvm."): continue
                out.append(callee)
            return out
        after = []
        for b, i in start:
            if b not in seen: after += calls(blocks[b][i + 1:])
        for b in seen: after += calls(blocks[b])
        total = sum(len(calls(ls)) for ls in blocks.values())
        ind = sum(1 for c in after if c.startswith("%"))
        wr = sum(1 for c in after if c.startswith("@") and "willreturn" in decl_attr.get(c[1:], ""))
        print("%-28s %-34s setjmp sites %d, calls after setjmp %3d (indirect %d, willreturn %d), calls in function %3d"
              % (path.split("/")[-1], fm.group(1), len(start), len(after), ind, wr, total))
