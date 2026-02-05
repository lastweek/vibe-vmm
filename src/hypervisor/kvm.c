/*
 * KVM hypervisor backend implementation for Linux
 *
 * This file implements the hypervisor abstraction layer using the Linux KVM API.
 */

#include "hypervisor.h"
#include "utils.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

/* KVM ioctl base */
#define KVMIO 0xAE

/* KVM API version */
#define KVM_API_VERSION 12

/* KVM API headers (from kernel) */
#define KVM_GET_API_VERSION       _IO(KVMIO, 0x00)
#define KVM_CREATE_VM             _IO(KVMIO, 0x01)
#define KVM_GET_VCPU_MMAP_SIZE    _IO(KVMIO, 0x04)
#define KVM_CREATE_VCPU           _IO(KVMIO, 0x41)
#define KVM_GET_REGS              _IOR(KVMIO, 0x81, struct kvm_regs)
#define KVM_SET_REGS              _IOW(KVMIO, 0x82, struct kvm_regs)
#define KVM_GET_SREGS             _IOR(KVMIO, 0x83, struct kvm_sregs)
#define KVM_SET_SREGS             _IOW(KVMIO, 0x84, struct kvm_sregs)
#define KVM_RUN                   _IO(KVMIO, 0x80)
#define KVM_SET_USER_MEMORY_REGION _IOW(KVMIO, 0x46, struct kvm_userspace_memory_region)
#define KVM_IRQ_LINE              _IOW(KVMIO, 0x61, struct kvm_irq_level)
#define KVM_SET_MSRS              _IOW(KVMIO, 0x89, struct kvm_msrs)
#define KVM_GET_MSRS              _IOW(KVMIO, 0x88, struct kvm_msrs)
#define KVM_GET_CPUID2            _IOWR(KVMIO, 0x91, struct kvm_cpuid2)
#define KVM_SET_CPUID2            _IOW(KVMIO, 0x8a, struct kvm_cpuid2)

/* KVM exit reasons */
#define KVM_EXIT_UNKNOWN          0
#define KVM_EXIT_EXCEPTION        1
#define KVM_EXIT_IO               2
#define KVM_EXIT_HLT              5
#define KVM_EXIT_MMIO             6
#define KVM_EXIT_IRQ_WINDOW_OPEN  7
#define KVM_EXIT_SHUTDOWN         8
#define KVM_EXIT_FAIL_ENTRY       9
#define KVM_EXIT_INTR             10
#define KVM_EXIT_SET_TPR          11
#define KVM_EXIT_TPR_ACCESS       12
#define KVM_EXIT_S390_SIEIC       13
#define KVM_EXIT_S390_RESET       14
#define KVM_EXIT_DCR              15  /* deprecated */
#define KVM_EXIT_NMI              16
#define KVM_EXIT_INTERNAL_ERROR   17
#define KVM_EXIT_OSI              18
#define KVM_EXIT_PAPR_HCALL      19
#define KVM_EXIT_S390_UCONTROL   20
#define KVM_EXIT_WATCHDOG        21
#define KVM_EXIT_S390_TSCH      22
#define KVM_EXIT_EPR            23
#define KVM_EXIT_SYSTEM_EVENT   24
#define KVM_EXIT_S390_STSI      25
#define KVM_EXIT_IOAPIC_EOI     26
#define KVM_EXIT_HYPERV         27

/* KVM memory region flags */
#define KVM_MEM_LOG_DIRTY_PAGES  (1UL << 0)
#define KVM_MEM_READONLY         (1UL << 1)

/* KVM structures */
struct kvm_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
};

struct kvm_sregs {
    struct {
        uint64_t base;
        uint32_t limit;
        uint16_t selector;
        uint16_t ar;
    } cs, ds, es, fs, gs, ss, ldt, tr;
    struct {
        uint64_t base;
        uint32_t limit;
        uint32_t ar;
    } gdt, idt;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
    uint64_t interrupt_bitmap[16];
};

