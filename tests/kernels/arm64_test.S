// ARM64 test kernel with actual computation
// This kernel demonstrates basic ARM64 operations and VM exits

.text
.global _start

// Test data in memory
.data
test_value:
    .quad 42                  // Test value
counter:
    .quad 0                   // Loop counter
result:
    .quad 0                   // Result storage

.text
_start:
    // Set up stack pointer (assuming stack is already configured by VMM)
    // Load test value and perform operations
    
    // Load address of test_value into x0
    ldr     x0, =test_value
    
    // Load the value (42) into x1
    ldr     x1, [x0]
    
    // Add 10 to x1 -> x1 = 52
    add     x1, x1, #10
    
    // Multiply by 2 -> x1 = 104
    lsl     x1, x1, #1
    
    // Store result back to memory
    ldr     x0, =result
    str     x1, [x0]
    
    // Loop counter
    ldr     x0, =counter
    ldr     x1, [x0]           // Load counter
    add     x1, x1, #1         // Increment
    str     x1, [x0]           // Store back
    
    // Compare with threshold
    mov     x2, #5             // We'll loop 5 times
    cmp     x1, x2
    b.ge    done               // If counter >= 5, we're done
    
    // Not done yet - execute WFI to cause VM exit
    // This demonstrates multiple VM exits
    wfi                         // Wait For Interrupt (causes VM exit)
    b       _start              // Branch back to start

done:
    // We're done! Infinite loop with WFI
    wfi
    b       done

// Signature string for verification
.section __TEXT,__rodata
signature:
    .asciz "ARM64-VIBE-VMM-TEST"
