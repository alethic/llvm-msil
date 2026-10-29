#!/usr/bin/env python3
"""Static inventory of Phase-3 constructs in the textual IR (-flto -S) of a
program: one or more .ll files. Usage: inv.py [--json OUT] file.ll ...
"""
import re, sys, json, collections, argparse

RE_DEFINE = re.compile(r'^define\s+(.*?)@("?[^"(]+"?)\((.*)\)\s*(.*)\{\s*$')
RE_DECLARE = re.compile(r'^declare\s+(.*?)@("?[^"(]+"?)\((.*)\)(.*)$')
RE_GLOBAL = re.compile(r'^@("?[^"=\s]+"?)\s*=\s*(.*)$')
RE_CALL = re.compile(r'\b(?:tail |musttail |notail )?(call|invoke)\b\s+(.*?)\s(ptr|@)')
RE_VARCALL = re.compile(r'\bcall\b[^@%]*\(([^()]*)\.\.\.\)\s*(@"?[\w.$"]+"?|%[\w.]+)\(')
RE_DIRECTCALL = re.compile(r'\bcall\b.*?(@"?[\w.$]+"?)\(')
RE_FNREF = re.compile(r'@("[^"]*"|[\w.$]+)')

LDOUBLE = set("""sqrtl expl exp2l expm1l logl log2l log10l log1pl powl sinl cosl tanl asinl acosl atanl atan2l sinhl coshl tanhl
asinhl acoshl atanhl fabsl floorl ceill truncl roundl lroundl llroundl rintl lrintl llrintl nearbyintl fmodl remainderl remquol
frexpl ldexpl modfl scalbnl scalblnl ilogbl logbl cbrtl hypotl erfl erfcl lgammal tgammal fmal fmaxl fminl fdiml copysignl nanl
nextafterl nexttowardl nexttoward nexttowardf strtold wcstold sincosl exp10l significandl""".split())

CATS = [
    ("stdio", r"^(f?printf|s?n?printf|v\w*printf|f?scanf|sscanf|v\w*scanf|f?puts|f?putc|putchar|_?fputc|f?getc|getchar|fgets|fread|fwrite|fopen|fopen64|freopen\w*|fclose|fflush|fseek\w*|ftell\w*|rewind|setvbuf|setbuf|tmpfile\w*|tmpnam|remove|rename|ferror|feof|clearerr|ungetc|perror|fileno|_fileno|fdopen|_fdopen|popen|pclose|_popen|_pclose|getline|getdelim|__acrt_iob_func|__stdio_common_\w+|_lock_file|_unlock_file|flockfile|funlockfile|getc_unlocked|putc_unlocked|_getc_nolock|_fseeki64|_ftelli64|_wfopen|fgetpos|fsetpos|__local_stdio_\w+)$"),
    ("string/mem", r"^(mem\w+|str\w+|wcs\w+|wmem\w+|_str\w+|_mem\w+|__strdup)$"),
    ("ctype/locale", r"^(is\w+|to(upper|lower)|__ctype\w+|_isctype\w*|___mb_cur_max_func|__pctype_func|_pctype|setlocale|localeconv|_errno|__errno_location|newlocale|uselocale|freelocale|nl_langinfo|___lc_\w+|mb\w+|wc\w+|btowc|wctob)$"),
    ("alloc", r"^(malloc|calloc|realloc|free|_aligned_\w+|posix_memalign|aligned_alloc|malloc_usable_size|_msize)$"),
    ("process/env", r"^(exit|_exit|_Exit|abort|atexit|getenv|_wgetenv|system|_wsystem|getpid|_getpid|raise|signal|sigaction|sigemptyset|sigaddset|kill|__p__\w+|__p___\w+|_set_\w+|_get_\w+|getpagesize|sysconf|_onexit)$"),
    ("time", r"^(time|_time64|clock|difftime|_difftime64|mktime|_mktime64|gmtime\w*|_gmtime64\w*|localtime\w*|_localtime64\w*|strftime|asctime|ctime|gettimeofday|clock_gettime|nanosleep|usleep|_ftime\w*|tzset|_tzset|times)$"),
    ("posix-io", r"^(open|open64|_open|_wopen|close|_close|read|_read|write|_write|lseek|lseek64|_lseeki64|_lseek|fstat\w*|stat\w*|lstat\w*|_fstat\w*|_stat\w*|_wstat\w*|access|_access|_waccess|unlink|_unlink|_wunlink|mkdir|rmdir|getcwd|chdir|fcntl|fcntl64|ioctl|ftruncate\w*|fsync|fdatasync|pread\w*|pwrite\w*|mmap\w*|munmap|mremap|readlink|fchmod|fchown|fallocate|mkstemp\w*|dup2?|pipe|isatty|_isatty|utime\w*|opendir|readdir\w*|closedir|realpath|_fullpath|_wfullpath|geteuid|getuid|chmod|_chmod|_chsize\w*|_commit|_get_osfhandle|_setmode|_locking|_dup2?|_findfirst\w*|_findnext\w*|_findclose|_mktemp\w*|_tempnam|tempnam)$"),
    ("dl", r"^(dlopen|dlsym|dlclose|dlerror|LoadLibrary\w*|GetProcAddress|FreeLibrary)$"),
    ("threads", r"^(pthread_\w+|thrd_\w+|mtx_\w+|cnd_\w+|tss_\w+|sched_yield|sem_\w+)$"),
    ("math", r"^(sqrt|cbrt|floor|ceil|trunc|round|lround|llround|rint|lrint|nearbyint|fmod|remainder|fma|fmin|fmax|fdim|sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|exp|exp2|expm1|log|log2|log10|log1p|pow|ldexp|frexp|modf|hypot|fabs|copysign|nan|nextafter|erf|erfc|lgamma|tgamma|logb|ilogb|scalbn|_?hypot|_copysign|_isnan|_finite|_fdtest|_dtest|_ldtest|_fdclass|_dclass)f?$"),
    ("win32", r"^([A-Z]\w+)$"),
    ("conv", r"^(atoi|atol|atoll|atof|strto\w+|_atoi64|_strtoi64|_strtoui64|abs|labs|llabs|div|ldiv|rand|srand|qsort|bsearch|_byteswap_\w+)$"),
    ("jmp", r"^(_?setjmp\w*|__sigsetjmp|_?longjmp|siglongjmp)$"),
]