struct kvm_segment {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint16_t type;
    uint8_t  present, dpl, db, s, l, g, avl;
    uint8_t  unusable;
    uint8_t  padding;
};

struct kvm_dtable {
    uint64_t base;
    uint16_t limit;
    uint16_t padding[3];
};

struct kvm_userspace_memory_region {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
};

struct kvm_irq_level {
    union {
        uint32_t irq;
        uint32_t status;
    };
    uint32_t level;
};

struct kvm_run {
    uint8_t request_interrupt_window;
    uint8_t padding1[7];
    uint32_t exit_reason;
    uint8_t ready_for_interrupt_injection;
    uint8_t if_flag;
    uint8_t padding2[2];

    union {
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
        } mmio;
        struct {
            uint64_t phys_addr;
            uint64_t data;
            uint32_t len;
            uint8_t  is_write;
        } mmio_ex;
        struct {
            uint16_t direction;
            uint16_t size;
            uint16_t port;
            uint16_t count;
            uint64_t data_offset;
        } io;
        struct {
            struct {
                uint64_t addr;
                uint64_t length;
                uint8_t  is_write;
            } data[2];
            uint32_t ndata;
            uint32_t flags;
        } internal;
    } u;
};

/* KVM backend data */
struct kvm_vm_data {
    int vcpu_mmap_size;
};

struct kvm_vcpu_data {
    struct kvm_run *run;
};

/* Global KVM fd */
static int kvm_fd = -1;

/* KVM operations */
static int kvm_init(void);
static void kvm_cleanup(void);

static struct hv_vm* kvm_create_vm(int *fd);
static void kvm_destroy_vm(struct hv_vm *vm);
static int kvm_vm_get_fd(struct hv_vm *vm);

static struct hv_vcpu* kvm_create_vcpu(struct hv_vm *vm, int index);
static void kvm_destroy_vcpu(struct hv_vcpu *vcpu);
static int kvm_vcpu_get_fd(struct hv_vcpu *vcpu);

static int kvm_map_mem(struct hv_vm *vm, struct hv_memory_slot *slot);
static int kvm_unmap_mem(struct hv_vm *vm, uint32_t slot);

static int kvm_run(struct hv_vcpu *vcpu);
static int kvm_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit);

static int kvm_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs);
static int kvm_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs);

static int kvm_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs);
static int kvm_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs);

static int kvm_irq_line(struct hv_vm *vm, int irq, int level);

/* KVM ops table */
const struct hv_ops kvm_ops = {
    .init = kvm_init,
    .cleanup = kvm_cleanup,

    .create_vm = kvm_create_vm,
    .destroy_vm = kvm_destroy_vm,
    .vm_get_fd = kvm_vm_get_fd,

    .create_vcpu = kvm_create_vcpu,
    .destroy_vcpu = kvm_destroy_vcpu,
    .vcpu_get_fd = kvm_vcpu_get_fd,

    .map_mem = kvm_map_mem,
    .unmap_mem = kvm_unmap_mem,

    .run = kvm_run,
    .get_exit = kvm_get_exit,

    .get_regs = kvm_get_regs,
    .set_regs = kvm_set_regs,

    .get_sregs = kvm_get_sregs,
    .set_sregs = kvm_set_sregs,

    .irq_line = kvm_irq_line,
};

/*
 * Initialize KVM
 */
static int kvm_init(void)
{
    int api_version;

    kvm_fd = open("/dev/kvm", O_RDWR);
    if (kvm_fd < 0) {
        perror("open /dev/kvm");
        return -1;
    }

    api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0) {
        perror("KVM_GET_API_VERSION");
        close(kvm_fd);
        kvm_fd = -1;
        return -1;
    }

    if (api_version != KVM_API_VERSION) {
        fprintf(stderr, "KVM API version mismatch: got %d, expected %d\n",
                api_version, KVM_API_VERSION);
        close(kvm_fd);
        kvm_fd = -1;
        return -1;
    }

    log_info("KVM initialized (API version %d)", api_version);
    return 0;
}

