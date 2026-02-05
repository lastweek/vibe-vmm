/*
 * VM lifecycle management
 */

#include "vm.h"
#include "vcpu.h"
#include "hypervisor.h"
#include "utils.h"
#include "devices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Log level (can be overridden by command line) */
int log_level = LOG_LEVEL_INFO;

/*
 * Create a new VM
 */
struct vm* vm_create(void)
{
    struct vm *vm;
    struct hv_vm *hv_vm;

    hv_vm = hv_create_vm();
    if (!hv_vm) {
        log_error("Failed to create hypervisor VM");
        return NULL;
    }

    vm = calloc(1, sizeof(*vm));
    if (!vm) {
        log_error("Failed to allocate VM");
        hv_destroy_vm(hv_vm);
        return NULL;
    }

    vm->hv_vm = hv_vm;
    vm->state = VM_STATE_STOPPED;
    vm->mem_size = 0;
    vm->num_vcpus = 0;
    vm->num_devices = 0;
    vm->irq_base = 5;  /* Start IRQs at 5 */

    log_info("VM created");
    return vm;
}

/*
 * Destroy a VM
 */
void vm_destroy(struct vm *vm)
{
    int i;

    if (!vm)
        return;

    /* Stop VM if running */
    if (vm->state == VM_STATE_RUNNING)
        vm_stop(vm);

    /* Destroy vCPUs */
    for (i = 0; i < vm->num_vcpus; i++) {
        if (vm->vcpus[i])
            vcpu_destroy(vm->vcpus[i]);
    }

    /* Destroy devices */
    for (i = 0; i < vm->num_devices; i++) {
        if (vm->devices[i])
            device_unregister(vm->devices[i]);
    }

    /* Free memory regions */
    for (i = 0; i < VM_MAX_SLOTS; i++) {
        if (vm->mem_regions[i].used) {
            /* Unmap from hypervisor */
            hv_unmap_mem(vm->hv_vm, vm->mem_regions[i].slot);
            /* Free guest memory */
            free(vm->mem_regions[i].hva);
        }
    }

    /* Free configuration strings */
    free(vm->kernel_path);
    free(vm->initrd_path);
    free(vm->cmdline);

    /* Destroy hypervisor VM */
    hv_destroy_vm(vm->hv_vm);

    /* Free VM structure */
    free(vm);

    log_info("VM destroyed");
}

/*
 * Start the VM
 */
int vm_start(struct vm *vm)
{
    int i, ret;

    if (vm->state == VM_STATE_RUNNING)
        return 0;

    log_info("Starting VM...");

    /* Start all vCPUs */
    for (i = 0; i < vm->num_vcpus; i++) {
        ret = vcpu_start(vm->vcpus[i]);
        if (ret < 0) {
            log_error("Failed to start vCPU %d", i);
            /* Stop already started vCPUs */
            for (int j = 0; j < i; j++)
                vcpu_stop(vm->vcpus[j]);
            return -1;
        }
    }

    vm->state = VM_STATE_RUNNING;
    log_info("VM started");
    return 0;
}

/*
 * Stop the VM
 */
int vm_stop(struct vm *vm)
{
    int i;

    if (vm->state == VM_STATE_STOPPED)
        return 0;

    log_info("Stopping VM...");

    /* Stop all vCPUs */
    for (i = 0; i < vm->num_vcpus; i++) {
        if (vm->vcpus[i])
            vcpu_stop(vm->vcpus[i]);
    }

    vm->state = VM_STATE_STOPPED;
    log_info("VM stopped");
    return 0;
}

/*
 * Pause the VM
 */
int vm_pause(struct vm *vm)
{
    (void)vm;
    /* Implementation: pause all vCPUs */
    return 0;
}

/*
 * Add a memory region to the VM
 */
