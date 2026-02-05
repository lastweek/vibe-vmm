// ARM64 bare-metal test kernel (raw binary format)
// This creates a simple infinite loop that will cause VM exits

.text
.global _start

_start:
    // Infinite loop with WFI (Wait For Interrupt)
    wfi                     // Wait for interrupt (causes VM exit)
    b _start                // Branch back to start