/*
 * Cleanup KVM
 */
static void kvm_cleanup(void)
{
    if (kvm_fd >= 0) {
        close(kvm_fd);
        kvm_fd = -1;
    }
}

/*
 * Create a VM
 */
static struct hv_vm* kvm_create_vm(int *fd)
{
    struct hv_vm *vm;
    struct kvm_vm_data *data;
    int vm_fd;

    vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("KVM_CREATE_VM");
        return NULL;
    }

    vm = calloc(1, sizeof(*vm));
    if (!vm) {
        close(vm_fd);
        return NULL;
    }

    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vm);
        close(vm_fd);
        return NULL;
    }

    data->vcpu_mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (data->vcpu_mmap_size <= 0) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        free(data);
        free(vm);
        close(vm_fd);
        return NULL;
    }

    vm->ops = &kvm_ops;
    vm->fd = vm_fd;
    vm->data = data;

    if (fd)
        *fd = vm_fd;

    log_debug("KVM VM created (fd=%d, mmap_size=%d)", vm_fd, data->vcpu_mmap_size);
    return vm;
}

/*
 * Destroy a VM
 */
static void kvm_destroy_vm(struct hv_vm *vm)
{
    if (!vm)
        return;

    if (vm->data)
        free(vm->data);

    if (vm->fd >= 0)
        close(vm->fd);

    free(vm);
}

/*
 * Get VM fd
 */
static int kvm_vm_get_fd(struct hv_vm *vm)
{
    return vm->fd;
}

/*
 * Create a vCPU
 */
static struct hv_vcpu* kvm_create_vcpu(struct hv_vm *vm, int index)
{
    struct hv_vcpu *vcpu;
    struct kvm_vcpu_data *data;
    int vcpu_fd;
    struct kvm_run *run;

    vcpu_fd = ioctl(vm->fd, KVM_CREATE_VCPU, index);
    if (vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        return NULL;
    }

    vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) {
        close(vcpu_fd);
        return NULL;
    }

    data = calloc(1, sizeof(*data));
    if (!data) {
        free(vcpu);
        close(vcpu_fd);
        return NULL;
    }

    run = mmap(NULL, ((struct kvm_vm_data *)vm->data)->vcpu_mmap_size,
               PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
    if (run == MAP_FAILED) {
        perror("mmap vcpu run");
        free(data);
        free(vcpu);
        close(vcpu_fd);
        return NULL;
    }

    vcpu->ops = &kvm_ops;
    vcpu->fd = vcpu_fd;
    vcpu->vm = vm;
    vcpu->index = index;
    vcpu->data = data;
    data->run = run;

    log_debug("KVM vCPU %d created (fd=%d)", index, vcpu_fd);
    return vcpu;
}

/*
 * Destroy a vCPU
 */
static void kvm_destroy_vcpu(struct hv_vcpu *vcpu)
{
    struct kvm_vcpu_data *data;

    if (!vcpu)
        return;

    data = vcpu->data;
    if (data) {
        if (data->run)
            munmap(data->run, ((struct kvm_vm_data *)vcpu->vm->data)->vcpu_mmap_size);
        free(data);
    }

    if (vcpu->fd >= 0)
        close(vcpu->fd);

    free(vcpu);
}

/*
 * Get vCPU fd
 */
static int kvm_vcpu_get_fd(struct hv_vcpu *vcpu)
{
    return vcpu->fd;
}

/*
 * Map memory into VM
 */
static int kvm_map_mem(struct hv_vm *vm, struct hv_memory_slot *slot)
{
    struct kvm_userspace_memory_region region;

    memset(&region, 0, sizeof(region));
    region.slot = slot->slot;
    region.flags = slot->flags;
    region.guest_phys_addr = slot->gpa;
    region.memory_size = slot->size;
    region.userspace_addr = (uint64_t)slot->hva;

    if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }

    log_debug("Mapped memory slot %d: GPA 0x%lx -> HVA %p (size=%ld)",
              slot->slot, slot->gpa, slot->hva, slot->size);
    return 0;
}

