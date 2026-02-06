/*
 * HVF (Hypervisor Framework) ARM64 Backend for Apple Silicon
 *
 * This file implements ARM64 virtualization using Apple's Hypervisor.framework.
 * It allows Vibe-VMM to run ARM64 virtual machines natively on Apple Silicon.
 *
 * Platform Requirements:
 *   - macOS 11.0+ (Big Sur or later)
 *   - Apple Silicon (M1/M2/M3/M4, etc.)
 *   - Hypervisor entitlement OR root privileges
 *
 * Usage:
 *   - Sign binary with com.apple.security.hypervisor entitlement, OR
 *   - Run with sudo: sudo ./bin/vibevmm ...
 *
 * Implementation Notes:
 *   - ARM64 VM exits are simpler than x86_64 (no segment registers, etc.)
 *   - WFI (Wait For Interrupt) instruction causes VM exits
 *   - Memory mapping uses GPA (Guest Physical Address) → HVA (Host Virtual Address)
 */

#include "hypervisor.h"
#include "utils.h"
#include "arch_arm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

/* Apple Hypervisor.framework headers */
#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vcpu_types.h>
#include <Hypervisor/hv_vcpu.h>
#include <Hypervisor/hv_vm.h>

/* Private VM data */
struct hvf_vm_data {
    uint64_t mem_size;
    int vm_created;
};

/* Private vCPU data */
struct hvf_vcpu_data {
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *exit_info;
    int vcpu_created;
};

/* Global mutex for thread safety */
static pthread_mutex_t hvf_lock = PTHREAD_MUTEX_INITIALIZER;

/* Forward declaration */
const struct hv_ops hvf_arm64_ops;

/* ============================================================
 * HVF Initialization
 * ============================================================ */

/**
 * hvf_arm64_init - Initialize Hypervisor.framework for ARM64
 *
 * Verifies that HVF is available by creating and destroying a test VM.
 * Returns 0 on success, -1 on failure.
 */
static int hvf_arm64_init(void)
{
    hv_return_t ret;

    /* Verify HVF is available by creating a test VM */
    ret = hv_vm_create(0);
    if (ret != HV_SUCCESS) {
        log_error("HVF initialization failed (error: %d)", ret);
        log_error("");
        log_error("On Apple Silicon, HVF requires proper authorization:");
        log_error("  Option 1: Sign the binary with hypervisor entitlement");
        log_error("    codesign --entitlements entitlements.plist --force --deep -s - bin/vibevmm");
        log_error("");
        log_error("  Option 2: Run with sudo privileges");
        log_error("    sudo ./bin/vibevmm [options...]");
        return -1;
    }

    /* Clean up test VM */
    hv_vm_destroy();

    log_info("HVF ARM64 initialized");
    return 0;
}

/**
 * hvf_arm64_cleanup - Cleanup HVF resources
 */
static void hvf_arm64_cleanup(void)
{
    /* No global cleanup needed for HVF */
}

/* ============================================================
 * VM Management
 * ============================================================ */

/**
 * hvf_arm64_create_vm - Create a new ARM64 virtual machine
 */
static struct hv_vm* hvf_arm64_create_vm(int *fd)
{
    struct hv_vm *vm;
    struct hvf_vm_data *data;
    hv_return_t ret;

    /* Allocate VM structure */
    vm = calloc(1, sizeof(*vm));
    if (!vm) {
        log_error("Failed to allocate VM structure");
        return NULL;
    }

    /* Allocate private data */
    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vm);
        return NULL;
    }

    /* Create VM using Hypervisor.framework */
    ret = hv_vm_create(0);
    if (ret != HV_SUCCESS) {
        log_error("Failed to create HVF VM: %d", ret);
        free(data);
        free(vm);
        return NULL;
    }

    data->mem_size = 0;
    data->vm_created = 1;

    vm->ops = &hvf_arm64_ops;
    vm->fd = -1;
    vm->data = data;

    if (fd)
        *fd = -1;

    log_info("Created ARM64 VM");
    return vm;
}

