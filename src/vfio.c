/*
 * VFIO/SR-IOV support for device passthrough
 */

#include "vfio.h"
#include "utils.h"
#include "vm.h"
#include "devices.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* VFIO API */
#define VFIO_API_VERSION    0
#define VFIO_FLAG_SET(flags, bit) ((flags) |= (1 << (bit)))

/* Missing VFIO constants */
#ifndef VFIO_GROUP_GET_DEVICE_FD
#define VFIO_GROUP_GET_DEVICE_FD   _IOW(VFIO_TYPE, VFIO_BASE + 6, char *)
#endif

#ifndef VFIO_IRQ_SET_ACTION_TRIGGER
#define VFIO_IRQ_SET_ACTION_TRIGGER 1
#define VFIO_IRQ_SET_DATA_NONE     0
#define VFIO_IRQ_SET_DATA_BOOL     1
#define VFIO_IRQ_SET_DATA_EVENTFD  2
#endif

/* VFIO IOCTLs */
#define VFIO_GET_API_VERSION    _IO(VFIO_TYPE, VFIO_BASE + 0)
#define VFIO_CHECK_EXTENSION    _IO(VFIO_TYPE, VFIO_BASE + 1)
#define VFIO_SET_IOMMU          _IO(VFIO_TYPE, VFIO_BASE + 2)
#define VFIO_GROUP_GET_STATUS   _IOR(VFIO_TYPE, VFIO_BASE + 3, struct vfio_group_status)
#define VFIO_GROUP_SET_CONTAINER    _IOW(VFIO_TYPE, VFIO_BASE + 4, int)
#define VFIO_GROUP_UNSET_CONTAINER  _IOW(VFIO_TYPE, VFIO_BASE + 5, int)
#define VFIO_DEVICE_GET_INFO    _IOR(VFIO_TYPE, VFIO_BASE + 7, struct vfio_device_info)
#define VFIO_DEVICE_GET_REGION_INFO  _IOWR(VFIO_TYPE, VFIO_BASE + 8, struct vfio_region_info)
#define VFIO_DEVICE_GET_IRQ_INFO    _IOWR(VFIO_TYPE, VFIO_BASE + 9, struct vfio_irq_info)
#define VFIO_DEVICE_SET_IRQS    _IOW(VFIO_TYPE, VFIO_BASE + 10, struct vfio_irq_set)
#define VFIO_DEVICE_RESET       _IO(VFIO_TYPE, VFIO_BASE + 11)
#define VFIO_DEVICE_GET_PCI_HOT_RESET_INFO  _IOWR(VFIO_TYPE, VFIO_BASE + 12, struct vfio_pci_hot_reset_info)

/* VFIO type and base */
#define VFIO_TYPE   0xAF
#define VFIO_BASE   0
#define VFIO_PCI_INDEX_TO_OFFSET(index) ((uint64_t)(index) << 40)

/* IOMMU types */
#define VFIO_TYPE1_IOMMU               1
#define VFIO_SPAPR_TCE_IOMMU           2
#define VFIO_TYPE1v2_IOMMU             3

/* VFIO group status */
#define VFIO_GROUP_FLAGS_VIABLE        (1 << 0)
#define VFIO_GROUP_FLAGS_CONTAINER_SET (1 << 1)

struct vfio_group_status {
    uint32_t argsz;
    uint32_t flags;
};

struct vfio_irq_set {
    uint32_t argsz;
    uint32_t flags;
    uint32_t index;
    uint32_t start;
    uint32_t count;
    int      fd;
};

/* PCI config space offsets */
#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_COMMAND     0x04
#define PCI_STATUS      0x06
#define PCI_CLASS_REVISION  0x08
#define PCI_HEADER_TYPE 0x0e
#define PCI_BAR0        0x10
#define PCI_BAR1        0x14
#define PCI_BAR2        0x18
#define PCI_BAR3        0x1c
#define PCI_BAR4        0x20
#define PCI_BAR5        0x24
#define PCI_CAPABILITY_LIST 0x34
#define PCI_ROM_ADDRESS 0x30

