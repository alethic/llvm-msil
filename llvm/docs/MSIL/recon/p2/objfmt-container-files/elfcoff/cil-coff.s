# The same CIL "object" in a COFF container (x86_64 used only as a carrier).
        .section .cil$helper,"xr"
helper:
        .byte 0x12, 0x02, 0x17, 0x58, 0x2a
        .section .cil$twice,"xr"
        .globl twice
twice:
        .byte 0x42, 0x02, 0x28
        .long helper                # token relocation (IMAGE_REL_AMD64_TOKEN in MSVC /clr objects)
        .byte 0x18, 0x5a, 0x7e
        .long counter
        .byte 0x4a, 0x58, 0x2a
        .section .cil$inl,"xr",discard,inl   # COMDAT (IMAGE_COMDAT_SELECT_ANY)
        .globl inl
inl:
        .byte 0x0a, 0x02, 0x2a
        .section .cil$hid,"xr"
        .globl hid
hid:
        .byte 0x1e, 0x02, 0x28
        .long ext
        .byte 0x2a
        .data
        .globl counter
        .p2align 2
counter: .long 1
        .globl table
        .p2align 3
table:  .quad counter
        .quad twice
        .section .tls$,"dw"
        .globl tlsvar
tlsvar: .long 5
        .comm common_var,8,3
        .weak weak_undef
        .data
        .quad weak_undef
        .section .CRT$XCT00101,"dr"
        .quad twice
        .section .msil.sig,"dr"
        .asciz "twice"