def cat(name):
    for c, r in CATS:
        if re.match(r, name):
            return c
    return "other"


def strip(n):
    return n.strip('"')


def parse(files):
    defs = {}          # name -> (linkage line, isvararg)
    decls = {}         # name -> signature text
    gdefs = {}         # global var name -> text
    gext = {}          # external globals
    tls = []
    calls = collections.Counter()      # (callee) -> count of direct calls
    varcalls = collections.Counter()   # callee -> count
    varcall_indirect = 0
    fnargs_to = collections.Counter()  # (ext callee, fn passed) -> count
    fnaddr_in_data = collections.Counter()  # declared function address in a global initializer
    atom = collections.Counter()
    intr = collections.Counter()
    dynalloca = 0
    stacksave = 0
    asm = 0
    i128 = 0
    byval_ext = collections.Counter()
    sret_ext = collections.Counter()
    vastart = 0
    vacopy = 0
    dtors = 0
    returns_twice = set()
    per_file_defs = collections.defaultdict(list)
    for fn in files:
        cur = None
        entry = True
        for line in open(fn, encoding="utf-8", errors="replace"):
            line = line.rstrip("\n")
            m = RE_DEFINE.match(line)
            if m:
                name = strip(m.group(2))
                params = m.group(3)
                defs[name] = (m.group(1), params.rstrip().endswith("..."))
                per_file_defs[fn].append(name)
                cur = name
                entry = True
                firstline = True
                continue
            if line.startswith("}"):
                cur = None
                continue
            m = RE_DECLARE.match(line)
            if m:
                name = strip(m.group(2))
                decls[name] = m.group(3)
                if "returns_twice" in line:
                    returns_twice.add(name)
                continue
            if line.startswith("attributes #") and "returns_twice" in line:
                pass
            m = RE_GLOBAL.match(line)
            if m and cur is None:
                name = strip(m.group(1))
                rest = m.group(2)
                if name == "llvm.global_dtors":
                    dtors += 1
                if re.match(r"(external|extern_weak)\b", rest) or re.match(r"(dso_local |dllimport )*external", rest):
                    gext[name] = rest
                else:
                    gdefs[name] = rest
                if "thread_local" in rest:
                    tls.append(name)
                continue
            if cur is None:
                continue
            s = line.strip()
            if not s:
                continue
            if re.match(r'^("[^"]*"|[\w.$-]+):', s) and not firstline:
                entry = False
            firstline = False
            if re.search(r"(?<![\w.%])i128\b", s):
                i128 += 1
            if re.search(r"\batomicrmw\b", s):
                atom["atomicrmw"] += 1
            if re.search(r"\bcmpxchg\b", s):
                atom["cmpxchg"] += 1
            if re.search(r"\bload atomic\b", s):
                atom["load atomic"] += 1
            if re.search(r"\bstore atomic\b", s):
                atom["store atomic"] += 1
            if re.search(r"\bfence\b", s):
                atom["fence " + ("singlethread" if "syncscope(\"singlethread\")" in s else "system")] += 1
            if re.search(r"=\s*alloca\b", s):
                if not entry or re.search(r"alloca [^,]+, i(32|64) %", s):
                    dynalloca += 1
            if " asm " in s or "asm sideeffect" in s:
                asm += 1
            m = RE_VARCALL.search(s)
            if m:
                callee = m.group(2)
                if callee.startswith("%"):
                    varcall_indirect += 1
                else:
                    varcalls[strip(callee[1:])] += 1
            for mm in re.finditer(r"@(llvm\.[\w.]+)\(", s):
                intr[mm.group(1)] += 1
            if "@llvm.stacksave" in s:
                stacksave += 1
            if "@llvm.va_start" in s:
                vastart += 1
            if "@llvm.va_copy" in s:
                vacopy += 1
            m = RE_DIRECTCALL.search(s)
            if m and re.search(r"\b(call|invoke)\b", s):
                callee = strip(m.group(1)[1:])
                calls[callee] += 1
                # args after the callee
                argtxt = s[m.end():]
                for fm in RE_FNREF.finditer(argtxt):
                    fnargs_to[(callee, strip(fm.group(1)))] += 1
                if " byval(" in argtxt:
                    byval_ext[callee] += 1
                if " sret(" in argtxt:
                    sret_ext[callee] += 1
    return dict(defs=defs, decls=decls, gdefs=gdefs, gext=gext, tls=tls, calls=calls, varcalls=varcalls,
                varcall_indirect=varcall_indirect, fnargs_to=fnargs_to, atom=atom, intr=intr,
                dynalloca=dynalloca, stacksave=stacksave, asm=asm, i128=i128, byval_ext=byval_ext,
                sret_ext=sret_ext, vastart=vastart, vacopy=vacopy, dtors=dtors, returns_twice=returns_twice)


