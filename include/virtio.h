#ifndef VIBE_VMM_VIRTIO_H
#define VIBE_VMM_VIRTIO_H

#include <stdint.h>
#include <stddef.h>
#include "devices.h"

/* Virtio device IDs */
enum virtio_device_id {
    VIRTIO_ID_NET          = 1,
    VIRTIO_ID_BLOCK        = 2,
    VIRTIO_ID_CONSOLE      = 3,
    VIRTIO_ID_RNG          = 4,
};

/* Virtio status flags */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE    1
#define VIRTIO_CONFIG_S_DRIVER         2
#define VIRTIO_CONFIG_S_DRIVER_OK      4
#define VIRTIO_CONFIG_S_FEATURES_OK    8
#define VIRTIO_CONFIG_FAILED           0x80

/* Virtio feature flags */
#define VIRTIO_F_RING_INDIRECT_DESC    28
#define VIRTIO_F_RING_EVENT_IDX        29
#define VIRTIO_F_VERSION_1             32
#define VIRTIO_F_ACCESS_PLATFORM       33
#define VIRTIO_F_RING_PACKED           34

/* Common virtio feature flags */
#define VIRTIO_BLK_F_SEG_MAX          2
#define VIRTIO_BLK_F_BLK_SIZE         6
#define VIRTIO_BLK_F_FLUSH            9
#define VIRTIO_BLK_F_RO               5

#define VIRTIO_NET_F_CSUM             1
#define VIRTIO_NET_F_GSO              6
#define VIRTIO_NET_F_MRG_RXBUF        15

/* Virtio queue descriptor */
struct vring_desc {
    uint64_t addr;      /* Address (guest-physical) */
    uint32_t len;       /* Length */
    uint16_t flags;     /* The flags as indicated above */
    uint16_t next;      /* Next field if flags & NEXT */
};

/* Virtio descriptor flags */
#define VRING_DESC_F_NEXT      1
#define VRING_DESC_F_WRITE     2
#define VRING_DESC_F_INDIRECT  4

/* Virtio available ring */
struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];      /* Only if VIRTIO_RING_F_EVENT_IDX */
};

/* Virtio used ring */
struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
};

/* Virtio queue */
struct virtqueue {
    struct device *dev;

    /* Queue configuration */
    uint16_t index;
    uint16_t size;
    uint64_t desc_gpa;
    uint64_t avail_gpa;
    uint64_t used_gpa;

    /* Mapped queue (host side) */
    struct vring_desc *desc;
    struct vring_avail *avail;
    struct vring_used *used;

    /* Last seen indices */
    uint16_t last_avail_idx;
    uint16_t last_used_idx;

    /* Ready flag */
    int ready;

    /* Private data */
    void *priv;
};

/* Virtio device common configuration (MMIO layout) */
struct virtio_mmio_config {
    /* About 4KB */
    uint32_t magic_value;           /* 0x00: Magic value */
    uint32_t version;               /* 0x04: Version */
    uint32_t device_id;             /* 0x08: Device ID */
    uint32_t vendor_id;             /* 0x0C: Vendor ID */
    uint32_t device_features;       /* 0x10: Device features */
    uint32_t device_features_sel;   /* 0x14: Device features selector */
    uint32_t driver_features;       /* 0x18: Driver features */
    uint32_t driver_features_sel;   /* 0x1C: Driver features selector */
    uint32_t guest_page_size;       /* 0x20: Guest page size */
    uint32_t queue_sel;             /* 0x24: Queue select */
    uint32_t queue_num_max;         /* 0x28: Queue maximum size */
    uint32_t queue_num;             /* 0x2C: Queue size */
    uint32_t queue_ready;           /* 0x30: Queue ready */
    uint32_t queue_notify;          /* 0x34: Queue notify */
    uint32_t interrupt_status;      /* 0x38: Interrupt status */
    uint32_t interrupt_ack;         /* 0x3C: Interrupt acknowledge */
    uint32_t device_status;         /* 0x40: Device status */
    uint8_t  config[0x100];         /* 0x100+: Device-specific config */
    /* Queue notifications follow at offset 0x200 + queue_index * 2 */
};

/* Virtio device structure */
struct virtio_dev {
    struct device device;          /* Base device */

    /* Configuration */
    enum virtio_device_id device_id;
    uint32_t device_features;
    uint32_t driver_features;
    uint8_t  device_status;

    /* Queues */
    struct virtqueue queues[8];
    int num_queues;

    /* Queue notification handler */
    int (*queue_notify)(struct virtio_dev *vdev, struct virtqueue *vq);

    /* Config space read/write */
    int (*config_read)(struct virtio_dev *vdev, uint64_t offset, void *data, size_t size);
    int (*config_write)(struct virtio_dev *vdev, uint64_t offset, const void *data, size_t size);

    /* Private data */
    void *priv;
};

/* Virtio operations */
int virtio_init(struct virtio_dev *vdev, enum virtio_device_id id);
void virtio_cleanup(struct virtio_dev *vdev);

/* Queue operations */
int virtqueue_setup(struct virtqueue *vq, struct device *dev, uint16_t index);
void virtqueue_cleanup(struct virtqueue *vq);

/* Pop next available descriptor from queue */
struct vring_desc* virtqueue_pop(struct virtqueue *vq);

/* Push descriptor to used ring */
void virtqueue_push(struct virtqueue *vq, uint32_t id, uint32_t len);

/* Notify guest about used buffers */
void virtqueue_notify(struct virtqueue *vq);

/* MMIO access handlers */
int virtio_mmio_read(struct virtio_dev *vdev, uint64_t offset, void *data, size_t size);
int virtio_mmio_write(struct virtio_dev *vdev, uint64_t offset, const void *data, size_t size);

/* Get/Set private data */
static inline void* virtio_get_priv(struct virtio_dev *vdev) {
    return vdev->priv;
}

static inline void virtio_set_priv(struct virtio_dev *vdev, void *priv) {
    vdev->priv = priv;
}

#endif /* VIBE_VMM_VIRTIO_H */
