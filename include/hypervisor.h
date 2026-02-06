#ifndef VIBE_VMM_HYPERVISOR_H
#define VIBE_VMM_HYPERVISOR_H

#include <stdint.h>
#include <stddef.h>

/* Hypervisor backend types */
enum hv_type {
    HV_TYPE_AUTO,           /* Auto-detect platform and architecture */
    HV_TYPE_KVM,            /* Linux KVM */
    HV_TYPE_HVF,            /* macOS HVF (legacy, auto-detects arch) */
    HV_TYPE_HVF_X86_64,     /* macOS HVF for x86_64 (Intel Macs) */
    HV_TYPE_HVF_ARM64,      /* macOS HVF for ARM64 (Apple Silicon) */
};

/* VM exit reasons - Apple Hypervisor.framework */
enum hv_exit_reason {
    /* Common/legacy exit reasons for x86_64 */
    HV_EXIT_UNKNOWN        = -1,
    HV_EXIT_NONE           = 0,
    HV_EXIT_HLT            = 1,
    HV_EXIT_IO             = 2,
    HV_EXIT_MMIO           = 3,
    HV_EXIT_EXTERNAL       = 4,
    HV_EXIT_FAIL_ENTRY     = 5,
    HV_EXIT_SHUTDOWN       = 6,
    HV_EXIT_INTERNAL_ERROR = 7,
    HV_EXIT_EXCEPTION      = 8,

    /* ARM64-specific exit reasons (Apple Silicon) */
    HV_EXIT_CANCELED       = 10,    /* Asynchronous exit from hv_vcpus_exit() */
    HV_EXIT_VTIMER         = 11,    /* Virtual timer activated */
};

/* Forward declarations */
struct hv_vm;
struct hv_vcpu;

/* I/O direction */
enum hv_io_dir {
    HV_IO_IN,
    HV_IO_OUT,
};

/* I/O operation */
struct hv_io {
    uint16_t port;
    uint8_t  size;  /* 1, 2, or 4 bytes */
    enum hv_io_dir direction;
    uint32_t data;
};

/* MMIO operation */
struct hv_mmio {
    uint64_t addr;
    uint8_t  size;  /* 1, 2, 4, or 8 bytes */
    int      is_write;
    uint64_t data;
};

/* VM exit info */
struct hv_exit {
    enum hv_exit_reason reason;
    union {
        struct hv_io io;
        struct hv_mmio mmio;
        uint64_t error_code;
    } u;
};

/* CPU registers */
struct hv_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
};

/* Special registers */
struct hv_sregs {
    struct {
        uint16_t selector;
        uint64_t base;
        uint32_t limit;
        uint32_t ar;
    } cs, ds, es, fs, gs, ss, ldt, tr;

    struct {
        uint64_t base;
        uint32_t limit;
        uint32_t ar;
    } gdt, idt;

    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
};

/* Memory slot */
struct hv_memory_slot {
    uint32_t slot;
    uint64_t gpa;
    uint64_t size;
    void     *hva;
    uint64_t flags;
};

/* Hypervisor operations (abstract interface) */
struct hv_ops {
    int (*init)(void);
    void (*cleanup)(void);

    struct hv_vm* (*create_vm)(int *fd);
    void (*destroy_vm)(struct hv_vm *vm);
    int (*vm_get_fd)(struct hv_vm *vm);

    struct hv_vcpu* (*create_vcpu)(struct hv_vm *vm, int index);
    void (*destroy_vcpu)(struct hv_vcpu *vcpu);
    int (*vcpu_get_fd)(struct hv_vcpu *vcpu);

    int (*map_mem)(struct hv_vm *vm, struct hv_memory_slot *slot);
    int (*unmap_mem)(struct hv_vm *vm, uint32_t slot);

    int (*run)(struct hv_vcpu *vcpu);
    int (*get_exit)(struct hv_vcpu *vcpu, struct hv_exit *exit);

    int (*get_regs)(struct hv_vcpu *vcpu, struct hv_regs *regs);
    int (*set_regs)(struct hv_vcpu *vcpu, const struct hv_regs *regs);

    int (*get_sregs)(struct hv_vcpu *vcpu, struct hv_sregs *sregs);
    int (*set_sregs)(struct hv_vcpu *vcpu, const struct hv_sregs *sregs);

    int (*irq_line)(struct hv_vm *vm, int irq, int level);
};

/* Opaque VM and vCPU structures */
struct hv_vm {
    const struct hv_ops *ops;
    int fd;
    void *data;  /* Backend-specific data */
};

struct hv_vcpu {
    const struct hv_ops *ops;
    int fd;
    struct hv_vm *vm;
    int index;
    void *data;  /* Backend-specific data (e.g., mmap'd run structure) */
};

/* Hypervisor API */
int hv_init(enum hv_type type);
void hv_cleanup(void);

const struct hv_ops* hv_get_ops(void);

/* VM operations */
struct hv_vm* hv_create_vm(void);
void hv_destroy_vm(struct hv_vm *vm);
int hv_vm_get_fd(struct hv_vm *vm);

/* vCPU operations */
struct hv_vcpu* hv_create_vcpu(struct hv_vm *vm, int index);
void hv_destroy_vcpu(struct hv_vcpu *vcpu);
int hv_vcpu_get_fd(struct hv_vcpu *vcpu);

/* Memory operations */
int hv_map_mem(struct hv_vm *vm, uint32_t slot, uint64_t gpa, void *hva, uint64_t size);
int hv_unmap_mem(struct hv_vm *vm, uint32_t slot);

/* Run operations */
int hv_run(struct hv_vcpu *vcpu);
int hv_get_exit(struct hv_vcpu *vcpu, struct hv_exit *exit);

/* Register operations */
int hv_get_regs(struct hv_vcpu *vcpu, struct hv_regs *regs);
int hv_set_regs(struct hv_vcpu *vcpu, const struct hv_regs *regs);

int hv_get_sregs(struct hv_vcpu *vcpu, struct hv_sregs *sregs);
int hv_set_sregs(struct hv_vcpu *vcpu, const struct hv_sregs *sregs);

/* IRQ operations */
int hv_irq_line(struct hv_vm *vm, int irq, int level);

#endif /* VIBE_VMM_HYPERVISOR_H */