/*
 * Unmap memory from VM
 */
static int kvm_unmap_mem(struct hv_vm *vm, uint32_t slot)
{
    struct kvm_userspace_memory_region region;

    memset(&region, 0, sizeof(region));
    region.slot = slot;
    region.flags = 0;
    region.guest_phys_addr = 0;
    region.memory_size = 0;
    region.userspace_addr = 0;

    if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION (unmap)");
        return -1;
    }

    log_debug("Unmapped memory slot %d", slot);
    return 0;
}

/*
 * Run vCPU
 */
static int kvm_run(struct hv_vcpu *vcpu)
{
    struct kvm_vcpu_data *data = vcpu->data;

    if (ioctl(vcpu->fd, KVM_RUN, 0) < 0) {
        if (errno == EINTR) {
            return 0;  /* Interrupted by signal */
        }
        perror("KVM_RUN");
        return -1;
    }

    return 0;
}

/*
 * Convert KVM exit reason to HV exit reason
 */
static enum hv_exit_reason kvm_convert_exit_reason(uint32_t kvm_reason)
{
    switch (kvm_reason) {
    case KVM_EXIT_HLT:       return HV_EXIT_HLT;
    case KVM_EXIT_IO:        return HV_EXIT_IO;
    case KVM_EXIT_MMIO:      return HV_EXIT_MMIO;
    case KVM_EXIT_INTR:      return HV_EXIT_EXTERNAL;
    case KVM_EXIT_FAIL_ENTRY: return HV_EXIT_FAIL_ENTRY;
    case KVM_EXIT_SHUTDOWN:  return HV_EXIT_SHUTDOWN;
    case KVM_EXIT_INTERNAL_ERROR: return HV_EXIT_INTERNAL_ERROR;
    case KVM_EXIT_EXCEPTION: return HV_EXIT_EXCEPTION;
    default:                 return HV_EXIT_UNKNOWN;
    }
}

/*
 * Get exit information
 */
static int kvm_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit)
{
    struct kvm_vcpu_data *data = vcpu->data;
    struct kvm_run *run = data->run;

    memset(exit, 0, sizeof(*exit));
    exit->reason = kvm_convert_exit_reason(run->exit_reason);

    switch (run->exit_reason) {
    case KVM_EXIT_IO:
        exit->u.io.direction = (run->u.io.direction == 0) ? HV_IO_IN : HV_IO_OUT;
        exit->u.io.size = run->u.io.size;
        exit->u.io.port = run->u.io.port;
        if (run->u.io.direction == 0) {
            memcpy(&exit->u.io.data,
                   (uint8_t *)run + run->u.io.data_offset,
                   run->u.io.size);
        }
        break;

    case KVM_EXIT_MMIO:
        exit->u.mmio.addr = run->u.mmio.phys_addr;
        exit->u.mmio.size = run->u.mmio.len;
        exit->u.mmio.is_write = run->u.mmio.is_write;
        if (run->u.mmio.is_write) {
            memcpy(&exit->u.mmio.data, run->u.mmio.data, run->u.mmio.len);
        }
        break;

    case KVM_EXIT_FAIL_ENTRY:
        exit->u.error_code = *(uint64_t *)run;
        break;

    case KVM_EXIT_INTERNAL_ERROR:
        exit->u.error_code = run->u.internal.ndata;
        break;

    default:
        break;
    }

    return 0;
}

/*
 * Get general registers
 */
