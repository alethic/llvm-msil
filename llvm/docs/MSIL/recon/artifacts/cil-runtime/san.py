import sys, re
# Replace the long scratch-directory prefix with $SCR/ so logs are readable.
PAT = re.compile(r'C:[\\/]Users[\\/][^ ]*?[\\/]cil-runtime[\\/]')
for l in sys.stdin:
    sys.stdout.write(PAT.sub('$SCR/', l))