/* IRQ types */
#define VFIO_PCI_INTX_IRQ_INDEX   0
#define VFIO_PCI_MSI_IRQ_INDEX    1
#define VFIO_PCI_MSIX_IRQ_INDEX   2
#define VFIO_PCI_ERR_IRQ_INDEX    3
#define VFIO_PCI_REQ_IRQ_INDEX    4

/*
 * Create VFIO container
 */
struct vfio_container* vfio_container_create(void)
{
    struct vfio_container *container;
    int fd;

    fd = open("/dev/vfio/vfio", O_RDWR);
    if (fd < 0) {
        perror("open /dev/vfio/vfio");
        log_error("Failed to open VFIO container. Is VFIO enabled?");
        return NULL;
    }

    /* Check API version */
    int version = ioctl(fd, VFIO_GET_API_VERSION, 0);
    if (version != VFIO_API_VERSION) {
        log_error("Unsupported VFIO API version: %d", version);
        close(fd);
        return NULL;
    }

    /* Check for Type1 IOMMU support */
    if (!ioctl(fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        log_error("Type1 IOMMU not supported");
        close(fd);
        return NULL;
    }

    container = calloc(1, sizeof(*container));
    if (!container) {
        close(fd);
        return NULL;
    }

    container->fd = fd;

    log_info("Created VFIO container");
    return container;
}

/*
 * Destroy VFIO container
 */
void vfio_container_destroy(struct vfio_container *container)
{
    if (!container)
        return;

    if (container->fd >= 0)
        close(container->fd);

    free(container);
    log_info("Destroyed VFIO container");
}

/*
 * Parse BDF (Bus:Device.Function) string
 */
static int parse_bdf(const char *bdf, int *domain, int *bus, int *slot, int *func)
{
    /* Format: XXXX:XX:XX.X or XX:XX.X */
    int ret;

    ret = sscanf(bdf, "%x:%x:%x.%d", domain, bus, slot, func);
    if (ret == 4)
        return 0;

    /* Try short format */
    *domain = 0;
    ret = sscanf(bdf, "%x:%x.%d", bus, slot, func);
    if (ret == 3)
        return 0;

    log_error("Invalid BDF format: %s (expected XXXX:XX:XX.X or XX:XX.X)", bdf);
    return -1;
}

/*
 * Open VFIO device
 */
struct vfio_dev* vfio_device_open(struct vfio_container *container, const char *bdf)
{
    struct vfio_dev *vdev;
    char path[64];
    int domain, bus, slot, func;
    int group_fd, device_fd;
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };

    if (parse_bdf(bdf, &domain, &bus, &slot, &func) < 0)
        return NULL;

    /* Find IOMMU group */
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%04x:%02x:%02x.%d/iommu_group",
             domain, bus, slot, func);

    /* For simplicity, assume IOMMU group is the same as BDF for now */
    /* In production, you need to read the symlink to get the actual group number */

    int group_num;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        log_error("Failed to open IOMMU group path");
        return NULL;
    }

    /* The symlink points to the group directory */
    fclose(fp);

    /* For this implementation, assume group = bus number */
    group_num = bus;

    /* Open group */
    snprintf(path, sizeof(path), "/dev/vfio/%d", group_num);
    group_fd = open(path, O_RDWR);
    if (group_fd < 0) {
        perror("open vfio group");
        log_error("Failed to open VFIO group %d", group_num);
        return NULL;
    }

    /* Get group status */
    if (ioctl(group_fd, VFIO_GROUP_GET_STATUS, &group_status) < 0) {
        perror("VFIO_GROUP_GET_STATUS");
        close(group_fd);
        return NULL;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        log_error("VFIO group %d is not viable", group_num);
        close(group_fd);
        return NULL;
    }

    /* Add group to container */
    if (ioctl(group_fd, VFIO_GROUP_SET_CONTAINER, &container->fd) < 0) {
        perror("VFIO_GROUP_SET_CONTAINER");
        close(group_fd);
        return NULL;
    }

    /* Set IOMMU type */
    if (ioctl(container->fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
        perror("VFIO_SET_IOMMU");
        ioctl(group_fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd);
        close(group_fd);
        return NULL;
    }

    /* Open device */
    snprintf(path, sizeof(path), "/dev/vfio/%d", group_num);
    device_fd = ioctl(group_fd, VFIO_GROUP_GET_DEVICE_FD, (char *)bdf);
    if (device_fd < 0) {
        perror("VFIO_GROUP_GET_DEVICE_FD");
        ioctl(group_fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd);
        close(group_fd);
        return NULL;
    }

    /* Allocate VFIO device */
    vdev = calloc(1, sizeof(*vdev));
    if (!vdev) {
        close(device_fd);
        ioctl(group_fd, VFIO_GROUP_UNSET_CONTAINER, &container->fd);
        close(group_fd);
        return NULL;
    }

    vdev->container_fd = container->fd;
    vdev->group_fd = group_fd;
    vdev->device_fd = device_fd;
    vdev->iommu_group = group_num;

    /* Get device info */
    struct vfio_device_info info = { .argsz = sizeof(info) };
    if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &info) < 0) {
        perror("VFIO_DEVICE_GET_INFO");
        vfio_device_close(vdev);
        return NULL;
    }

    vdev->num_regions = info.num_regions;
    vdev->num_irqs = info.num_irqs;

    /* Read PCI config space */
    vfio_pci_cfg_read(vdev, PCI_VENDOR_ID, &vdev->vendor_id, 2);
    vfio_pci_cfg_read(vdev, PCI_DEVICE_ID, &vdev->device_id, 2);

    log_info("Opened VFIO device %s (vendor=0x%04x, device=0x%04x)",
             bdf, vdev->vendor_id, vdev->device_id);

    return vdev;
}

