/*
 * ARM64 Architecture Definitions for Vibe-VMM
 *
 * This file contains ARM64-specific definitions for running VMs on Apple Silicon.
 */

#ifndef VIBE_VMM_ARCH_ARM64_H
#define VIBE_VMM_ARCH_ARM64_H

#include <stdint.h>

/* ARM64 General Purpose Registers */
struct arm64_regs {
    uint64_t x[31];      /* X0-X30 */
    uint64_t sp;         /* X31: Stack Pointer */
    uint64_t pc;         /* Program Counter */
    uint64_t pstate;     /* Processor State (flags) */
};

/* ARM64 System Registers */
struct arm64_sregs {
    uint64_t ttbr0_el1;  /* Translation Table Base Register 0 */
    uint64_t ttbr1_el1;  /* Translation Table Base Register 1 */
    uint64_t tcr_el1;    /* Translation Control Register */
    uint64_t sctlr_el1;  /* System Control Register */
    uint64_t actlr_el1;  /* Auxiliary Control Register */
    uint64_t cpsr;       /* Current Processor State Register */
    uint64_t sp_el0;     /* Stack Pointer for EL0 */
    uint64_t sp_el1;     /* Stack Pointer for EL1 */
    uint64_t elr_el1;    /* Exception Link Register for EL1 */
};

/* ARM64 Virtual Processor Info */
#define ARM64_VCPU_ID_MAX   255

/* ARM64 Memory Attributes */
#define ARM64_MEM_DEVICE    0x0
#define ARM64_MEM_NORMAL    0x1

/* ARM64 Page Table Definitions */
#define ARM64_PAGE_SHIFT       12
#define ARM64_PAGE_SIZE        (1ULL << ARM64_PAGE_SHIFT)
#define ARM64_PAGE_MASK        (~(ARM64_PAGE_SIZE - 1))

/* Exception levels */
#define ARM64_EL0            0
#define ARM64_EL1            1
#define ARM64_EL2            2
#define ARM64_EL3            3

/* ARM64 boot constants */
#define ARM64_BOOT_ADDR       0x40000000ULL
#define ARM64_STACK_ADDR      0x40000000ULL
#define ARM64_STACK_SIZE      0x10000  /* 64KB stack */
#define ARM64_DTB_ADDR        0x40080000ULL  /* Device tree blob */

/* ARM64 instruction helpers */
#define ARM64_INSN_HLT       0xD5037FFD  /* HLT instruction */
#define ARM64_INSN_RET       0xD65F03C0  /* RET instruction */
#define ARM64_INSN_NOP       0xD503201F  /* NOP instruction */

/* VCPU architecture type */
#define ARCH_ARM64            0x1
#define ARCH_X86_64           0x2

/* Detect host architecture at runtime */
#if defined(__aarch64__)
    #define HOST_ARCH_ARM64
#elif defined(__x86_64__)
    #define HOST_ARCH_X86_64
#else
    #error "Unsupported architecture"
#endif

#endif /* VIBE_VMM_ARCH_ARM64_H */
