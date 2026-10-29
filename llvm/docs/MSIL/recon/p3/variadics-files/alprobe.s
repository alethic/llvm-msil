	.text
	.byte 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
	.globl al_probe
	.type al_probe,@function
al_probe:
	movzbl %al, %eax
	retq
	.size al_probe, .-al_probe
	.p2align 8
	.globl al_probe_z
	.type al_probe_z,@function
al_probe_z:
	movzbl %al, %eax
	retq
	.size al_probe_z, .-al_probe_z
# double va_probe_*(double d): returns d the way a gcc-compiled variadic
# callee reads its first variadic double: XMM registers are saved only if
# AL != 0 (otherwise this returns 0.0).
	.p2align 8
	.globl va_probe_z
	.type va_probe_z,@function
va_probe_z:
	testb %al, %al
	je 1f
	retq
1:	xorps %xmm0, %xmm0
	retq
	.size va_probe_z, .-va_probe_z
	.p2align 6
	.globl va_probe_nz
	.type va_probe_nz,@function
va_probe_nz:
	testb %al, %al
	je 2f
	retq
2:	xorps %xmm0, %xmm0
	retq
	.size va_probe_nz, .-va_probe_nz
	.section .note.GNU-stack,"",@progbits
