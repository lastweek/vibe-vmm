/*
 * x86_64 HVF stub for ARM64 platforms
 *
 * This provides stub implementations so the VMM can build on ARM64,
 * even though x86_64 HVF is Intel-only.
 */

#include "hypervisor.h"
#include "utils.h"

#include <stdio.h>

#include <string.h>
static int hvf_stub_init(void)
{
    log_warn("x86_64 HVF backend is not available on this platform");
    log_warn("x86_64 HVF is Intel Mac only");
    return -1;
}

static void hvf_stub_cleanup(void)
{
}

static struct hv_vm* hvf_stub_create_vm(int *fd)
{
    (void)fd;
    log_error("x86_64 HVF is not available on this platform (Intel Mac only)");
    return NULL;
}

/* Stub all HVF operations */
static void hvf_stub_destroy_vm(struct hv_vm *vm) { (void)vm; }
static int hvf_stub_vm_get_fd(struct hv_vm *vm) { (void)vm; return -1; }
static struct hv_vcpu* hvf_stub_create_vcpu(struct hv_vm *vm, int index) { (void)vm; (void)index; return NULL; }
static void hvf_stub_destroy_vcpu(struct hv_vcpu *vcpu) { (void)vcpu; }
static int hvf_stub_vcpu_get_fd(struct hv_vcpu *vcpu) { (void)vcpu; return -1; }
static int hvf_stub_vcpu_exit(struct hv_vcpu *vcpu) { (void)vcpu; return 0; }
static int hvf_stub_map_mem(struct hv_vm *vm, struct hv_memory_slot *slot) { (void)vm; (void)slot; return -1; }
static int hvf_stub_unmap_mem(struct hv_vm *vm, uint32_t slot) { (void)vm; (void)slot; return 0; }
static int hvf_stub_run(struct hv_vcpu *vcpu) { (void)vcpu; return -1; }
static int hvf_stub_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit) { (void)vcpu; memset(exit, 0, sizeof(*exit)); return 0; }
static int hvf_stub_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs) { (void)vcpu; memset(regs, 0, sizeof(*regs)); return 0; }
static int hvf_stub_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs) { (void)vcpu; (void)regs; return 0; }
static int hvf_stub_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs) { (void)vcpu; memset(sregs, 0, sizeof(*sregs)); return 0; }
static int hvf_stub_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs) { (void)vcpu; (void)sregs; return 0; }
static int hvf_stub_irq_line(struct hv_vm *vm, int irq, int level) { (void)vm; (void)irq; (void)level; return 0; }

const struct hv_ops hvf_ops = {
    .init = hvf_stub_init,
    .cleanup = hvf_stub_cleanup,
    .create_vm = hvf_stub_create_vm,
    .destroy_vm = hvf_stub_destroy_vm,
    .vm_get_fd = hvf_stub_vm_get_fd,
    .create_vcpu = hvf_stub_create_vcpu,
    .destroy_vcpu = hvf_stub_destroy_vcpu,
    .vcpu_get_fd = hvf_stub_vcpu_get_fd,
    .vcpu_exit = hvf_stub_vcpu_exit,
    .map_mem = hvf_stub_map_mem,
    .unmap_mem = hvf_stub_unmap_mem,
    .run = hvf_stub_run,
    .get_exit = hvf_stub_get_exit,
    .get_regs = hvf_stub_get_regs,
    .set_regs = hvf_stub_set_regs,
    .get_sregs = hvf_stub_get_sregs,
    .set_sregs = hvf_stub_set_sregs,
    .irq_line = hvf_stub_irq_line,
};
