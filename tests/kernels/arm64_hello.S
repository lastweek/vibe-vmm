// ARM64 kernel that repeatedly prints "Hello World" with delays
//
// Position-independent code using movz/movk for raw binary loading
//
// To build: clang -arch arm64 -c arm64_hello.S -o arm64_hello.o
// Then extract instructions and create raw binary

.text
.global _start

.equ MMIO_CONSOLE_BASE, 0x90000000  // Must match device address in mmio.c

_start:
    // Load MMIO console base address into x1 (preserve this register)
    movz    x1, #0x0000
    movk    x1, #0x9000, lsl #16   // x1 = 0x90000000

print_loop:
    // Print "Hello World\n"

    // 'H' (0x48)
    mov     x0, #0x48
    strb    w0, [x1]

    // 'e' (0x65)
    mov     x0, #0x65
    strb    w0, [x1]

    // 'l' (0x6C)
    mov     x0, #0x6C
    strb    w0, [x1]

    // 'l' (0x6C)
    mov     x0, #0x6C
    strb    w0, [x1]

    // 'o' (0x6F)
    mov     x0, #0x6F
    strb    w0, [x1]

    // ' ' (0x20)
    mov     x0, #0x20
    strb    w0, [x1]

    // 'W' (0x57)
    mov     x0, #0x57
    strb    w0, [x1]

    // 'o' (0x6F)
    mov     x0, #0x6F
    strb    w0, [x1]

    // 'r' (0x72)
    mov     x0, #0x72
    strb    w0, [x1]

    // 'l' (0x6C)
    mov     x0, #0x6C
    strb    w0, [x1]

    // 'd' (0x64)
    mov     x0, #0x64
    strb    w0, [x1]

    // '!' (0x21)
    mov     x0, #0x21
    strb    w0, [x1]

    // '\n' (0x0A)
    mov     x0, #0x0A
    strb    w0, [x1]

    // '\r' (0x0D) for proper line ending
    mov     x0, #0x0D
    strb    w0, [x1]

    // Delay loop for approximately 5 seconds
    // On a ~3GHz CPU, we need to waste ~15 billion cycles
    // Use nested loops: outer * inner = ~15 billion

    mov     x2, #0x64        // Outer loop counter: 100 iterations
    mov     x3, #0x0F        // Inner loop multiplier
    movk    x3, #0xABCD, lsl #16  // x3 = ~1 billion (0xABCD000F)

delay_outer:
    mov     x4, x3           // Load inner loop count

delay_inner:
    // Do some dummy work to waste cycles
    sub     x4, x4, #1
    cmp     x4, #0
    b.ne    delay_inner

    sub     x2, x2, #1
    cmp     x2, #0
    b.ne    delay_outer

    // Branch back to print again
    b       print_loop

    // Never reached
halt:
    wfi
    b       halt