static int kvm_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs)
{
    struct kvm_regs kvm_regs;

    if (ioctl(vcpu->fd, KVM_GET_REGS, &kvm_regs) < 0) {
        perror("KVM_GET_REGS");
        return -1;
    }

    regs->rax = kvm_regs.rax;
    regs->rbx = kvm_regs.rbx;
    regs->rcx = kvm_regs.rcx;
    regs->rdx = kvm_regs.rdx;
    regs->rsi = kvm_regs.rsi;
    regs->rdi = kvm_regs.rdi;
    regs->rsp = kvm_regs.rsp;
    regs->rbp = kvm_regs.rbp;
    regs->r8 = kvm_regs.r8;
    regs->r9 = kvm_regs.r9;
    regs->r10 = kvm_regs.r10;
    regs->r11 = kvm_regs.r11;
    regs->r12 = kvm_regs.r12;
    regs->r13 = kvm_regs.r13;
    regs->r14 = kvm_regs.r14;
    regs->r15 = kvm_regs.r15;
    regs->rip = kvm_regs.rip;
    regs->rflags = kvm_regs.rflags;

    return 0;
}

/*
 * Set general registers
 */
static int kvm_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs)
{
    struct kvm_regs kvm_regs;

    memset(&kvm_regs, 0, sizeof(kvm_regs));
    kvm_regs.rax = regs->rax;
    kvm_regs.rbx = regs->rbx;
    kvm_regs.rcx = regs->rcx;
    kvm_regs.rdx = regs->rdx;
    kvm_regs.rsi = regs->rsi;
    kvm_regs.rdi = regs->rdi;
    kvm_regs.rsp = regs->rsp;
    kvm_regs.rbp = regs->rbp;
    kvm_regs.r8 = regs->r8;
    kvm_regs.r9 = regs->r9;
    kvm_regs.r10 = regs->r10;
    kvm_regs.r11 = regs->r11;
    kvm_regs.r12 = regs->r12;
    kvm_regs.r13 = regs->r13;
    kvm_regs.r14 = regs->r14;
    kvm_regs.r15 = regs->r15;
    kvm_regs.rip = regs->rip;
    kvm_regs.rflags = regs->rflags;

    if (ioctl(vcpu->fd, KVM_SET_REGS, &kvm_regs) < 0) {
        perror("KVM_SET_REGS");
        return -1;
    }

    return 0;
}

/*
 * Get special registers
 */
static int kvm_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs)
{
    struct kvm_sregs kvm_sregs;

    if (ioctl(vcpu->fd, KVM_GET_SREGS, &kvm_sregs) < 0) {
        perror("KVM_GET_SREGS");
        return -1;
    }

    sregs->cs.selector = kvm_sregs.cs.selector;
    sregs->cs.base = kvm_sregs.cs.base;
    sregs->cs.limit = kvm_sregs.cs.limit;
    sregs->cs.ar = kvm_sregs.cs.ar;

    sregs->ds.selector = kvm_sregs.ds.selector;
    sregs->ds.base = kvm_sregs.ds.base;
    sregs->ds.limit = kvm_sregs.ds.limit;
    sregs->ds.ar = kvm_sregs.ds.ar;

    sregs->es.selector = kvm_sregs.es.selector;
    sregs->es.base = kvm_sregs.es.base;
    sregs->es.limit = kvm_sregs.es.limit;
    sregs->es.ar = kvm_sregs.es.ar;

    sregs->fs.selector = kvm_sregs.fs.selector;
    sregs->fs.base = kvm_sregs.fs.base;
    sregs->fs.limit = kvm_sregs.fs.limit;
    sregs->fs.ar = kvm_sregs.fs.ar;

    sregs->gs.selector = kvm_sregs.gs.selector;
    sregs->gs.base = kvm_sregs.gs.base;
    sregs->gs.limit = kvm_sregs.gs.limit;
    sregs->gs.ar = kvm_sregs.gs.ar;

    sregs->ss.selector = kvm_sregs.ss.selector;
    sregs->ss.base = kvm_sregs.ss.base;
    sregs->ss.limit = kvm_sregs.ss.limit;
    sregs->ss.ar = kvm_sregs.ss.ar;

    sregs->gdt.base = kvm_sregs.gdt.base;
    sregs->gdt.limit = kvm_sregs.gdt.limit;
    sregs->gdt.ar = kvm_sregs.gdt.ar;

    sregs->idt.base = kvm_sregs.idt.base;
    sregs->idt.limit = kvm_sregs.idt.limit;
    sregs->idt.ar = kvm_sregs.idt.ar;

    sregs->cr0 = kvm_sregs.cr0;
    sregs->cr2 = kvm_sregs.cr2;
    sregs->cr3 = kvm_sregs.cr3;
    sregs->cr4 = kvm_sregs.cr4;
    sregs->cr8 = kvm_sregs.cr8;
    sregs->efer = kvm_sregs.efer;
    sregs->apic_base = kvm_sregs.apic_base;

    return 0;
}