/*
 * Close VFIO device
 */
void vfio_device_close(struct vfio_dev *vdev)
{
    int i;

    if (!vdev)
        return;

    /* Unmap regions */
    for (i = 0; i < vdev->num_regions; i++) {
        if (vdev->regions[i].hva)
            munmap(vdev->regions[i].hva, vdev->regions[i].size);
    }

    if (vdev->device_fd >= 0)
        close(vdev->device_fd);

    if (vdev->group_fd >= 0)
        ioctl(vdev->group_fd, VFIO_GROUP_UNSET_CONTAINER, &vdev->container_fd);

    if (vdev->group_fd >= 0)
        close(vdev->group_fd);

    free(vdev);
}

/*
 * Get device info
 */
int vfio_device_get_info(struct vfio_dev *vdev, struct vfio_device_info *info)
{
    if (ioctl(vdev->device_fd, VFIO_DEVICE_GET_INFO, info) < 0) {
        perror("VFIO_DEVICE_GET_INFO");
        return -1;
    }
    return 0;
}

/*
 * Get region info
 */
int vfio_device_get_region_info(struct vfio_dev *vdev, uint32_t index,
                                 struct vfio_region_info *info)
{
    info->argsz = sizeof(*info);
    info->index = index;

    if (ioctl(vdev->device_fd, VFIO_DEVICE_GET_REGION_INFO, info) < 0) {
        perror("VFIO_DEVICE_GET_REGION_INFO");
        return -1;
    }

    /* Cache region info */
    if (index < VFIO_MAX_REGIONS) {
        vdev->regions[index].index = index;
        vdev->regions[index].size = info->size;
        vdev->regions[index].offset = info->offset;
        vdev->regions[index].flags = info->flags;
    }

    return 0;
}

/*
 * Map region to guest
 */
int vfio_map_region(struct vfio_dev *vdev, uint32_t index, uint64_t gpa)
{
    struct vfio_region_info info;
    void *hva;

    if (index >= VFIO_MAX_REGIONS)
        return -1;

    /* Get region info */
    if (vfio_device_get_region_info(vdev, index, &info) < 0)
        return -1;

    /* Check if mappable */
    if (!(info.flags & VFIO_REGION_INFO_FLAG_MMAP)) {
        log_warn("Region %d is not mappable", index);
        return -1;
    }

    /* Map region */
    hva = mmap(NULL, info.size, PROT_READ | PROT_WRITE, MAP_SHARED,
               vdev->device_fd, info.offset);
    if (hva == MAP_FAILED) {
        perror("mmap vfio region");
        return -1;
    }

    vdev->regions[index].hva = hva;
    vdev->regions[index].gpa = gpa;

    log_info("Mapped VFIO region %d: GPA 0x%lx -> HVA %p (size=%ld)",
             index, gpa, hva, info.size);

    return 0;
}