int vm_add_memory_region(struct vm *vm, uint64_t gpa, uint64_t size)
{
    struct vm_mem_region *region;
    struct hv_memory_slot slot;
    void *hva;
    int i, ret;

    /* Find free slot */
    region = NULL;
    for (i = 0; i < VM_MAX_SLOTS; i++) {
        if (!vm->mem_regions[i].used) {
            region = &vm->mem_regions[i];
            break;
        }
    }

    if (!region) {
        log_error("No free memory slots");
        return -1;
    }

    /* Allocate guest memory */
    hva = calloc(1, size);
    if (!hva) {
        log_error("Failed to allocate guest memory");
        return -1;
    }

    /* Align to page boundaries */
    gpa = PAGE_ALIGN_DOWN(gpa);

    /* Set up slot */
    slot.slot = i;
    slot.gpa = gpa;
    slot.hva = hva;
    slot.size = size;
    slot.flags = 0;

    /* Map into hypervisor */
    ret = hv_map_mem(vm->hv_vm, slot.slot, slot.gpa, slot.hva, slot.size);
    if (ret < 0) {
        log_error("Failed to map memory into hypervisor");
        free(hva);
        return -1;
    }

    /* Record region */
    region->gpa = gpa;
    region->hva = hva;
    region->size = size;
    region->slot = i;
    region->used = 1;

    vm->mem_size += size;

    log_info("Added memory region: GPA 0x%lx -> HVA %p (size=%ld MB)",
             gpa, hva, size / (1024 * 1024));
    return 0;
}

/*
 * Translate GPA to HVA
 */
void* vm_gpa_to_hva(struct vm *vm, uint64_t gpa, uint64_t size)
{
    int i;

    for (i = 0; i < VM_MAX_SLOTS; i++) {
        struct vm_mem_region *region = &vm->mem_regions[i];

        if (!region->used)
            continue;

        if (gpa >= region->gpa && (gpa + size) <= (region->gpa + region->size)) {
            uint64_t offset = gpa - region->gpa;
            return (char *)region->hva + offset;
        }
    }

    log_warn("GPA 0x%lx not mapped", gpa);
    return NULL;
}

/*
 * Register a device with the VM
 */
int vm_register_device(struct vm *vm, struct device *dev)
{
    if (vm->num_devices >= VM_MAX_DEVICES) {
        log_error("Too many devices");
        return -1;
    }

    vm->devices[vm->num_devices++] = dev;

    log_info("Registered device: %s at GPA 0x%lx-0x%lx",
             dev->name, dev->gpa_start, dev->gpa_end);
    return 0;
}

/*
 * Find device at GPA
 */
struct device* vm_find_device_at_gpa(struct vm *vm, uint64_t gpa)
{
    int i;

    for (i = 0; i < vm->num_devices; i++) {
        struct device *dev = vm->devices[i];

        if (gpa >= dev->gpa_start && gpa <= dev->gpa_end)
            return dev;
    }

    return NULL;
}

/*
 * Create vCPUs
 */
int vm_create_vcpus(struct vm *vm, int num_vcpus)
{
    int i;

    if (num_vcpus > VM_MAX_VCPUS) {
        log_error("Too many vCPUs: %d (max %d)", num_vcpus, VM_MAX_VCPUS);
        return -1;
    }

    for (i = 0; i < num_vcpus; i++) {
        vm->vcpus[i] = vcpu_create(vm, i);
        if (!vm->vcpus[i]) {
            log_error("Failed to create vCPU %d", i);
            /* Cleanup created vCPUs */
            for (int j = 0; j < i; j++) {
                vcpu_destroy(vm->vcpus[j]);
                vm->vcpus[j] = NULL;
            }
            return -1;
        }
    }

    vm->num_vcpus = num_vcpus;
    log_info("Created %d vCPU(s)", num_vcpus);
    return 0;
}

/*
 * Set kernel path
 */
int vm_set_kernel(struct vm *vm, const char *path)
{
    free(vm->kernel_path);
    vm->kernel_path = strdup(path);
    if (!vm->kernel_path)
        return -1;
    return 0;
}

/*
 * Set initrd path
 */
int vm_set_initrd(struct vm *vm, const char *path)
{
    free(vm->initrd_path);
    vm->initrd_path = strdup(path);
    if (!vm->initrd_path)
        return -1;
    return 0;
}

/*
 * Set kernel command line
 */
int vm_set_cmdline(struct vm *vm, const char *cmdline)
{
    free(vm->cmdline);
    vm->cmdline = strdup(cmdline);
    if (!vm->cmdline)
        return -1;
    return 0;
}
