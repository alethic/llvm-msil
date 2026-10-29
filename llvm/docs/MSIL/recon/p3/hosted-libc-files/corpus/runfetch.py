# hosted-libc recon: run the tree's fetch.py (read-only import) against a proposed manifest and a scratch cache.
import sys
sys.path.insert(0, r"D:/llvm-msil/msil/utils/corpus")
import fetch
manifest, cache = sys.argv[1], sys.argv[2]
sys.exit(fetch.main(["--cache", cache] + sys.argv[3:], manifest_path=manifest))
