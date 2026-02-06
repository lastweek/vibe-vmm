/*
 * Hypervisor API wrapper
 *
 * This file provides the hypervisor abstraction layer that selects
 * the appropriate backend (KVM, HVF, etc.) based on the platform.
 * It now auto-detects the host architecture and uses the appropriate backend.
 */

#include "hypervisor.h"
#include "utils.h"
#include "arch_arm64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* External hypervisor ops tables */
extern const struct hv_ops kvm_ops;
extern const struct hv_ops hvf_ops;
extern const struct hv_ops hvf_arm64_ops;

/* Current hypervisor ops */
static const struct hv_ops *g_hv_ops = NULL;

/*
 * Initialize hypervisor (auto-detect platform and architecture)
 */
int hv_init(enum hv_type type)
{
    int ret;

    /* Auto-detect if HV_TYPE_AUTO is specified */
    if (type == HV_TYPE_AUTO) {
#if defined(__linux__)
        /* Linux: Use KVM (x86_64) */
        log_info("Auto-detected: Linux with KVM");
        type = HV_TYPE_KVM;
#elif defined(__APPLE__)
        /* macOS: Use HVF, detect architecture */
#if defined(__aarch64__)
        /* Apple Silicon: Use ARM64 HVF */
        log_info("Auto-detected: Apple Silicon (ARM64) with HVF");
        type = HV_TYPE_HVF_ARM64;
#elif defined(__x86_64__)
        /* Intel Mac: Use x86_64 HVF */
        log_info("Auto-detected: Intel Mac (x86_64) with HVF");
        type = HV_TYPE_HVF_X86_64;
#else
#error "Unsupported macOS architecture"
#endif
#else
#error "Unsupported platform"
#endif
    }

    /* Select hypervisor ops based on type */
    switch (type) {
    case HV_TYPE_KVM:
        g_hv_ops = &kvm_ops;
        break;

    case HV_TYPE_HVF_X86_64:
        g_hv_ops = &hvf_ops;
        break;

    case HV_TYPE_HVF_ARM64:
        g_hv_ops = &hvf_arm64_ops;
        break;

    case HV_TYPE_HVF:
        /* Legacy HVF - select based on host arch */
#ifdef __x86_64__
        g_hv_ops = &hvf_ops;
#else
        g_hv_ops = &hvf_arm64_ops;
#endif
        break;

    default:
        log_error("Unknown hypervisor type: %d", type);
        return -1;
    }

    if (!g_hv_ops) {
        log_error("Hypervisor ops not set");
        return -1;
    }

    if (g_hv_ops->init) {
        ret = g_hv_ops->init();
        if (ret < 0) {
            log_error("Hypervisor init failed");
            g_hv_ops = NULL;
            return ret;
        }
    }

    log_info("Hypervisor initialized");
    return 0;
}

/*
 * Cleanup hypervisor
 */
void hv_cleanup(void)
{
    if (g_hv_ops && g_hv_ops->cleanup)
        g_hv_ops->cleanup();

    g_hv_ops = NULL;
}

/*
 * Get hypervisor ops
 */
const struct hv_ops* hv_get_ops(void)
{
    return g_hv_ops;
}

/*
 * Create VM
 */
struct hv_vm* hv_create_vm(void)
{
    if (!g_hv_ops || !g_hv_ops->create_vm)
        return NULL;

    return g_hv_ops->create_vm(NULL);
}

/*
 * Destroy VM
 */
void hv_destroy_vm(struct hv_vm *vm)
{
    if (!g_hv_ops || !g_hv_ops->destroy_vm)
        return;

    g_hv_ops->destroy_vm(vm);
}

/*
 * Get VM fd
 */
int hv_vm_get_fd(struct hv_vm *vm)
{
    if (!g_hv_ops || !g_hv_ops->vm_get_fd)
        return -1;

    return g_hv_ops->vm_get_fd(vm);
}

/*
 * Create vCPU
 */
struct hv_vcpu* hv_create_vcpu(struct hv_vm *vm, int index)
{
    if (!g_hv_ops || !g_hv_ops->create_vcpu)
        return NULL;

    return g_hv_ops->create_vcpu(vm, index);
}

/*
 * Destroy vCPU
 */
void hv_destroy_vcpu(struct hv_vcpu *vcpu)
{
    if (!g_hv_ops || !g_hv_ops->destroy_vcpu)
        return;

    g_hv_ops->destroy_vcpu(vcpu);
}

/*
 * Get vCPU fd
 */
int hv_vcpu_get_fd(struct hv_vcpu *vcpu)
{
    if (!g_hv_ops || !g_hv_ops->vcpu_get_fd)
        return -1;

    return g_hv_ops->vcpu_get_fd(vcpu);
}

int hv_vcpu_exit(struct hv_vcpu *vcpu)
{
    if (!g_hv_ops)
        return -1;

    /* Only ARM64/HVF needs explicit exit request */
    if (g_hv_ops->vcpu_exit)
        return g_hv_ops->vcpu_exit(vcpu);

    return 0;  /* No-op for backends that don't need it */
}

/*
 * Map memory into VM
 */
int hv_map_mem(struct hv_vm *vm, uint32_t slot, uint64_t gpa, void *hva, uint64_t size)
{
    struct hv_memory_slot mem_slot;

    if (!g_hv_ops || !g_hv_ops->map_mem)
        return -1;

    mem_slot.slot = slot;
    mem_slot.gpa = gpa;
    mem_slot.hva = hva;
    mem_slot.size = size;
    mem_slot.flags = 0;

    return g_hv_ops->map_mem(vm, &mem_slot);
}

/*
 * Unmap memory from VM
 */
int hv_unmap_mem(struct hv_vm *vm, uint32_t slot)
{
    if (!g_hv_ops || !g_hv_ops->unmap_mem)
        return -1;

    return g_hv_ops->unmap_mem(vm, slot);
}

/*
 * Run vCPU
 */
int hv_run(struct hv_vcpu *vcpu)
{
    if (!g_hv_ops || !g_hv_ops->run)
        return -1;

    return g_hv_ops->run(vcpu);
}

/*
 * Get exit info
 */
int hv_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit)
{
    if (!g_hv_ops || !g_hv_ops->get_exit)
        return -1;

    return g_hv_ops->get_exit(vcpu, exit);
}

/*
 * Get general registers
 */
int hv_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs)
{
    if (!g_hv_ops || !g_hv_ops->get_regs)
        return -1;

    return g_hv_ops->get_regs(vcpu, regs);
}

/*
 * Set general registers
 */
int hv_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs)
{
    if (!g_hv_ops || !g_hv_ops->set_regs)
        return -1;

    return g_hv_ops->set_regs(vcpu, regs);
}

/*
 * Get special registers
 */
int hv_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs)
{
    if (!g_hv_ops || !g_hv_ops->get_sregs)
        return -1;

    return g_hv_ops->get_sregs(vcpu, sregs);
}

/*
 * Set special registers
 */
int hv_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs)
{
    if (!g_hv_ops || !g_hv_ops->set_sregs)
        return -1;

    return g_hv_ops->set_sregs(vcpu, sregs);
}

/*
 * Assert/deassert IRQ line
 */
int hv_irq_line(struct hv_vm *vm, int irq, int level)
{
    if (!g_hv_ops || !g_hv_ops->irq_line)
        return -1;

    return g_hv_ops->irq_line(vm, irq, level);
}
