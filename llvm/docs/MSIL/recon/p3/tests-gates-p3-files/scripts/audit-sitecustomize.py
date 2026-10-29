import sys, os, time
_LOG = os.environ.get("MSIL_P3_OPENLOG")
_PLOG = os.environ.get("MSIL_P3_POPENLOG")
def _w(path, text):
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write(text)
    except Exception:
        pass
def _hook(event, args):
    if event == "open" and _LOG:
        p = str(args[0])
        if p.endswith("lit.cfg.py") or p.endswith("lit.site.cfg.py") or p.endswith("lit.local.cfg"):
            _w(_LOG, "%d %.3f %s\n" % (os.getpid(), time.time(), p))
    elif event == "subprocess.Popen" and _PLOG:
        exe, argv = args[0], args[1]
        if isinstance(argv, (list, tuple)):
            a = [str(x) for x in argv]
        else:
            a = [str(argv)]
        _w(_PLOG, "%d\t%.3f\t%s\t%s\n" % (os.getpid(), time.time(), os.path.basename(sys.argv[0]) if sys.argv else "?", " ".join(a)[:400].replace("\t", " ")))
if _LOG or _PLOG:
    sys.addaudithook(_hook)
