# Synthetic multi-TU C program: NTU self-contained translation units (only Phase-1
# features: no varargs, no libc), each exporting tu<N>_entry(); main.c calls all.
import random, sys
NTU, NF = int(sys.argv[1]), int(sys.argv[2])
random.seed(1)
for t in range(NTU):
    L = [f"static int g{t}[64] = {{{', '.join(str(random.randint(0,99)) for _ in range(64))}}};",
         f"struct S{t} {{ int a; long long b; short c[4]; }};",
         f"static struct S{t} s{t}[8];"]
    for f in range(NF):
        k = random.randint(1, 9)
        callee = f"f{t}_{f-1}(x ^ {k}, n - 1)" if f else "x"
        L.append(f"""__attribute__((noinline)) static int f{t}_{f}(int x, int n) {{
  int acc = {k};
  for (int i = 0; i < (n & 31); ++i) {{
    acc += g{t}[(x + i) & 63] * {k} - (acc >> 3);
    if (acc & 1) acc ^= x << (i & 7); else acc -= i * {f % 5 + 1};
    switch ((acc ^ i) & 7) {{
      case 0: acc += 3; break; case 1: acc -= x; break; case 2: acc *= 5; break;
      case 3: s{t}[i & 7].a += acc; break; case 4: s{t}[i & 7].b += acc; break;
      default: s{t}[(i + 1) & 7].c[i & 3] = (short)acc; break;
    }}
  }}
  return n > 0 ? acc + {callee} : acc;
}}""")
    L.append(f"int tu{t}_entry(int x) {{ int r = 0; " + " ".join(f"r += f{t}_{f}(x + {f}, {f % 7 + 2});" for f in range(NF)) + " return r; }")
    open(f"tu{t}.c", "w").write("\n".join(L) + "\n")
M = [f"int tu{t}_entry(int x);" for t in range(NTU)]
M.append("int main(void) { unsigned r = 0; " + " ".join(f"r = r * 31u + (unsigned)tu{t}_entry({t});" for t in range(NTU)) + " return (int)(r & 0xff); }")
open("main.c", "w").write("\n".join(M) + "\n")
