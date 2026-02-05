/*
 * Virtio block device
 */

#include "virtio.h"
#include "vm.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* Virtio block features */
#define VIRTIO_BLK_F_BARRIER   0
#define VIRTIO_BLK_F_SIZE_MAX  1
#define VIRTIO_BLK_F_SEG_MAX   2
#define VIRTIO_BLK_F_GEOMETRY  4
#define VIRTIO_BLK_F_RO        5
#define VIRTIO_BLK_F_BLK_SIZE  6
#define VIRTIO_BLK_F_FLUSH     9

/* Virtio block request types */
#define VIRTIO_BLK_T_IN        0
#define VIRTIO_BLK_T_OUT       1
#define VIRTIO_BLK_T_FLUSH     4

/* Virtio block status */
#define VIRTIO_BLK_S_OK        0
#define VIRTIO_BLK_S_IOERR     1
#define VIRTIO_BLK_S_UNSUPP    2

/* Virtio block configuration */
struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct {
        uint16_t cylinders;
        uint8_t  heads;
        uint8_t  sectors;
    } geometry;
    uint32_t blk_size;
} PACKED;

/* Virtio block request */
struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
};

/* Virtio block device state */
struct virtio_blk_state {
    struct virtio_blk_config config;
    int      disk_fd;
    uint64_t disk_size;
    uint32_t blk_size;
};

/* Default GPA for virtio block */
#define VIRTIO_BLOCK_GPA  0xa001000
#define VIRTIO_BLOCK_SIZE 0x1000

/*
 * Block config read
 */
static int virtio_blk_config_read(struct virtio_dev *vdev,
                                   uint64_t offset,
                                   void *data, size_t size)
{
    struct virtio_blk_state *s = vdev->priv;
    struct virtio_blk_config *cfg = &s->config;

    switch (offset) {
    case 0x00:  /* capacity (low) */
        *(uint32_t *)data = cfg->capacity & 0xFFFFFFFF;
        break;
    case 0x04:  /* capacity (high) */
        *(uint32_t *)data = (cfg->capacity >> 32) & 0xFFFFFFFF;
        break;
    case 0x08:  /* size_max */
        *(uint32_t *)data = cfg->size_max;
        break;
    case 0x0C:  /* seg_max */
        *(uint32_t *)data = cfg->seg_max;
        break;
    case 0x10:  /* geometry */
        if (size == 1) {
            *(uint8_t *)data = *(uint8_t *)((char *)cfg + offset);
        } else {
            memcpy(data, (char *)cfg + offset, size);
        }
        break;
    case 0x18:  /* blk_size */
        *(uint32_t *)data = cfg->blk_size;
        break;
    default:
        memset(data, 0, size);
        break;
    }

    return 0;
}

/*
 * Handle queue notification
 */
static int virtio_blk_queue_notify(struct virtio_dev *vdev,
                                    struct virtqueue *vq)
{
    struct vm *vm = vdev->device.vm;
    struct virtio_blk_state *s = vdev->priv;
    struct vring_desc *desc;
    struct virtio_blk_req req;
    void *req_hva, *data_hva, *status_hva;
    uint8_t status;
    ssize_t ret;

    /* Pop request descriptor */
    desc = virtqueue_pop(vq);
    if (!desc)
        return 0;

    /* Read request header */
    req_hva = vm_gpa_to_hva(vm, desc->addr, sizeof(req));
    if (!req_hva) {
        log_error("Failed to translate request GPA");
        return -1;
    }

    memcpy(&req, req_hva, sizeof(req));

    /* Get data descriptor (next in chain) */
    if (!(desc->flags & VRING_DESC_F_NEXT)) {
        log_error("Block request has no data descriptor");
        return -1;
    }

    desc = &vq->desc[desc->next];
    data_hva = vm_gpa_to_hva(vm, desc->addr, desc->len);
    if (!data_hva) {
        log_error("Failed to translate data GPA");
        return -1;
    }

    /* Get status descriptor */
    if (!(desc->flags & VRING_DESC_F_NEXT)) {
        log_error("Data descriptor has no status descriptor");
        return -1;
    }

    desc = &vq->desc[desc->next];
    status_hva = vm_gpa_to_hva(vm, desc->addr, 1);
    if (!status_hva) {
        log_error("Failed to translate status GPA");
        return -1;
    }

    /* Handle request */
    status = VIRTIO_BLK_S_OK;

