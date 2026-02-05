#ifndef VIBE_VMM_VM_H
#define VIBE_VMM_VM_H

#include "hypervisor.h"
#include <stdint.h>
#include <stddef.h>

/* Maximum number of memory slots */
#define VM_MAX_SLOTS      32
#define VM_MAX_VCPUS      8
#define VM_MAX_DEVICES    16

/* VM state */
enum vm_state {
    VM_STATE_STOPPED,
    VM_STATE_RUNNING,
    VM_STATE_PAUSED,
    VM_STATE_ERROR,
};

/* Memory region */
struct vm_mem_region {
    uint64_t gpa;         /* Guest physical address */
    void     *hva;        /* Host virtual address */
    uint64_t size;        /* Size in bytes */
    uint32_t slot;        /* Hypervisor slot number */
    int       used;       /* Whether this region is in use */
};

/* VM structure */
struct vm {
    struct hv_vm *hv_vm;              /* Hypervisor VM */
    enum vm_state state;              /* VM state */

    /* Memory */
    struct vm_mem_region mem_regions[VM_MAX_SLOTS];
    uint64_t mem_size;                /* Total guest memory size */

    /* vCPUs */
    struct vcpu *vcpus[VM_MAX_VCPUS];
    int num_vcpus;

    /* Devices */
    struct device *devices[VM_MAX_DEVICES];
    int num_devices;

    /* Configuration */
    char *kernel_path;
    char *initrd_path;
    char *cmdline;

    /* IRQ routing */
    int irq_base;                     /* Base IRQ number for devices */
};

/* Create/destroy VM */
struct vm* vm_create(void);
void vm_destroy(struct vm *vm);

/* VM state management */
int vm_start(struct vm *vm);
int vm_stop(struct vm *vm);
int vm_pause(struct vm *vm);

/* Memory management */
int vm_add_memory_region(struct vm *vm, uint64_t gpa, uint64_t size);
void *vm_gpa_to_hva(struct vm *vm, uint64_t gpa, uint64_t size);

/* Device management */
int vm_register_device(struct vm *vm, struct device *dev);
struct device* vm_find_device_at_gpa(struct vm *vm, uint64_t gpa);

/* vCPU management */
int vm_create_vcpus(struct vm *vm, int num_vcpus);

/* Configuration */
int vm_set_kernel(struct vm *vm, const char *path);
int vm_set_initrd(struct vm *vm, const char *path);
int vm_set_cmdline(struct vm *vm, const char *cmdline);

#endif /* VIBE_VMM_VM_H */
