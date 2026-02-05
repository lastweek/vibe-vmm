#ifndef VIBE_VMM_DEVICES_H
#define VIBE_VMM_DEVICES_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct vm;
struct device;

/* Device operations */
struct device_ops {
    const char *name;

    /* Read from device */
    int (*read)(struct device *dev, uint64_t offset, void *data, size_t size);

    /* Write to device */
    int (*write)(struct device *dev, uint64_t offset, const void *data, size_t size);

    /* Destroy device */
    void (*destroy)(struct device *dev);
};

/* MMIO device */
struct device {
    const struct device_ops *ops;

    char            *name;
    struct vm       *vm;

    /* MMIO region */
    uint64_t         gpa_start;
    uint64_t         gpa_end;
    uint64_t         size;

    /* Private data */
    void            *data;

    /* Interrupt handling */
    int              irq;
    int              irq_fd;  /* eventfd for interrupt injection */

    /* Linked list */
    struct device   *next;
};

/* Register an MMIO device */
int device_register(struct vm *vm, struct device *dev);

/* Unregister a device */
void device_unregister(struct device *dev);

/* Find device at GPA */
struct device* device_find_at_gpa(struct vm *vm, uint64_t gpa);

/* Handle MMIO access */
int device_handle_mmio(struct vm *vm, uint64_t gpa, int is_write,
                       uint64_t *data, uint8_t size);

/* Assert/deassert IRQ */
int device_assert_irq(struct device *dev);
int device_deassert_irq(struct device *dev);

/* Create device */
struct device* device_create(const char *name, size_t priv_size);

/* Destroy device */
void device_destroy(struct device *dev);

/* Get private data */
static inline void* device_get_priv(struct device *dev) {
    return dev->data;
}

/* Device creation functions */
struct device* mmio_console_create(void);
struct device* virtio_console_create(void);
struct device* virtio_blk_create(const char *disk_path);
struct device* virtio_net_create(const char *tap_name);

#endif /* VIBE_VMM_DEVICES_H */