    switch (req.type) {
    case VIRTIO_BLK_T_IN:
        /* Read from disk */
        ret = pread(s->disk_fd, data_hva, desc->len, req.sector * s->blk_size);
        if (ret < 0) {
            perror("pread");
            status = VIRTIO_BLK_S_IOERR;
        } else if ((size_t)ret != desc->len) {
            log_warn("Short read: %zd != %d", ret, desc->len);
        }
        break;

    case VIRTIO_BLK_T_OUT:
        /* Write to disk */
        ret = pwrite(s->disk_fd, data_hva, desc->len, req.sector * s->blk_size);
        if (ret < 0) {
            perror("pwrite");
            status = VIRTIO_BLK_S_IOERR;
        } else if ((size_t)ret != desc->len) {
            log_warn("Short write: %zd != %d", ret, desc->len);
        }
        break;

    case VIRTIO_BLK_T_FLUSH:
        /* Flush - not implemented */
        status = VIRTIO_BLK_S_OK;
        break;

    default:
        log_warn("Unknown block request type: %u", req.type);
        status = VIRTIO_BLK_S_UNSUPP;
        break;
    }

    /* Write status */
    *(uint8_t *)status_hva = status;

    /* Complete request */
    virtqueue_push(vq, 0, 1);

    return 0;
}

/* Device operations */
static int virtio_blk_read(struct device *dev, uint64_t offset,
                            void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_read(vdev, offset, data, size);
}

static int virtio_blk_write(struct device *dev, uint64_t offset,
                             const void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_write(vdev, offset, data, size);
}

static void virtio_blk_destroy(struct device *dev)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    struct virtio_blk_state *s = vdev->priv;

    if (s && s->disk_fd >= 0)
        close(s->disk_fd);

    virtio_cleanup(vdev);
    free(s);
    free(vdev);
}

static const struct device_ops virtio_blk_ops = {
    .name = "virtio-block",
    .read = virtio_blk_read,
    .write = virtio_blk_write,
    .destroy = virtio_blk_destroy,
};

/*
 * Create virtio block device
 */
struct device* virtio_blk_create(const char *disk_path)
{
    struct virtio_dev *vdev;
    struct virtio_blk_state *s;
    struct stat st;

    vdev = calloc(1, sizeof(*vdev));
    if (!vdev)
        return NULL;

    s = calloc(1, sizeof(*s));
    if (!s) {
        free(vdev);
        return NULL;
    }

    /* Open disk image */
    s->disk_fd = open(disk_path, O_RDWR);
    if (s->disk_fd < 0) {
        /* Try read-only */
        s->disk_fd = open(disk_path, O_RDONLY);
        if (s->disk_fd < 0) {
            perror("open disk image");
            free(s);
            free(vdev);
            return NULL;
        }
        log_info("Opened disk image in read-only mode");
    }

    /* Get disk size */
    if (fstat(s->disk_fd, &st) < 0) {
        perror("fstat");
        close(s->disk_fd);
        free(s);
        free(vdev);
        return NULL;
    }

    s->disk_size = st.st_size;
    s->blk_size = 512;

    /* Initialize config */
    s->config.capacity = s->disk_size / s->blk_size;
    s->config.size_max = 65535;  /* Maximum segment size */
    s->config.seg_max = 128;     /* Maximum segments in request */
    s->config.blk_size = s->blk_size;

    log_info("Disk image: %s (%ld MB, %ld sectors)",
             disk_path, s->disk_size / (1024 * 1024), s->config.capacity);

    /* Initialize virtio device */
    virtio_init(vdev, VIRTIO_ID_BLOCK);

    vdev->priv = s;
    vdev->config_read = virtio_blk_config_read;
    vdev->config_write = NULL;
    vdev->queue_notify = virtio_blk_queue_notify;

    /* Setup device */
    vdev->device.ops = &virtio_blk_ops;
    vdev->device.name = strdup("virtio-block");
    vdev->device.data = vdev;
    vdev->device.gpa_start = VIRTIO_BLOCK_GPA;
    vdev->device.gpa_end = VIRTIO_BLOCK_GPA + VIRTIO_BLOCK_SIZE - 1;
    vdev->device.size = VIRTIO_BLOCK_SIZE;

    log_info("Created virtio block at GPA 0x%x", VIRTIO_BLOCK_GPA);
    return &vdev->device;
}
