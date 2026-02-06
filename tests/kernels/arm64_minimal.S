// Ultra-minimal ARM64 kernel - NO literal pools
// Uses absolute addresses and immediate values only

.text
.global _start

.equ MMIO_CONSOLE_BASE, 0x9000000

_start:
    // Load the MMIO base address: 0x9000000 = 0x0090_0000 = 144 MB
    // 0x0090_0000 means bits 16-31 are 0x9000, all other bits are 0
    movz    x1, #0x0000             // Load 0x0000 into bits 0-15: x1 = 0x0000_0000_0000_0000
    movk    x1, #0x9000, lsl #16   // Insert 0x9000 into bits 16-31: x1 = 0x0000_0000_0900_0000

    // Write 'H' (0x48)
    mov     x0, #0x48
    strb    w0, [x1]

    // Write 'i' (0x69)
    mov     x0, #0x69
    strb    w0, [x1]

    // Halt loop
halt:
    wfi
    b       halt