/*
 * Set special registers
 */
static int kvm_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs)
{
    struct kvm_sregs kvm_sregs;

    memset(&kvm_sregs, 0, sizeof(kvm_sregs));

    kvm_sregs.cs.selector = sregs->cs.selector;
    kvm_sregs.cs.base = sregs->cs.base;
    kvm_sregs.cs.limit = sregs->cs.limit;
    kvm_sregs.cs.ar = sregs->cs.ar;

    kvm_sregs.ds.selector = sregs->ds.selector;
    kvm_sregs.ds.base = sregs->ds.base;
    kvm_sregs.ds.limit = sregs->ds.limit;
    kvm_sregs.ds.ar = sregs->ds.ar;

    kvm_sregs.es.selector = sregs->es.selector;
    kvm_sregs.es.base = sregs->es.base;
    kvm_sregs.es.limit = sregs->es.limit;
    kvm_sregs.es.ar = sregs->es.ar;

    kvm_sregs.fs.selector = sregs->fs.selector;
    kvm_sregs.fs.base = sregs->fs.base;
    kvm_sregs.fs.limit = sregs->fs.limit;
    kvm_sregs.fs.ar = sregs->fs.ar;

    kvm_sregs.gs.selector = sregs->gs.selector;
    kvm_sregs.gs.base = sregs->gs.base;
    kvm_sregs.gs.limit = sregs->gs.limit;
    kvm_sregs.gs.ar = sregs->gs.ar;

    kvm_sregs.ss.selector = sregs->ss.selector;
    kvm_sregs.ss.base = sregs->ss.base;
    kvm_sregs.ss.limit = sregs->ss.limit;
    kvm_sregs.ss.ar = sregs->ss.ar;

    kvm_sregs.gdt.base = sregs->gdt.base;
    kvm_sregs.gdt.limit = sregs->gdt.limit;
    kvm_sregs.gdt.ar = sregs->gdt.ar;

    kvm_sregs.idt.base = sregs->idt.base;
    kvm_sregs.idt.limit = sregs->idt.limit;
    kvm_sregs.idt.ar = sregs->idt.ar;

    kvm_sregs.cr0 = sregs->cr0;
    kvm_sregs.cr2 = sregs->cr2;
    kvm_sregs.cr3 = sregs->cr3;
    kvm_sregs.cr4 = sregs->cr4;
    kvm_sregs.cr8 = sregs->cr8;
    kvm_sregs.efer = sregs->efer;
    kvm_sregs.apic_base = sregs->apic_base;

    if (ioctl(vcpu->fd, KVM_SET_SREGS, &kvm_sregs) < 0) {
        perror("KVM_SET_SREGS");
        return -1;
    }

    return 0;
}

/*
 * Assert/deassert IRQ line
 */
static int kvm_irq_line(struct hv_vm *vm, int irq, int level)
{
    struct kvm_irq_level irq_level;

    irq_level.irq = irq;
    irq_level.level = level ? 1 : 0;

    if (ioctl(vm->fd, KVM_IRQ_LINE, &irq_level) < 0) {
        perror("KVM_IRQ_LINE");
        return -1;
    }

    return 0;
}