/**
 * hvf_arm64_destroy_vm - Destroy an ARM64 virtual machine
 */
static void hvf_arm64_destroy_vm(struct hv_vm *vm)
{
    struct hvf_vm_data *data;

    if (!vm)
        return;

    data = vm->data;
    if (data && data->vm_created) {
        hv_vm_destroy();
        free(data);
    }

    free(vm);
    log_info("Destroyed ARM64 VM");
}

/**
 * hvf_arm64_vm_get_fd - Get VM file descriptor (not used in HVF)
 */
static int hvf_arm64_vm_get_fd(struct hv_vm *vm)
{
    (void)vm;
    return -1;
}

/* ============================================================
 * vCPU Management
 * ============================================================ */

/**
 * hvf_arm64_create_vcpu - Create a new ARM64 virtual CPU
 */
static struct hv_vcpu* hvf_arm64_create_vcpu(struct hv_vm *vm, int index)
{
    struct hv_vcpu *vcpu;
    struct hvf_vcpu_data *data;
    hv_return_t ret;

    /* Allocate vCPU structure */
    vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) {
        log_error("Failed to allocate vCPU structure");
        return NULL;
    }

    /* Allocate private data */
    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vcpu);
        return NULL;
    }

    /* Allocate exit info structure */
    data->exit_info = calloc(1, sizeof(*data->exit_info));
    if (!data->exit_info) {
        free(data);
        free(vcpu);
        return NULL;
    }

    /* Create vCPU using Hypervisor.framework */
    ret = hv_vcpu_create(&data->vcpu, &data->exit_info, NULL);
    if (ret != HV_SUCCESS) {
        log_error("Failed to create ARM64 vCPU %d: %d", index, ret);
        free(data->exit_info);
        free(data);
        free(vcpu);
        return NULL;
    }

    data->vcpu_created = 1;

    vcpu->ops = &hvf_arm64_ops;
    vcpu->fd = -1;
    vcpu->vm = vm;
    vcpu->index = index;
    vcpu->data = data;

    log_info("Created ARM64 vCPU %d", index);
    return vcpu;
}

/**
 * hvf_arm64_destroy_vcpu - Destroy an ARM64 virtual CPU
 */
static void hvf_arm64_destroy_vcpu(struct hv_vcpu *vcpu)
{
    struct hvf_vcpu_data *data;

    if (!vcpu)
        return;

    data = vcpu->data;
    if (data) {
        if (data->vcpu_created)
            hv_vcpu_destroy(data->vcpu);
        if (data->exit_info)
            free(data->exit_info);
        free(data);
    }

    free(vcpu);
}

/**
 * hvf_arm64_vcpu_get_fd - Get vCPU file descriptor (not used in HVF)
 */
static int hvf_arm64_vcpu_get_fd(struct hv_vcpu *vcpu)
{
    (void)vcpu;
    return -1;
}

/* ============================================================
 * Memory Management
 * ============================================================ */

/**
 * hvf_arm64_map_mem - Map guest physical memory to host virtual memory
 */
static int hvf_arm64_map_mem(struct hv_vm *vm, struct hv_memory_slot *slot)
{
    (void)vm;
    hv_return_t ret;
    hv_memory_flags_t flags;

    /* Set memory permissions: read, write, execute */
    flags = HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC;

    /* Map memory region */
    ret = hv_vm_map((void *)(uintptr_t)slot->hva, slot->gpa, slot->size, flags);
    if (ret != HV_SUCCESS) {
        log_error("Failed to map memory: GPA 0x%llx -> HVA %p (size=%llu bytes)",
                  (unsigned long long)slot->gpa, (void *)(uintptr_t)slot->hva,
                  (unsigned long long)slot->size);
        return -1;
    }

    log_debug("Mapped memory: GPA 0x%llx -> HVA %p (size=%llu bytes)",
              (unsigned long long)slot->gpa, (void *)(uintptr_t)slot->hva,
              (unsigned long long)slot->size);
    return 0;
}

