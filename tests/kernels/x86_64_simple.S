; x86_64 bare-metal test kernel for Vibe-VMM
; This is a minimal x86_64 kernel that can be used to test the VMM.
; It will work with:
; - KVM backend on Linux x86_64
; - HVF backend on Intel Macs

[BITS 64]

section .text

global _start

_start:
    cli                     ; Disable interrupts
    mov rsp, stack_top      ; Set up stack

loop:
    hlt                     ; HLT instruction - VM will exit here
    jmp loop                ; Infinite loop

section .data
signature db "VIBE-VMM"

section .bss
align 16
stack_bottom:
    resb 4096
stack_top:
