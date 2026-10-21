# startup-gen.py: option 4 cost, N one-file assemblies vs one assembly with the same N classes.
# Each unit: a public class with an address field, a function, and a type initializer that
# allocates 64 bytes (a data segment). Main calls every unit's function. Assemble with ilasm
# (many/u<i>.il -> u<i>.dll, many/main.il and one/main.il -> main.exe) and time with startup-bench.py.
import os
N = 200
hdr = '.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }\n'
def unit(i, cls):
    return f'''
.class public abstract sealed auto ansi '{cls}' extends [mscorlib]System.Object
{{
  .field public static initonly native int 'g{i}'
  .method public static int32 'f{i}'(int32) cil managed {{ .maxstack 2 ldarg.0 ldsfld native int '{cls}'::'g{i}' ldind.i4 add ret }}
  .method private specialname rtspecialname static void .cctor() cil managed
  {{ .maxstack 3 ldc.i4 64 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
     dup ldc.i4 {i} stind.i4 stsfld native int '{cls}'::'g{i}' ret }}
}}'''
os.makedirs('many', exist_ok=True); os.makedirs('one', exist_ok=True)
main_many = hdr + ''.join(f'.assembly extern u{i} {{ .ver 0:0:0:0 }}\n' for i in range(N)) + '.assembly mainmany { .ver 0:0:0:0 }\n'
main_one = hdr + '.assembly mainone { .ver 0:0:0:0 }\n'
body_many = ''.join(f"  ldc.i4.1 call int32 [u{i}]'S{i}'::'f{i}'(int32) add\n" for i in range(N))
body_one = ''.join(f"  ldc.i4.1 call int32 'S{i}'::'f{i}'(int32) add\n" for i in range(N))
mainm = '''.method private static int32 Main(string[] a) cil managed
{{ .entrypoint .maxstack 3 ldc.i4.0
{0}  ldc.i4 255 and ret }}
'''
open('many/main.il', 'w').write(main_many + mainm.format(body_many))
for i in range(N):
    open(f'many/u{i}.il', 'w').write(hdr + f'.assembly u{i} {{ .ver 0:0:0:0 }}\n' + unit(i, f'S{i}'))
open('one/main.il', 'w').write(main_one + ''.join(unit(i, f'S{i}') for i in range(N)) + mainm.format(body_one))
