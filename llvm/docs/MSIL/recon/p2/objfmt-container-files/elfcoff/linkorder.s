        .section .cil.text.twice,"ax",@progbits
        .globl twice
twice:  .byte 0x0a, 0x02, 0x2a
        .section .msil.method.twice,"o",@progbits,.cil.text.twice   # per-method side data, kept/dropped with the body
        .long twice                                                  # which symbol it describes (relocation)
        .byte 0x00, 0x01, 0x08, 0x08                                 # ECMA-335 MethodDefSig: default, 1 param, int32(int32)
        .section .cil.text.inl,"axG",@progbits,inl,comdat
        .weak inl
inl:    .byte 0x0a, 0x02, 0x2a
        .section .msil.method.inl,"oG",@progbits,.cil.text.inl,inl,comdat
        .long inl
        .byte 0x00, 0x01, 0x08, 0x08
        .section .note.msil,"",@note                                 # the full triple (EM_CIL loses arch and OS)
        .long 5, 28, 1
        .asciz "MSIL"
        .p2align 2
        .asciz "x86_64-pc-windows-msvc-msil"
        .p2align 2
