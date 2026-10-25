"""Prints the IR module of shortest-forms.ll's index checks: CIL argument and
local indices 3, 4, 255 and 256 need 257 parameters and 257 locals, which is
too long to write out by hand.

- @args has 257 int32 parameters and adds parameters 3, 4, 255 and 256:
  ldarg.3, ldarg.s 4, ldarg.s 255 and ldarg 256.
- @locals loads 257 volatile int32 values in its entry block and adds them in
  another block, which only one arm of a branch reaches (so the two blocks
  are not merged). Every value is live across the block boundary, none can be
  stackified and no two can share a local: 257 locals, stored in load order
  (stloc.0-3, stloc.s 4-255, stloc 256) and read in the same order (ldloc.0-3,
  ldloc.s 4-255, ldloc 256).
"""

N = 257

params = ", ".join("i32 %%a%d" % i for i in range(N))
print("define i32 @args(%s) {" % params)
print("  %x = add i32 %a3, %a4")
print("  %y = add i32 %x, %a255")
print("  %z = add i32 %y, %a256")
print("  ret i32 %z")
print("}")
print()
print("define i32 @locals(ptr %p, i1 %c) {")
print("entry:")
for i in range(N):
    print("  %%v%d = load volatile i32, ptr %%p" % i)
print("  br i1 %c, label %sum, label %none")
print("none:")
print("  ret i32 0")
print("sum:")
prev = "%v0"
for i in range(1, N):
    print("  %%s%d = add i32 %s, %%v%d" % (i, prev, i))
    prev = "%%s%d" % i
print("  ret i32 %s" % prev)
print("}")
