/*
 * HVF (macOS Hypervisor Framework) implementation
 *
 * IMPORTANT: This implementation requires an Intel-based Mac.
 * Apple Silicon (M1/M2/M3/etc.) Macs do NOT support x86_64 virtualization
 * via Hypervisor.framework. The framework on Apple Silicon only supports ARM64 VMs.
 *
 * For testing on Apple Silicon:
 * - Run Linux in a VM (UTM, Parallels, VMware Fusion) and use KVM backend
 * - Use an Intel Mac for x86_64 HVF support
 * - Use Apple's Virtualization framework for ARM64 VMs (completely different API)
 */

#include "hypervisor.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

/* Check if we're on x86_64 */
#if defined(__x86_64__)
#include <Hypervisor/hv.h>
#else
/* Stub for Apple Silicon */
#define HV_SUCCESS 0
typedef int hv_return_t;
typedef unsigned int hv_vcpuid_t;
typedef uint64_t hv_vm_options_t;
typedef uint64_t hv_vcpu_options_t;
typedef uint64_t hv_memory_flags_t;
typedef uint64_t hv_x86_reg_t;
#endif

/* HVF VM data */
struct hvf_vm_data {
    uint64_t mem_size;
    int vm_created;  /* Track if VM was actually created */
};

/* HVF vCPU data */
struct hvf_vcpu_data {
    hv_vcpuid_t vcpu;
    void *run;
    size_t run_size;
    int vcpu_created;  /* Track if vCPU was actually created */
};

/* Global HVF state */
static pthread_mutex_t hvf_lock = PTHREAD_MUTEX_INITIALIZER;

/* Forward declaration of ops table */
const struct hv_ops hvf_ops;

/* Register constants for x86_64 */
#if !defined(__x86_64__)
#define HV_X64_RIP   0
#define HV_X64_RFLAGS 1
#define HV_X64_RAX   2
#define HV_X64_RBX   5
#define HV_X64_RCX   3
#define HV_X64_RDX   4
#define HV_X64_RSI   6
#define HV_X64_RDI   7
#define HV_X64_RSP   8
#define HV_X64_RBP   9
#define HV_X64_R8    10
#define HV_X64_R9    11
#define HV_X64_R10   12
#define HV_X64_R11   13
#define HV_X64_R12   14
#define HV_X64_R13   15
#define HV_X64_R14   16
#define HV_X64_R15   17
#define HV_X64_CR0   36
#define HV_X64_CR2   38
#define HV_X64_CR3   39
#define HV_X64_CR4   40
#define HV_X64_EFER  999  /* Special case */
#define HV_X64_SEGMENT_CS  18
#define HV_X64_SEGMENT_DS  20
#define HV_X64_SEGMENT_ES  21
#define HV_X64_SEGMENT_FS  22
#define HV_X64_SEGMENT_GS  23
#define HV_X64_SEGMENT_SS  19

/* Segment structure for stub */
typedef struct {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint32_t access;
} hv_segment_t;
#endif

/* Memory flags */
#if !defined(__x86_64__)
#define HV_MEMORY_READ  1
#define HV_MEMORY_WRITE 2
#define HV_MEMORY_EXEC  4
#endif

/*
 * Initialize HVF
 */
static int hvf_init(void)
{
#if defined(__x86_64__)
    hv_return_t ret;

    /* Try to create and destroy a test VM to verify availability */
    ret = hv_vm_create(0);
    if (ret != HV_SUCCESS) {
        log_error("HVF not available: %d", ret);
        return -1;
    }

    /* Clean up test VM */
    hv_vm_destroy();

    log_info("HVF initialized (Hypervisor.framework x86_64)");
    return 0;
#else
    log_error("HVF x86_64 support requires an Intel-based Mac");
    log_error("Apple Silicon (M1/M2/M3) does not support x86_64 virtualization");
    log_error("For testing on Apple Silicon, use KVM backend in a Linux VM");
    return -1;
#endif
}

/*
 * Cleanup HVF
 */
static void hvf_cleanup(void)
{
    /* No global cleanup needed for HVF */
}

/*
 * Create VM
 */
static struct hv_vm* hvf_create_vm(int *fd)
{
    struct hv_vm *vm;
    struct hvf_vm_data *data;