def report(d, out=sys.stdout):
    defs, decls = d["defs"], d["decls"]
    ext = {n for n in decls if not n.startswith("llvm.")} - set(defs)
    vdefs = sorted(n for n, (_, v) in defs.items() if v)
    p = lambda *a: print(*a, file=out)
    p(f"definitions: {len(defs)}; external functions: {len(ext)}; external globals: {len(d['gext'])}")
    p(f"variadic definitions ({len(vdefs)}): {', '.join(vdefs)}")
    vc_ext = {k: v for k, v in d["varcalls"].items() if k not in defs}
    vc_own = {k: v for k, v in d["varcalls"].items() if k in defs}
    p(f"variadic call sites to external (native) functions ({sum(vc_ext.values())}): " +
      ", ".join(f"{k} {v}" for k, v in sorted(vc_ext.items(), key=lambda x: -x[1])))
    p(f"variadic call sites to own definitions ({sum(vc_own.values())}): " +
      ", ".join(f"{k} {v}" for k, v in sorted(vc_own.items(), key=lambda x: -x[1])))
    p(f"indirect variadic calls: {d['varcall_indirect']}; va_start: {d['vastart']}; va_copy: {d['vacopy']}")
    jmp = {k: v for k, v in d["calls"].items() if re.search(r"setjmp|longjmp", k)}
    p(f"setjmp/longjmp calls: {jmp}; returns_twice decls: {sorted(d['returns_twice'])}")
    cb = collections.Counter()
    for (callee, fn), c in d["fnargs_to"].items():
        if callee in ext and fn in defs:
            cb[(callee, fn)] += c
    p(f"own functions passed to external functions (callbacks) ({sum(cb.values())}): " +
      ", ".join(f"{a}({b}) {c}" for (a, b), c in sorted(cb.items())))
    extfn_addr = collections.Counter()
    for (callee, fn), c in d["fnargs_to"].items():
        if fn in ext:
            extfn_addr[(callee, fn)] += c
    p(f"external function addresses passed as arguments: " +
      ", ".join(f"{a}({b}) {c}" for (a, b), c in sorted(extfn_addr.items())))
    # external function addresses in global initializers
    ext_in_data = collections.Counter()
    own_in_data = collections.Counter()
    for g, txt in d["gdefs"].items():
        for fm in RE_FNREF.finditer(txt):
            n = strip(fm.group(1))
            if n in ext:
                ext_in_data[n] += 1
            elif n in defs:
                own_in_data[n] += 1
    p(f"external (native) function addresses in data initializers ({len(ext_in_data)}): " +
      ", ".join(sorted(ext_in_data)))
    p(f"own function addresses in data initializers: {len(own_in_data)} functions")
    p(f"external globals (data imports): " + ", ".join(sorted(set(d["gext"]) - set(d["gdefs"]))))
    p(f"thread_local globals: {d['tls']}")
    p(f"atomics: {dict(d['atom'])}")
    p(f"dynamic allocas: {d['dynalloca']}; stacksave: {d['stacksave']}; inline asm: {d['asm']}; i128 lines: {d['i128']}")
    p(f"global_dtors: {d['dtors']}; atexit calls: {d['calls'].get('atexit', 0)}")
    p(f"byval args to external functions: {dict((k, v) for k, v in d['byval_ext'].items() if k in ext)}")
    p(f"sret calls to external functions: {dict((k, v) for k, v in d['sret_ext'].items() if k in ext)}")
    ld = sorted(n for n in ext if n in LDOUBLE)
    p(f"long double library functions: {ld}")
    intr = {k: v for k, v in d["intr"].items() if not re.match(r"llvm\.(lifetime|dbg|memcpy|memset|memmove|assume|experimental\.noalias|va_start|va_end|va_copy)", k)}
    p(f"intrinsics (excluding lifetime/mem*/va_*): " + ", ".join(f"{k} {v}" for k, v in sorted(intr.items())))
    bycat = collections.defaultdict(list)
    for n in sorted(ext):
        bycat[cat(n)].append(n)
    p("external functions by category:")
    for c in sorted(bycat):
        p(f"  {c} ({len(bycat[c])}): {' '.join(bycat[c])}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    d = parse(a.files)
    report(d)


if __name__ == "__main__":
    main()
