// ARM64 timing test kernel
// Demonstrates long-running execution with periodic updates

.text
.global _start

// Data section
.data
main_counter:
    .quad 0
message_count:
    .quad 0
iterations_per_msg:
    .quad 10000000

// Status markers (loaded via literal pool)
status_ready:
    .quad 0
status_running:
    .quad 0
status_message:
    .quad 0

.text
_start:
    // Initialize status markers
    ldr     x0, =0xDEADBEEF
    ldr     x1, =status_ready
    str     x0, [x1]

    ldr     x0, =0xCAFEBABE
    ldr     x1, =status_running
    str     x0, [x1]

    ldr     x0, =0xF00DF00D
    ldr     x1, =status_message
    str     x0, [x1]

    // Initialize main counter to 0
    ldr     x0, =main_counter
    str     xzr, [x0]

main_loop:
    // Increment main counter
    ldr     x0, =main_counter
    ldr     x1, [x0]
    add     x1, x1, #1
    str     x1, [x0]

    // Check if we should update message counter
    ldr     x2, =iterations_per_msg
    ldr     x2, [x2]
    udiv    x3, x1, x2
    
    ldr     x4, =message_count
    ldr     x5, [x4]
    
    cmp     x3, x5
    b.eq    delay

    // Update message count
    str     x3, [x4]

delay:
    // Execute WFI to cause VM exit (yields to host)
    wfi
    
    // Additional delay loop
    mov     x0, #100
delay_loop:
    wfi
    subs    x0, x0, #1
    b.ne    delay_loop

    b       main_loop

// Infinite halt
halt:
    wfi
    b       halt

.section __TEXT,__rodata
signature:
    .asciz "ARM64-TIMING-TEST-01"