    vm = calloc(1, sizeof(*vm));
    if (!vm) {
        log_error("Failed to allocate VM");
        return NULL;
    }

    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vm);
        return NULL;
    }

#if defined(__x86_64__)
    hv_return_t ret = hv_vm_create(0);
    if (ret != HV_SUCCESS) {
        log_error("Failed to create HVF VM: %d", ret);
        free(data);
        free(vm);
        return NULL;
    }
    data->vm_created = 1;
#else
    log_error("Cannot create HVF VM on Apple Silicon");
    log_error("HVF x86_64 only works on Intel-based Macs");
    free(data);
    free(vm);
    return NULL;
#endif

    data->mem_size = 0;

    vm->ops = &hvf_ops;
    vm->fd = -1;  /* HVF doesn't use file descriptors */
    vm->data = data;

    if (fd)
        *fd = -1;

    log_info("Created HVF VM");
    return vm;
}

/*
 * Destroy VM
 */
static void hvf_destroy_vm(struct hv_vm *vm)
{
    struct hvf_vm_data *data;

    if (!vm)
        return;

    data = vm->data;
    if (data) {
#if defined(__x86_64__)
        if (data->vm_created)
            hv_vm_destroy();
#endif
        free(data);
    }

    free(vm);
    log_info("Destroyed HVF VM");
}

/*
 * Get VM fd (not used in HVF)
 */
static int hvf_vm_get_fd(struct hv_vm *vm)
{
    (void)vm;
    return -1;
}

/*
 * Create vCPU
 */
static struct hv_vcpu* hvf_create_vcpu(struct hv_vm *vm, int index)
{
    struct hv_vcpu *vcpu;
    struct hvf_vcpu_data *data;

    vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) {
        log_error("Failed to allocate vCPU");
        return NULL;
    }

    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vcpu);
        return NULL;
    }

    /* Allocate vCPU run structure */
    data->run_size = 256;  /* Placeholder size */
    data->run = calloc(1, data->run_size);
    if (!data->run) {
        free(data);
        free(vcpu);
        return NULL;
    }

#if defined(__x86_64__)
    hv_return_t ret = hv_vcpu_create(&data->vcpu, 0);
    if (ret != HV_SUCCESS) {
        log_error("Failed to create HVF vCPU: %d", ret);
        free(data->run);
        free(data);
        free(vcpu);
        return NULL;
    }
    data->vcpu_created = 1;
#else
    /* Stub - mark vCPU as not created */
    data->vcpu = 0;
    data->vcpu_created = 0;
#endif

    vcpu->ops = &hvf_ops;
    vcpu->fd = -1;
    vcpu->vm = vm;
    vcpu->index = index;
    vcpu->data = data;

    log_info("Created HVF vCPU %d", index);
    return vcpu;
}

/*
 * Destroy vCPU
 */
static void hvf_destroy_vcpu(struct hv_vcpu *vcpu)
{
    struct hvf_vcpu_data *data;

    if (!vcpu)
        return;

    data = vcpu->data;
    if (data) {
        if (data->run)
            free(data->run);
#if defined(__x86_64__)
        if (data->vcpu_created)
            hv_vcpu_destroy(data->vcpu);
#endif
        free(data);
    }

    free(vcpu);
}

/*
 * Get vCPU fd (not used in HVF)
 */
static int hvf_vcpu_get_fd(struct hv_vcpu *vcpu)
{
    (void)vcpu;
    return -1;
}

/*
 * Map memory into VM
 */
static int hvf_map_mem(struct hv_vm *vm, struct hv_memory_slot *slot)
{
    (void)vm;

#if defined(__x86_64__)
    hv_return_t ret;
    hv_memory_flags_t flags;

    flags = HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC;

    ret = hv_vm_map((void*)(uintptr_t)slot->hva, slot->gpa, slot->size, flags);
    if (ret != HV_SUCCESS) {
        log_error("Failed to map memory: GPA 0x%lx -> HVA 0x%lx (size=%ld)",
                  slot->gpa, slot->hva, slot->size);
        return -1;
    }

    log_debug("Mapped memory: GPA 0x%lx -> HVA 0x%lx (size=%ld)",
              slot->gpa, slot->hva, slot->size);
    return 0;
#else
    log_error("Cannot map memory on Apple Silicon");
    return -1;
#endif
}

