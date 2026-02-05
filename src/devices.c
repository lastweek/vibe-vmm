/*
 * Device framework implementation
 */

#include "devices.h"
#include "utils.h"
#include "vm.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/eventfd.h>
#else
/* Stub for platforms without eventfd */
#define EFD_NONBLOCK 0
static inline int eventfd(unsigned int initval, int flags) {
    (void)initval;
    (void)flags;
    return -1; /* Not supported */
}
#endif

/*
 * Create a device
 */
struct device* device_create(const char *name, size_t priv_size)
{
    struct device *dev;

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        log_error("Failed to allocate device");
        return NULL;
    }

    dev->name = strdup(name);
    if (!dev->name) {
        free(dev);
        log_error("Failed to allocate device name");
        return NULL;
    }

    if (priv_size > 0) {
        dev->data = calloc(1, priv_size);
        if (!dev->data) {
            free(dev->name);
            free(dev);
            log_error("Failed to allocate device private data");
            return NULL;
        }
    }

    dev->irq_fd = -1;

    log_debug("Created device: %s", name);
    return dev;
}

/*
 * Destroy a device
 */
void device_destroy(struct device *dev)
{
    if (!dev)
        return;

    if (dev->ops && dev->ops->destroy)
        dev->ops->destroy(dev);

    if (dev->irq_fd >= 0)
        close(dev->irq_fd);

    free(dev->data);
    free(dev->name);
    free(dev);
}

/*
 * Register a device with VM
 */
int device_register(struct vm *vm, struct device *dev)
{
    if (!vm || !dev)
        return -1;

    if (vm->num_devices >= VM_MAX_DEVICES) {
        log_error("Too many devices");
        return -1;
    }

    dev->vm = vm;

    /* Create eventfd for IRQ if not already created */
    if (dev->irq_fd < 0) {
        dev->irq_fd = eventfd(0, EFD_NONBLOCK);
        if (dev->irq_fd < 0) {
            perror("eventfd");
            return -1;
        }
    }

    vm->devices[vm->num_devices++] = dev;

    log_info("Registered device: %s at GPA 0x%lx-0x%lx",
             dev->name, dev->gpa_start, dev->gpa_end);
    return 0;
}

/*
 * Unregister a device
 */
void device_unregister(struct device *dev)
{
    struct vm *vm;
    int i;

    if (!dev)
        return;

    vm = dev->vm;
    if (!vm)
        return;

    /* Find and remove device from VM */
    for (i = 0; i < vm->num_devices; i++) {
        if (vm->devices[i] == dev) {
            /* Shift remaining devices */
            for (int j = i; j < vm->num_devices - 1; j++)
                vm->devices[j] = vm->devices[j + 1];
            vm->num_devices--;
            break;
        }
    }

    device_destroy(dev);
}

/*
 * Find device at GPA
 */
struct device* device_find_at_gpa(struct vm *vm, uint64_t gpa)
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
 * Handle MMIO access
 */
int device_handle_mmio(struct vm *vm, uint64_t gpa, int is_write,
                       uint64_t *data, uint8_t size)
{
    struct device *dev;
    uint64_t offset;
    int ret;

    dev = device_find_at_gpa(vm, gpa);
    if (!dev) {
        log_warn("No device at GPA 0x%lx", gpa);
        return -1;
    }

    offset = gpa - dev->gpa_start;

    if (is_write) {
        if (dev->ops && dev->ops->write) {
            ret = dev->ops->write(dev, offset, data, size);
        } else {
            log_warn("Device %s has no write handler", dev->name);
            ret = -1;
        }
    } else {
        if (dev->ops && dev->ops->read) {
            ret = dev->ops->read(dev, offset, data, size);
        } else {
            log_warn("Device %s has no read handler", dev->name);
            ret = -1;
        }
    }

    return ret;
}

/*
 * Assert IRQ
 */
int device_assert_irq(struct device *dev)
{
    uint64_t value = 1;

    if (dev->irq_fd < 0) {
        log_warn("Device %s has no IRQ fd", dev->name);
        return -1;
    }

    if (write(dev->irq_fd, &value, sizeof(value)) != sizeof(value)) {
        perror("write eventfd");
        return -1;
    }

    log_debug("Device %s asserted IRQ %d", dev->name, dev->irq);
    return 0;
}

/*
 * Deassert IRQ
 */
int device_deassert_irq(struct device *dev)
{
    uint64_t value;

    if (dev->irq_fd < 0) {
        log_warn("Device %s has no IRQ fd", dev->name);
        return -1;
    }

    /* Read to clear */
    if (read(dev->irq_fd, &value, sizeof(value)) != sizeof(value)) {
        /* If no data, that's OK */
    }

    log_debug("Device %s deasserted IRQ %d", dev->name, dev->irq);
    return 0;
}
