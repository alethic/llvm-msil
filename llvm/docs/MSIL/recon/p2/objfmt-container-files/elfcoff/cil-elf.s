# A CIL "object" in an ELF relocatable container (x86_64 used only as a carrier).
# Each function is a complete CIL method body (tiny header) in its own section,
# call/ldsfld tokens are 4-byte relocations against symbols, data is ordinary
# .data/.bss/.tdata with absolute relocations, C link semantics use ELF features.
        .section .cil.text.helper,"ax",@progbits
        .type helper,@function
helper:                             # static int helper(int y) { return y + 1; }
        .byte 0x12                  # tiny header: code size 4
        .byte 0x02, 0x17, 0x58, 0x2a  # ldarg.0 ldc.i4.1 add ret
        .size helper, .-helper

        .section .cil.text.twice,"ax",@progbits
        .globl twice
        .type twice,@function
twice:                              # int twice(int x) { return helper(x)*2 + *counter_addr + ext(x); }
        .byte 0x42                  # tiny header: code size 16
        .byte 0x02, 0x28            # ldarg.0 ; call <token>
        .long helper                # token relocation -> helper (R_CIL_TOKEN in a real e_machine)
        .byte 0x18, 0x5a            # ldc.i4.2 mul
        .byte 0x7e                  # ldsfld <token>
        .long counter               # token relocation -> address field of counter
        .byte 0x4a, 0x58            # ldind.i4 add
        .byte 0x2a                  # ret
        .size twice, .-twice

        .section .cil.text.inl,"axG",@progbits,inl,comdat
        .weak inl                   # C99/C++ inline: linkonce_odr -> COMDAT group
        .type inl,@function
inl:
        .byte 0x0a, 0x02, 0x2a      # ldarg.0 ret
        .size inl, .-inl

        .section .cil.text.hid,"ax",@progbits
        .globl hid
        .hidden hid                 # -fvisibility=hidden: not exported from a -shared assembly
        .type hid,@function
hid:
        .byte 0x1e, 0x02, 0x28      # tiny header (size 7), ldarg.0, call <token>
        .long ext                   # undefined function -> resolved by msil-ld (object, assembly or P/Invoke)
        .byte 0x2a
        .size hid, .-hid

        .data
        .globl counter
        .p2align 2
counter: .long 1
        .size counter, 4
        .globl table
        .p2align 3
table:  .quad counter               # data relocation (R_X86_64_64 here; R_CIL_ADDR64 in a real e_machine)
        .quad twice                 # function address in data
        .size table, 16

        .section .tdata,"awT",@progbits
        .globl tlsvar
tlsvar: .long 5                     # thread_local template
        .size tlsvar, 4
        .comm common_var,8,8        # tentative definition (-fcommon)
        .weak weak_undef            # undefined weak reference
        .section .data.rel,"aw",@progbits
        .quad weak_undef
        .section .init_array.101,"aw",@init_array
        .p2align 3
        .quad twice                 # constructor with priority 101
        .section .msil.sig,"",@progbits   # per-symbol CIL signatures (blob), the part ELF has no field for
        .asciz "twice\x00\x01\x08\x08"
        .section .note.GNU-stack,"",@progbits