/*
 * Unmap memory from VM
 */
static int hvf_unmap_mem(struct hv_vm *vm, uint32_t slot)
{
    (void)vm;
    (void)slot;

    log_debug("Unmapped memory slot %d", slot);
    return 0;
}

/*
 * Run vCPU
 */
static int hvf_run(struct hv_vcpu *vcpu)
{
    struct hvf_vcpu_data *data = vcpu->data;

#if defined(__x86_64__)
    hv_return_t ret = hv_vcpu_run(data->vcpu);
    if (ret != HV_SUCCESS) {
        if (ret == HV_ERROR) {
            log_error("vCPU run error");
            return -1;
        }
        /* Other returns might be expected exits */
    }
    return 0;
#else
    log_error("Cannot run vCPU on Apple Silicon");
    return -1;
#endif
}

/*
 * Get exit information
 */
static int hvf_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit)
{
    (void)vcpu;

    /* Return default HLT exit */
    memset(exit, 0, sizeof(*exit));
    exit->reason = HV_EXIT_HLT;

    return 0;
}

/*
 * Get general registers
 */
static int hvf_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs)
{
    struct hvf_vcpu_data *data = vcpu->data;

#if defined(__x86_64__)
    hv_return_t ret;

    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RIP, &regs->rip);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RFLAGS, &regs->rflags);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RAX, &regs->rax);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RBX, &regs->rbx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RCX, &regs->rcx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RDX, &regs->rdx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RSI, &regs->rsi);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RDI, &regs->rdi);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RSP, &regs->rsp);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_RBP, &regs->rbp);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R8, &regs->r8);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R9, &regs->r9);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R10, &regs->r10);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R11, &regs->r11);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R12, &regs->r12);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R13, &regs->r13);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R14, &regs->r14);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_R15, &regs->r15);
    if (ret != HV_SUCCESS) return -1;

    return 0;
#else
    /* Stub - return zeros */
    memset(regs, 0, sizeof(*regs));
    return -1;
#endif
}

/*
 * Set general registers
 */
static int hvf_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs)
{
    struct hvf_vcpu_data *data = vcpu->data;

#if defined(__x86_64__)
    hv_return_t ret;

    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RIP, regs->rip);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RFLAGS, regs->rflags);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RAX, regs->rax);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RBX, regs->rbx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RCX, regs->rcx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RDX, regs->rdx);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RSI, regs->rsi);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RDI, regs->rdi);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RSP, regs->rsp);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_RBP, regs->rbp);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R8, regs->r8);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R9, regs->r9);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R10, regs->r10);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R11, regs->r11);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R12, regs->r12);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R13, regs->r13);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R14, regs->r14);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_R15, regs->r15);
    if (ret != HV_SUCCESS) return -1;

    return 0;
#else
    (void)regs;
    return -1;
#endif
}

/*
 * Get special registers
 */
static int hvf_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs)
{
    struct hvf_vcpu_data *data = vcpu->data;

#if defined(__x86_64__)
    hv_segment_t seg;
    hv_return_t ret;

    /* Code segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_CS, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->cs.selector = seg.selector;
    sregs->cs.base = seg.base;
    sregs->cs.limit = seg.limit;
    sregs->cs.ar = seg.access;

    /* Data segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_DS, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->ds.selector = seg.selector;
    sregs->ds.base = seg.base;
    sregs->ds.limit = seg.limit;
    sregs->ds.ar = seg.access;

    /* ES segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_ES, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->es.selector = seg.selector;
    sregs->es.base = seg.base;
    sregs->es.limit = seg.limit;
    sregs->es.ar = seg.access;

    /* FS segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_FS, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->fs.selector = seg.selector;
    sregs->fs.base = seg.base;
    sregs->fs.limit = seg.limit;
    sregs->fs.ar = seg.access;

    /* GS segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_GS, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->gs.selector = seg.selector;
    sregs->gs.base = seg.base;
    sregs->gs.limit = seg.limit;
    sregs->gs.ar = seg.access;

    /* SS segment */
    ret = hv_vcpu_read_segment(data->vcpu, HV_X64_SEGMENT_SS, &seg);
    if (ret != HV_SUCCESS) return -1;
    sregs->ss.selector = seg.selector;
    sregs->ss.base = seg.base;
    sregs->ss.limit = seg.limit;
    sregs->ss.ar = seg.access;

    /* Control registers */
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_CR0, &sregs->cr0);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_CR2, &sregs->cr2);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_CR3, &sregs->cr3);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_read_register(data->vcpu, HV_X64_CR4, &sregs->cr4);
    if (ret != HV_SUCCESS) return -1;

    /* EFER - special handling */
    sregs->efer = 0x1000;  /* Default EFER with LMA bit set */

    return 0;
