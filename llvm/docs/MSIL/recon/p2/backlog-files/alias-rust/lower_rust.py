"""Simulate the proposed MSILPrepareIR lowerings on rustc IR (experiment only):
- an external function alias becomes a forwarding definition (tail call to the aliasee);
- an empty 'asm sideeffect' with only inputs and clobbers becomes the D36 compiler
  fence + llvm.fake.use of each input + llvm.sideeffect;
- the Rust panic declaration becomes a trap stub (Phase 1 has no linker);
- a main() calling the exported functions is appended."""
import re, sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
m = re.search(r'^@(\w+) = (?:\w+ )*alias i32 \(ptr, i64\), ptr @(\w+)$', s, re.M)
alias, aliasee = m.group(1), m.group(2)
s = s.replace(m.group(0), '')
s += f'''
define i32 @{alias}(ptr %0, i64 %1) unnamed_addr {{
  %r = tail call i32 @{aliasee}(ptr %0, i64 %1)
  ret i32 %r
}}
'''
s, n = re.subn(r'call void asm sideeffect "", "r,~\{memory\}"\((ptr nonnull %\w+)\)[^\n]*',
              lambda mm: 'fence syncscope("singlethread") seq_cst\n  call void (...) @llvm.fake.use(' + mm.group(1) + ')\n  call void @llvm.sideeffect()', s)
print('asm replaced:', n, file=sys.stderr)
s = re.sub(r'^declare void (@_RNvNtNtCs\w+panic_const_div_by_zero)\(([^)]*)\)[^\n]*$',
           lambda mm: f'define void {mm.group(1)}(ptr %p) {{\n  call void @llvm.trap()\n  unreachable\n}}', s, flags=re.M)
s += '''
declare void @llvm.fake.use(...)
declare void @llvm.sideeffect()
declare void @llvm.trap()
define i32 @main() {
  %a = alloca i128, align 16
  %b = alloca i128, align 16
  store i128 1000000000000000000000000000007, ptr %a, align 16
  store i128 97, ptr %b, align 16
  %e = call i64 @entry(i64 5)
  %d = call <16 x i8> @div128(ptr %a, ptr %b)
  %di = bitcast <16 x i8> %d to i128
  %f = call <16 x i8> @f2i128(double -1.5e30)
  %fi = bitcast <16 x i8> %f to i128
  %x = xor i128 %di, %fi
  %e128 = zext i64 %e to i128
  %y = xor i128 %x, %e128
  %h = lshr i128 %y, 64
  %z = xor i128 %y, %h
  %t = trunc i128 %z to i32
  %u = and i32 %t, 127
  ret i32 %u
}
'''
open(dst, 'w').write(s)