/*
 * Unmap region
 */
int vfio_unmap_region(struct vfio_dev *vdev, uint32_t index)
{
    if (index >= VFIO_MAX_REGIONS)
        return -1;

    if (vdev->regions[index].hva) {
        munmap(vdev->regions[index].hva, vdev->regions[index].size);
        vdev->regions[index].hva = NULL;
    }

    return 0;
}

/*
 * Setup IRQs
 */
int vfio_setup_irqs(struct vfio_dev *vdev, uint32_t index, uint32_t type)
{
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info), .index = index };

    if (ioctl(vdev->device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info) < 0) {
        perror("VFIO_DEVICE_GET_IRQ_INFO");
        return -1;
    }

    if (index < VFIO_MAX_IRQS) {
        vdev->irqs[index].index = index;
        vdev->irqs[index].count = irq_info.count;
        vdev->irqs[index].type = type;
        vdev->irqs[index].enabled = 0;
    }

    log_debug("Setup VFIO IRQ index %d (count=%d)", index, irq_info.count);
    return 0;
}

/*
 * Enable IRQ
 */
int vfio_irq_enable(struct vfio_dev *vdev, uint32_t index, uint32_t subindex, int fd)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_ACTION_TRIGGER | VFIO_IRQ_SET_DATA_EVENTFD,
        .index = index,
        .start = subindex,
        .count = 1,
        .fd = fd,
    };

    if (ioctl(vdev->device_fd, VFIO_DEVICE_SET_IRQS, &irq_set) < 0) {
        perror("VFIO_DEVICE_SET_IRQS");
        return -1;
    }

    if (index < VFIO_MAX_IRQS) {
        vdev->irqs[index].fds[subindex] = fd;
        vdev->irqs[index].enabled = 1;
    }

    log_info("Enabled VFIO IRQ index %d subindex %d", index, subindex);
    return 0;
}

/*
 * Disable IRQ
 */
int vfio_irq_disable(struct vfio_dev *vdev, uint32_t index)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE,
        .index = index,
        .start = 0,
        .count = 0,
    };

    if (ioctl(vdev->device_fd, VFIO_DEVICE_SET_IRQS, &irq_set) < 0) {
        perror("VFIO_DEVICE_SET_IRQS");
        return -1;
    }

    if (index < VFIO_MAX_IRQS) {
        vdev->irqs[index].enabled = 0;
    }

    log_info("Disabled VFIO IRQ index %d", index);
    return 0;
}

/*
 * PCI config space read
 */
int vfio_pci_cfg_read(struct vfio_dev *vdev, uint32_t offset, void *data, size_t size)
{
    struct vfio_region_info info;
    uint8_t *cfg;
    int ret;

    /* PCI config space is region 0 */
    if (vfio_device_get_region_info(vdev, 0 /* PCI config */, &info) < 0)
        return -1;

    /* Read via config region */
    cfg = calloc(1, info.size);
    if (!cfg)
        return -1;

    ret = pread(vdev->device_fd, cfg, info.size, info.offset);
    if (ret < 0) {
        perror("pread vfio config");
        free(cfg);
        return -1;
    }

    memcpy(data, cfg + offset, size);
    free(cfg);

    return 0;
}

/*
 * PCI config space write
 */
int vfio_pci_cfg_write(struct vfio_dev *vdev, uint32_t offset, const void *data, size_t size)
{
    struct vfio_region_info info;
    uint8_t *cfg;
    int ret;

    /* PCI config space is region 0 */
    if (vfio_device_get_region_info(vdev, 0 /* PCI config */, &info) < 0)
        return -1;

    cfg = calloc(1, info.size);
    if (!cfg)
        return -1;

    /* Read current config */
    ret = pread(vdev->device_fd, cfg, info.size, info.offset);
    if (ret < 0) {
        perror("pread vfio config");
        free(cfg);
        return -1;
    }

    /* Modify and write back */
    memcpy(cfg + offset, data, size);
    ret = pwrite(vdev->device_fd, cfg, info.size, info.offset);
    if (ret < 0) {
        perror("pwrite vfio config");
        free(cfg);
        return -1;
    }

    free(cfg);
    return 0;
}