#else
    /* Stub - return defaults */
    memset(sregs, 0, sizeof(*sregs));
    return -1;
#endif
}

/*
 * Set special registers
 */
static int hvf_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs)
{
    struct hvf_vcpu_data *data = vcpu->data;

#if defined(__x86_64__)
    hv_segment_t seg;
    hv_return_t ret;

    /* Code segment */
    seg.selector = sregs->cs.selector;
    seg.base = sregs->cs.base;
    seg.limit = sregs->cs.limit;
    seg.access = sregs->cs.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_CS, seg);
    if (ret != HV_SUCCESS) return -1;

    /* Data segment */
    seg.selector = sregs->ds.selector;
    seg.base = sregs->ds.base;
    seg.limit = sregs->ds.limit;
    seg.access = sregs->ds.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_DS, seg);
    if (ret != HV_SUCCESS) return -1;

    /* ES segment */
    seg.selector = sregs->es.selector;
    seg.base = sregs->es.base;
    seg.limit = sregs->es.limit;
    seg.access = sregs->es.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_ES, seg);
    if (ret != HV_SUCCESS) return -1;

    /* FS segment */
    seg.selector = sregs->fs.selector;
    seg.base = sregs->fs.base;
    seg.limit = sregs->fs.limit;
    seg.access = sregs->fs.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_FS, seg);
    if (ret != HV_SUCCESS) return -1;

    /* GS segment */
    seg.selector = sregs->gs.selector;
    seg.base = sregs->gs.base;
    seg.limit = sregs->gs.limit;
    seg.access = sregs->gs.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_GS, seg);
    if (ret != HV_SUCCESS) return -1;

    /* SS segment */
    seg.selector = sregs->ss.selector;
    seg.base = sregs->ss.base;
    seg.limit = sregs->ss.limit;
    seg.access = sregs->ss.ar;
    ret = hv_vcpu_write_segment(data->vcpu, HV_X64_SEGMENT_SS, seg);
    if (ret != HV_SUCCESS) return -1;

    /* Control registers */
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_CR0, sregs->cr0);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_CR2, sregs->cr2);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_CR3, sregs->cr3);
    if (ret != HV_SUCCESS) return -1;
    ret = hv_vcpu_write_register(data->vcpu, HV_X64_CR4, sregs->cr4);
    if (ret != HV_SUCCESS) return -1;

    /* EFER - skip for now, needs special handling */

    return 0;
#else
    (void)sregs;
    return -1;
#endif
}

/*
 * Assert/deassert IRQ line (HVF uses different mechanism)
 */
static int hvf_irq_line(struct hv_vm *vm, int irq, int level)
{
    (void)vm;
    (void)irq;
    (void)level;

    /* HVF uses different interrupt mechanism */
    log_warn("HVF irq_line not implemented");
    return 0;
}

/* HVF ops table (exported for hypervisor.c) */
const struct hv_ops hvf_ops = {
    .init = hvf_init,
    .cleanup = hvf_cleanup,

    .create_vm = hvf_create_vm,
    .destroy_vm = hvf_destroy_vm,
    .vm_get_fd = hvf_vm_get_fd,

    .create_vcpu = hvf_create_vcpu,
    .destroy_vcpu = hvf_destroy_vcpu,
    .vcpu_get_fd = hvf_vcpu_get_fd,

    .map_mem = hvf_map_mem,
    .unmap_mem = hvf_unmap_mem,

    .run = hvf_run,
    .get_exit = hvf_get_exit,

    .get_regs = hvf_get_regs,
    .set_regs = hvf_set_regs,

    .get_sregs = hvf_get_sregs,
    .set_sregs = hvf_set_sregs,

    .irq_line = hvf_irq_line,
};
