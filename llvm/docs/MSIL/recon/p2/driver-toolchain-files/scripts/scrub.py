# Removes user-profile paths and user names from logs before they are copied into the
# (public) repository. Usage: python scrub.py < in > out
import re, sys
BS = chr(92)
s = sys.stdin.read()
# temp directories (single or doubled backslashes, or forward slashes)
s = re.sub(r'C:(?:\\\\|\\|/)Users(?:\\\\|\\|/)[^"\s\\/]+(?:\\\\|\\|/)AppData(?:\\\\|\\|/)Local(?:\\\\|\\|/)Temp(?:\\\\|\\|/)(?:lit-tmp-[a-z0-9_]+(?:\\\\|\\|/))?', '$TMP/', s)
s = re.sub(r'C:(?:\\\\|\\|/)Users(?:\\\\|\\|/)[^"\s\\/]+(?:\\\\|\\|/)AppData(?:\\\\|\\|/)Local(?:\\\\|\\|/)Microsoft(?:\\\\|\\|/)WinGet(?:\\\\|\\|/)Packages(?:\\\\|\\|/)[^"\s\\/]+(?:\\\\|\\|/)', '<winget>/', s)
s = re.sub(r'C:(?:\\\\|\\|/)Users(?:\\\\|\\|/)[^"\s\\/]+', '<userprofile>', s)
# Account and display names to remove are given in MSIL_SCRUB_NAMES (';'-separated),
# so that they never appear in this public file.
import os
for name in [n for n in os.environ.get('MSIL_SCRUB_NAMES', '').split(';') if n]:
    s = s.replace(name, '<user>')
sys.stdout.write(s)