/*
 * VFIO device read handler
 */
static int vfio_device_read(struct device *dev, uint64_t offset, void *data, size_t size)
{
    struct vfio_dev *vdev = dev->data;
    int region, region_offset;
    uint64_t addr;

    /* Find which region this access belongs to */
    for (region = 1; region < vdev->num_regions; region++) {
        if (offset >= vdev->regions[region].gpa &&
            offset < (vdev->regions[region].gpa + vdev->regions[region].size)) {
            region_offset = offset - vdev->regions[region].gpa;

            if (vdev->regions[region].hva) {
                /* Memory mapped region */
                memcpy(data, (char *)vdev->regions[region].hva + region_offset, size);
                return 0;
            } else {
                /* Non-mapped region - read via pread */
                addr = vdev->regions[region].offset + region_offset;
                if (pread(vdev->device_fd, data, size, addr) < 0) {
                    perror("pread vfio region");
                    return -1;
                }
                return 0;
            }
        }
    }

    log_warn("VFIO read to unmapped offset 0x%lx", offset);
    return -1;
}

/*
 * VFIO device write handler
 */
static int vfio_device_write(struct device *dev, uint64_t offset, const void *data, size_t size)
{
    struct vfio_dev *vdev = dev->data;
    int region, region_offset;
    uint64_t addr;

    /* Find which region this access belongs to */
    for (region = 1; region < vdev->num_regions; region++) {
        if (offset >= vdev->regions[region].gpa &&
            offset < (vdev->regions[region].gpa + vdev->regions[region].size)) {
            region_offset = offset - vdev->regions[region].gpa;

            if (vdev->regions[region].hva) {
                /* Memory mapped region */
                memcpy((char *)vdev->regions[region].hva + region_offset, data, size);
                return 0;
            } else {
                /* Non-mapped region - write via pwrite */
                addr = vdev->regions[region].offset + region_offset;
                if (pwrite(vdev->device_fd, data, size, addr) < 0) {
                    perror("pwrite vfio region");
                    return -1;
                }
                return 0;
            }
        }
    }

    log_warn("VFIO write to unmapped offset 0x%lx", offset);
    return -1;
}

/*
 * Destroy VFIO device wrapper
 */
static void vfio_device_destroy_wrapper(struct device *dev)
{
    struct vfio_dev *vdev = dev->data;
    vfio_device_close(vdev);
    free(dev->name);
    free(dev);
}

static const struct device_ops vfio_device_ops = {
    .name = "vfio-device",
    .read = vfio_device_read,
    .write = vfio_device_write,
    .destroy = vfio_device_destroy_wrapper,
};

/*
 * Register VFIO device with VM
 */
int vfio_register_with_vm(struct vm *vm, struct vfio_dev *vdev, const char *bdf)
{
    struct device *dev;
    uint64_t gpa;
    int i, ret;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return -1;

    dev->name = strdup(bdf);
    dev->data = vdev;
    dev->ops = &vfio_device_ops;

    /* Map BARs to guest physical address space */
    gpa = 0xb000000;  /* Start GPA for VFIO devices */
    for (i = 1; i < vdev->num_regions && i <= 6; i++) {  /* BAR0-5 */
        if (vdev->regions[i].size == 0)
            continue;

        ret = vfio_map_region(vdev, i, gpa);
        if (ret < 0)
            continue;

        gpa += ALIGN_UP(vdev->regions[i].size, 0x10000);  /* 64K alignment */
    }

    /* Set device GPA range */
    dev->gpa_start = 0xb000000;
    dev->gpa_end = gpa - 1;
    dev->size = gpa - dev->gpa_start;

    /* Register with VM */
    ret = device_register(vm, dev);
    if (ret < 0) {
        free(dev->name);
        free(dev);
        return ret;
    }

    /* Setup IRQs */
    vfio_setup_irqs(vdev, VFIO_PCI_INTX_IRQ_INDEX, 0);
    vfio_setup_irqs(vdev, VFIO_PCI_MSIX_IRQ_INDEX, 0);

    log_info("Registered VFIO device %s with VM", bdf);
    return 0;
}
