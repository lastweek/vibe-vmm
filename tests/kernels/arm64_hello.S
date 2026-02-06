// Simple ARM64 kernel that writes to MMIO console
.text
.global _start

.equ MMIO_CONSOLE_BASE, 0x9000000
.equ UART_TX, 0

.data
message:
    .asciz "Hello from ARM64 VM!\r\n"

.text
_start:
    // Write message to console
    ldr     x0, =message
    ldr     x1, =MMIO_CONSOLE_BASE

write_loop:
    ldrb    w2, [x0], #1
    cbz     w2, done
    
    // Write to UART_TX
    strb    w2, [x1, #UART_TX]
    
    b       write_loop

done:
    // Infinite WFI loop
    wfi
    b       done
