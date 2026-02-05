/*
 * Virtio common framework
 */

#include "virtio.h"
#include "utils.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Virtio MMIO magic value */
#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976

/* Register access helper */
static inline uint32_t virtio_read_reg(uint32_t *reg, uint32_t offset)
{
    return *(volatile uint32_t *)((volatile char *)reg + offset);
}

static inline void virtio_write_reg(uint32_t *reg, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((volatile char *)reg + offset) = val;
}

/*
 * Initialize virtio device
 */
int virtio_init(struct virtio_dev *vdev, enum virtio_device_id id)
{
    memset(vdev, 0, sizeof(*vdev));

    vdev->device_id = id;
    vdev->device_features = (1U << VIRTIO_F_VERSION_1);
    vdev->driver_features = 0;
    vdev->device_status = 0;
    vdev->num_queues = 0;

    log_debug("Initialized virtio device %d", id);
    return 0;
}

/*
 * Cleanup virtio device
 */
void virtio_cleanup(struct virtio_dev *vdev)
{
    int i;

    for (i = 0; i < vdev->num_queues; i++) {
        virtqueue_cleanup(&vdev->queues[i]);
    }

    log_debug("Cleaned up virtio device %d", vdev->device_id);
}

/*
 * Setup virtqueue
 */
int virtqueue_setup(struct virtqueue *vq, struct device *dev, uint16_t index)
{
    memset(vq, 0, sizeof(*vq));

    vq->dev = dev;
    vq->index = index;
    vq->size = 0;
    vq->ready = 0;
    vq->last_avail_idx = 0;
    vq->last_used_idx = 0;

    log_debug("Setup virtqueue %d for device %s", index, dev->name);
    return 0;
}

/*
 * Cleanup virtqueue
 */
void virtqueue_cleanup(struct virtqueue *vq)
{
    vq->ready = 0;
    vq->desc = NULL;
    vq->avail = NULL;
    vq->used = NULL;
}

/*
 * Pop next available descriptor from queue
 */
struct vring_desc* virtqueue_pop(struct virtqueue *vq)
{
    struct vring_avail *avail;
    uint16_t avail_idx;
    uint16_t desc_idx;
    struct vring_desc *desc;

    if (!vq->ready || !vq->desc || !vq->avail)
        return NULL;

    avail = vq->avail;
    avail_idx = avail->idx;

    if (vq->last_avail_idx == avail_idx)
        return NULL;  /* No new descriptors */

    desc_idx = avail->ring[vq->last_avail_idx % vq->size];
    desc = &vq->desc[desc_idx];

    vq->last_avail_idx++;

    return desc;
}

/*
 * Push descriptor to used ring
 */
void virtqueue_push(struct virtqueue *vq, uint32_t id, uint32_t len)
{
    struct vring_used *used;
    uint16_t used_idx;

    if (!vq->ready || !vq->used)
        return;

    used = vq->used;
    used_idx = used->idx;

    used->ring[used_idx % vq->size].id = id;
    used->ring[used_idx % vq->size].len = len;

    used->idx++;
    vq->last_used_idx++;

    /* Notify guest */
    virtqueue_notify(vq);
}

/*
 * Notify guest about used buffers
 */
void virtqueue_notify(struct virtqueue *vq)
{
    /* This would trigger an interrupt to the guest */
    /* For now, we just set a flag that the hypervisor checks */
    device_assert_irq(vq->dev);
}

/*
 * Handle virtio MMIO read
 */
int virtio_mmio_read(struct virtio_dev *vdev, uint64_t offset,
                      void *data, size_t size)
{
    uint32_t val = 0;

    /* Most registers are 32-bit */
    if (size != 4) {
        log_warn("Virtio: non-32-bit read");
        return -1;
    }

    switch (offset) {
    case 0x00:  /* Magic value */
        val = VIRTIO_MMIO_MAGIC_VALUE;
        break;

    case 0x04:  /* Version */
        val = 1;  /* Legacy virtio */
        break;

    case 0x08:  /* Device ID */
        val = vdev->device_id;
        break;

    case 0x0C:  /* Vendor ID */
        val = 0;  /* No vendor ID */
        break;

    case 0x10:  /* Device features */
        val = vdev->device_features;
        break;

    case 0x14:  /* Device features selector */
        val = 0;
        break;

    case 0x20:  /* Queue num max */
        val = 32;  /* Maximum queue size */
        break;

    case 0x24:  /* Queue num */
        val = vdev->queues[vdev->num_queues > 0 ? vdev->num_queues - 1 : 0].size;
        break;

    case 0x30:  /* Queue ready */
        val = vdev->queues[vdev->num_queues > 0 ? vdev->num_queues - 1 : 0].ready;
        break;

    case 0x38:  /* Interrupt status */
        val = 0x01;  /* Interrupt pending */
        break;

    case 0x40:  /* Device status */
        val = vdev->device_status;
        break;

    default:
        if (offset >= 0x100 && vdev->config_read) {
            /* Device-specific config */
            return vdev->config_read(vdev, offset - 0x100, data, size);
        } else {
            log_debug("Virtio: read from unknown offset 0x%lx", offset);
        }
        break;
    }

    *(uint32_t *)data = val;
    return 0;
}

/*
 * Handle virtio MMIO write
 */
int virtio_mmio_write(struct virtio_dev *vdev, uint64_t offset,
                       const void *data, size_t size)
{
    uint32_t val = *(const uint32_t *)data;

    if (size != 4) {
        log_warn("Virtio: non-32-bit write");
        return -1;
    }

    switch (offset) {
    case 0x14:  /* Device features selector */
        /* Not implemented */
        break;

    case 0x18:  /* Driver features */
        vdev->driver_features = val;
        break;

    case 0x1C:  /* Driver features selector */
        /* Not implemented */
        break;

    case 0x20:  /* Guest page size */
        /* Page size - not used in modern virtio */
        break;

    case 0x24:  /* Queue select */
        /* Select queue - not implemented yet */
        break;

    case 0x28:  /* Queue num */
        /* Set queue size - not implemented yet */
        break;

    case 0x30:  /* Queue ready */
        /* Set queue ready - not implemented yet */
        break;

    case 0x34:  /* Queue notify */
        /* Queue notification - handle this */
        if (vdev->queue_notify) {
            struct virtqueue *vq = &vdev->queues[val & 0xFF];
            vdev->queue_notify(vdev, vq);
        }
        break;

    case 0x38:  /* Interrupt ACK */
        /* Acknowledge interrupt */
        device_deassert_irq(&vdev->device);
        break;

    case 0x40:  /* Device status */
        vdev->device_status = val;

        /* Check for driver OK */
        if (val & VIRTIO_CONFIG_S_DRIVER_OK) {
            log_info("Virtio device %d: driver OK", vdev->device_id);
        }
        break;

    default:
        if (offset >= 0x100 && vdev->config_write) {
            /* Device-specific config */
            return vdev->config_write(vdev, offset - 0x100, data, size);
        } else {
            log_debug("Virtio: write to unknown offset 0x%lx", offset);
        }
        break;
    }

    return 0;
}