/**
 * hvf_arm64_unmap_mem - Unmap guest physical memory
 */
static int hvf_arm64_unmap_mem(struct hv_vm *vm, uint32_t slot)
{
    (void)vm;
    (void)slot;
    /* Memory unmapping is handled automatically on VM destroy */
    return 0;
}

/* ============================================================
 * vCPU Execution
 * ============================================================ */

/**
 * hvf_arm64_run - Run a virtual CPU until it exits
 */
static int hvf_arm64_run(struct hv_vcpu *vcpu)
{
    struct hvf_vcpu_data *data = vcpu->data;
    hv_return_t ret;

    /* Run vCPU - blocks until VM exit */
    ret = hv_vcpu_run(data->vcpu);

    /* Check for errors */
    if (ret != HV_SUCCESS) {
        if (ret == HV_ERROR) {
            log_error("vCPU run error");
            return -1;
        }
    }

    return 0;
}

/**
 * hvf_arm64_get_exit - Get information about why the vCPU exited
 *
 * For ARM64, exits can be due to:
 * - WFI instruction (treated as HLT)
 * - Exception (data abort to unmapped MMIO)
 * - Virtual timer
 * - Canceled (async request)
 */
static int hvf_arm64_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit)
{
    struct hvf_vcpu_data *data = vcpu->data;

    /* For ARM64 on Apple Silicon, hv_vcpu_run() returns when the vCPU exits.
     * The exit_info structure contains the reason for the exit.
     */

    memset(exit, 0, sizeof(*exit));

    /* Check the actual exit reason from Apple HVF */
    if (data->exit_info) {
        hv_exit_reason_t reason = data->exit_info->reason;

        switch (reason) {
        case HV_EXIT_REASON_CANCELED:
            exit->reason = HV_EXIT_CANCELED;
            log_debug("VM exit: CANCELED (async request)");
            break;

        case HV_EXIT_REASON_EXCEPTION:
            /* Exception - could be MMIO data abort or other exception */
            exit->reason = HV_EXIT_EXCEPTION;

            /* Check if this is a data abort (might be MMIO access) */
            /* For now, treat as exception and let upper layers handle it */
            log_debug("VM exit: EXCEPTION (syndrome=0x%llx, addr=0x%llx)",
                     (unsigned long long)data->exit_info->exception.syndrome,
                     (unsigned long long)data->exit_info->exception.virtual_address);

            /* If the fault address is in device region, treat as MMIO */
            if (data->exit_info->exception.virtual_address != 0) {
                exit->reason = HV_EXIT_MMIO;
                exit->u.mmio.addr = data->exit_info->exception.physical_address;
                exit->u.mmio.size = 4;  /* Default to 4 bytes */
                exit->u.mmio.is_write = 1;  /* Assume write for now */
                exit->u.mmio.data = 0;
                log_debug("VM exit: MMIO access at GPA 0x%llx",
                         (unsigned long long)exit->u.mmio.addr);
            }
            break;

        case HV_EXIT_REASON_VTIMER_ACTIVATED:
            exit->reason = HV_EXIT_VTIMER;
            log_debug("VM exit: VTIMER activated");
            break;

        case HV_EXIT_REASON_UNKNOWN:
        default:
            /* Unknown or WFI - treat as HLT for our simple test kernels */
            exit->reason = HV_EXIT_HLT;
            log_debug("VM exit: treating as HLT/WFI");
            break;
        }
    } else {
        /* No exit info - assume HLT */
        exit->reason = HV_EXIT_HLT;
        log_debug("VM exit: no exit info, treating as HLT");
    }

    return 0;
}

/* ============================================================
 * Register Management
 * ============================================================ */

/**
 * hvf_arm64_get_regs - Get ARM64 general-purpose registers
 *
 * Note: ARM64 registers (X0-X30, SP, PC, PSTATE) are different from x86_64.
 * This is a placeholder for future implementation.
 */
