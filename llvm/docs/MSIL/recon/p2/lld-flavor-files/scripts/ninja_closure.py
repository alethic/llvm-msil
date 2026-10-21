"""Parse build.ninja (read-only) and report which build edges a target needs
that have not been built yet (output file missing). Usage:
  python ninja_closure.py <build_dir> <target> [<target> ...]
Never runs ninja; only reads build.ninja and checks file existence.
"""
import os, sys, collections, re

BS = chr(92)


def parse(path):
    edges = {}
    with open(path, encoding='utf-8', errors='replace') as f:
        data = f.read()
    data = data.replace('$\r\n', '').replace('$\n', '')
    for line in data.splitlines():
        if not line.startswith('build '):
            continue
        m = re.match(r'build (.*?)(?<!\$): (\S+)(.*)$', line)
        if not m:
            continue
        outs, rule, rest = m.group(1), m.group(2), m.group(3)

        def toks(s):
            return [t.replace('$ ', ' ').replace('$:', ':')
                    for t in re.split(r'(?<!\$) +', s.strip()) if t]
        oo = []
        if ' || ' in rest:
            rest, oo_s = rest.split(' || ', 1)
            oo = toks(oo_s)
        ins = [t for t in toks(rest) if t != '|']
        outs_l = [t for t in toks(outs) if t != '|']
        for o in outs_l:
            edges[o] = (rule, ins, oo, outs_l[0])
    return edges


def main():
    bdir = sys.argv[1]
    targets = sys.argv[2:]
    edges = parse(os.path.join(bdir, 'build.ninja'))
    seen_edges = set()
    need = collections.Counter()
    need_list = collections.defaultdict(list)
    stack = list(targets)
    visited = set()
    while stack:
        t = stack.pop()
        if t in visited:
            continue
        visited.add(t)
        e = edges.get(t)
        if not e:
            continue
        rule, ins, oo, first = e
        if first in seen_edges:
            continue
        seen_edges.add(first)
        exists = os.path.exists(os.path.join(bdir, first))
        if rule != 'phony' and not exists:
            kind = rule
            if 'CXX_COMPILER' in rule or 'C_COMPILER' in rule:
                kind = 'compile'
            elif 'LINKER' in rule:
                kind = 'link'
            elif 'STATIC_LIBRARY' in rule:
                kind = 'static_lib'
            need[kind] += 1
            need_list[kind].append(first)
        stack.extend(ins)
        stack.extend(oo)
    print('targets:', targets)
    for k, v in need.most_common():
        print(f'  {k}: {v}')
    dirs = collections.Counter()
    for p in need_list.get('compile', []):
        p2 = p.replace(BS, '/')
        parts = p2.split('/')
        dirs['/'.join(parts[:3])] += 1
    print('  compile by dir:', dict(dirs.most_common(30)))
    for k, v in need_list.items():
        if k != 'compile':
            print(f'  {k} outputs:', v[:60])


main()
