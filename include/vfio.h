#ifndef VIBE_VMM_VFIO_H
#define VIBE_VMM_VFIO_H

#include <stdint.h>
#include <stddef.h>
#include "devices.h"

/* VFIO constants */
#define VFIO_API_VERSION     0
#define VFIO_SPAS_COUNT      2
#define VFIO_MAX_REGIONS     8
#define VFIO_MAX_IRQS        32

/* VFIO region flags */
#define VFIO_REGION_INFO_FLAG_READ      (1 << 0)
#define VFIO_REGION_INFO_FLAG_WRITE     (1 << 1)
#define VFIO_REGION_INFO_FLAG_MMAP      (1 << 2)
#define VFIO_REGION_INFO_FLAG_CAPS      (1 << 3)

/* VFIO IRQ flags */
#define VFIO_IRQ_INFO_EVENTFD          (1 << 0)
#define VFIO_IRQ_INFO_MASKABLE         (1 << 1)
#define VFIO_IRQ_INFO_AUTOMASKED       (1 << 2)
#define VFIO_IRQ_INFO_NORESIZE         (1 << 3)

/* VFIO region info */
struct vfio_region_info {
    uint32_t argsz;
    uint32_t flags;
    uint32_t index;
    uint32_t cap_offset;
    uint64_t size;
    uint64_t offset;
};

/* VFIO IRQ info */
struct vfio_irq_info {
    uint32_t argsz;
    uint32_t flags;
    uint32_t index;
    uint32_t count;
};

/* VFIO device info */
struct vfio_device_info {
    uint32_t argsz;
    uint32_t flags;
    uint32_t num_regions;
    uint32_t num_irqs;
};

/* VFIO region */
struct vfio_region {
    uint32_t index;
    uint64_t size;
    uint64_t offset;
    uint64_t gpa;          /* Guest physical address (if mapped) */
    void     *hva;         /* Host virtual address (if mmap'd) */
    uint32_t flags;
};

/* VFIO IRQ */
struct vfio_irq {
    uint32_t index;
    uint32_t count;
    int      type;         /* IRQ type (e.g., VFIO_PCI_INTX_IRQ_INDEX) */
    int      fds[VFIO_MAX_IRQS];
    int      enabled;
};

/* VFIO device */
struct vfio_dev {
    struct device device;     /* Base device */

    /* VFIO specific */
    int       container_fd;
    int       group_fd;
    int       device_fd;

    /* PCI config */
    uint16_t  vendor_id;
    uint16_t  device_id;
    uint8_t   class_code[3];
    uint8_t   revision;
    char      name[64];

    /* Regions */
    struct vfio_region regions[VFIO_MAX_REGIONS];
    int       num_regions;

    /* IRQs */
    struct vfio_irq irqs[VFIO_MAX_IRQS];
    int       num_irqs;

    /* IOMMU */
    int       iommu_group;
};

/* VFIO container */
struct vfio_container {
    int fd;
};

/* Create/destroy VFIO container */
struct vfio_container* vfio_container_create(void);
void vfio_container_destroy(struct vfio_container *container);

/* Open VFIO device */
struct vfio_dev* vfio_device_open(struct vfio_container *container, const char *bdf);
void vfio_device_close(struct vfio_dev *vdev);

/* Get device info */
int vfio_device_get_info(struct vfio_dev *vdev, struct vfio_device_info *info);

/* Get region info */
int vfio_device_get_region_info(struct vfio_dev *vdev, uint32_t index,
                                 struct vfio_region_info *info);

/* Map region to guest */
int vfio_map_region(struct vfio_dev *vdev, uint32_t index, uint64_t gpa);

/* Unmap region */
int vfio_unmap_region(struct vfio_dev *vdev, uint32_t index);

/* Setup IRQs */
int vfio_setup_irqs(struct vfio_dev *vdev, uint32_t index, uint32_t type);

/* Enable/disable IRQ */
int vfio_irq_enable(struct vfio_dev *vdev, uint32_t index, uint32_t subindex, int fd);
int vfio_irq_disable(struct vfio_dev *vdev, uint32_t index);

/* PCI config space access */
int vfio_pci_cfg_read(struct vfio_dev *vdev, uint32_t offset, void *data, size_t size);
int vfio_pci_cfg_write(struct vfio_dev *vdev, uint32_t offset, const void *data, size_t size);

/* Register VFIO device with VM */
int vfio_register_with_vm(struct vm *vm, struct vfio_dev *vdev, const char *bdf);

#endif /* VIBE_VMM_VFIO_H */
