"""Rewrites the Phase-1 output p1prog.il (clang -S, [mscorlib] only) into
p1prog_net8.il, bound as msil-ld would bind it against the .NET 8 reference
pack: Marshal -> System.Runtime.InteropServices, Interlocked ->
System.Threading, everything else -> System.Runtime, all 8.0.0.0."""
src = open('p1prog.il', encoding='utf-8').read()
hdr = '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }'
assert hdr in src
new = ('.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }\n'
       '.assembly extern System.Runtime.InteropServices { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }\n'
       '.assembly extern System.Threading { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }')
s = src.replace(hdr, new)
s = s.replace('[mscorlib]System.Runtime.InteropServices.Marshal', '[System.Runtime.InteropServices]System.Runtime.InteropServices.Marshal')
s = s.replace('[mscorlib]System.Threading.Interlocked', '[System.Threading]System.Threading.Interlocked')
s = s.replace('[mscorlib]', '[System.Runtime]')
s = s.replace(".assembly 'p1prog'", ".assembly 'p1prog_net8'")
open('p1prog_net8.il', 'w', encoding='utf-8', newline='\n').write(s)