static int hvf_arm64_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs)
{
    (void)vcpu;
    (void)regs;
    /* TODO: Implement ARM64 register reads using hv_vcpu_get_reg() */
    return 0;
}

/**
 * hvf_arm64_set_regs - Set ARM64 general-purpose registers
 *
 * For ARM64, we primarily need to set the PC (program counter) to the entry point.
 * Other registers can start at 0.
 */
static int hvf_arm64_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs)
{
    struct hvf_vcpu_data *data = vcpu->data;
    hv_return_t ret;

    if (!data || !data->vcpu_created) {
        return -1;
    }

    /* Set PC (Program Counter) to the entry point */
    /* regs.rip contains the entry point address */
    ret = hv_vcpu_set_reg(data->vcpu, HV_REG_PC, regs->rip);
    if (ret != HV_SUCCESS) {
        log_error("Failed to set PC register: %d", ret);
        return -1;
    }

    /* Set CPSR (Current Program Status Register) */
    /* Start in EL1 (hypervisor mode) with interrupts disabled */
    ret = hv_vcpu_set_reg(data->vcpu, HV_REG_CPSR, 0x3C5);  /* EL1h, IRQ/FIQ masked */
    if (ret != HV_SUCCESS) {
        log_warn("Failed to set CPSR register: %d", ret);
        /* Continue anyway - CPSR might not be critical */
    }

    log_debug("Set ARM64 PC=0x%llx, CPSR=0x3c5", (unsigned long long)regs->rip);
    return 0;
}

/**
 * hvf_arm64_get_sregs - Get ARM64 system registers
 *
 * Note: ARM64 system registers (TTBR0_EL1, TCR_EL1, etc.) are different
 * from x86_64 control registers. This is a placeholder for future implementation.
 */
static int hvf_arm64_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs)
{
    (void)vcpu;
    (void)sregs;
    /* TODO: Implement ARM64 system register reads */
    return 0;
}

/**
 * hvf_arm64_set_sregs - Set ARM64 system registers
 *
 * Note: ARM64 system registers (TTBR0_EL1, TCR_EL1, etc.) are different
 * from x86_64 control registers. This is a placeholder for future implementation.
 */
static int hvf_arm64_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs)
{
    (void)vcpu;
    (void)sregs;
    /* ARM64 doesn't have x86 segments - ignore */
    log_debug("Ignoring x86 sregs for ARM64");
    return 0;
}

/* ============================================================
 * Interrupt Management
 * ============================================================ */

/**
 * hvf_arm64_irq_line - Assert/deassert IRQ line
 *
 * Note: ARM64 interrupt handling is different from x86_64.
 * This uses GIC (Generic Interrupt Controller) instead of APIC.
 */
static int hvf_arm64_irq_line(struct hv_vm *vm, int irq, int level)
{
    (void)vm;
    (void)irq;
    (void)level;

    log_warn("HVF ARM64 irq_line not implemented");
    return 0;
}

/* ============================================================
 * HVF Operations Table
 * ============================================================ */

/* Exported operations table for ARM64 HVF backend */
const struct hv_ops hvf_arm64_ops = {
    .init = hvf_arm64_init,
    .cleanup = hvf_arm64_cleanup,

    .create_vm = hvf_arm64_create_vm,
    .destroy_vm = hvf_arm64_destroy_vm,
    .vm_get_fd = hvf_arm64_vm_get_fd,

    .create_vcpu = hvf_arm64_create_vcpu,
    .destroy_vcpu = hvf_arm64_destroy_vcpu,
    .vcpu_get_fd = hvf_arm64_vcpu_get_fd,

    .map_mem = hvf_arm64_map_mem,
    .unmap_mem = hvf_arm64_unmap_mem,

    .run = hvf_arm64_run,
    .get_exit = hvf_arm64_get_exit,

    .get_regs = hvf_arm64_get_regs,
    .set_regs = hvf_arm64_set_regs,

    .get_sregs = hvf_arm64_get_sregs,
    .set_sregs = hvf_arm64_set_sregs,

    .irq_line = hvf_arm64_irq_line,
};
