; Multiboot2 header with framebuffer request (1680x1050x32)
MB2_MAGIC    equ 0xE85250D6
MB2_ARCH     equ 0           ; i386
MB2_CHECKSUM equ -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))

section .multiboot2
align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd (mb2_header_end - mb2_header_start)
    dd MB2_CHECKSUM

    ; Framebuffer tag (type 5)
    align 8
    dw 5            ; type: framebuffer
    dw 0            ; flags
    dd 20           ; size
    dd 1920         ; width
    dd 1080         ; height
    dd 32           ; depth (bpp)

    ; End tag
    align 8
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .bss
align 16
stack_bottom:
    resb 32768      ; 32 KB stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    push 0          ; padding
    push ebx        ; Multiboot2 info pointer
    push eax        ; Multiboot2 magic (0x36D76289)
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang
