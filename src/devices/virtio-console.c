/*
 * Virtio console device
 */

#include "virtio.h"
#include "vm.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Virtio console configuration */
struct virtio_console_config {
    uint16_t cols;
    uint16_t rows;
    uint32_t max_nr_ports;
    uint32_t emerg_wr;
} PACKED;

/* Virtio console device state */
struct virtio_console_state {
    struct virtio_console_config config;
    struct virtqueue *rx_vq;
    struct virtqueue *tx_vq;
};

/* Default GPA for virtio console */
#define VIRTIO_CONSOLE_GPA  0xa000000
#define VIRTIO_CONSOLE_SIZE 0x1000

/*
 * Console config read
 */
static int virtio_console_config_read(struct virtio_dev *vdev,
                                       uint64_t offset,
                                       void *data, size_t size)
{
    struct virtio_console_state *s = vdev->priv;

    switch (offset) {
    case 0x00:  /* cols */
        *(uint16_t *)data = s->config.cols;
        break;
    case 0x02:  /* rows */
        *(uint16_t *)data = s->config.rows;
        break;
    case 0x04:  /* max_nr_ports */
        *(uint32_t *)data = s->config.max_nr_ports;
        break;
    case 0x10:  /* emerg_wr */
        *(uint32_t *)data = s->config.emerg_wr;
        break;
    default:
        memset(data, 0, size);
        break;
    }

    return 0;
}

/*
 * Console config write
 */
static int virtio_console_config_write(struct virtio_dev *vdev,
                                        uint64_t offset,
                                        const void *data, size_t size)
{
    struct virtio_console_state *s = vdev->priv;

    switch (offset) {
    case 0x00:  /* cols */
        s->config.cols = *(const uint16_t *)data;
        break;
    case 0x02:  /* rows */
        s->config.rows = *(const uint16_t *)data;
        break;
    default:
        break;
    }

    return 0;
}

/*
 * Handle queue notification
 */
static int virtio_console_queue_notify(struct virtio_dev *vdev,
                                        struct virtqueue *vq)
{
    struct vm *vm = vdev->device.vm;
    struct vring_desc *desc;
    void *hva;
    uint8_t *data;
    uint32_t len;

    /* Process TX queue (guest -> host) */
    while ((desc = virtqueue_pop(vq)) != NULL) {
        hva = vm_gpa_to_hva(vm, desc->addr, desc->len);
        if (!hva) {
            log_error("Failed to translate GPA 0x%lx", desc->addr);
            continue;
        }

        data = hva;
        len = desc->len;

        /* Write to stdout */
        for (uint32_t i = 0; i < len; i++) {
            putchar(data[i]);
        }
        fflush(stdout);

        /* Complete the request */
        virtqueue_push(vq, desc - vq->desc, len);
    }

    return 0;
}

/* Device operations */
static int virtio_console_read(struct device *dev, uint64_t offset,
                                void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_read(vdev, offset, data, size);
}

static int virtio_console_write(struct device *dev, uint64_t offset,
                                 const void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_write(vdev, offset, data, size);
}

static void virtio_console_destroy(struct device *dev)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    virtio_cleanup(vdev);
    free(vdev->priv);
    free(vdev);
}

static const struct device_ops virtio_console_ops = {
    .name = "virtio-console",
    .read = virtio_console_read,
    .write = virtio_console_write,
    .destroy = virtio_console_destroy,
};

/*
 * Create virtio console device
 */
struct device* virtio_console_create(void)
{
    struct virtio_dev *vdev;
    struct virtio_console_state *s;

    vdev = calloc(1, sizeof(*vdev));
    if (!vdev)
        return NULL;

    s = calloc(1, sizeof(*s));
    if (!s) {
        free(vdev);
        return NULL;
    }

    /* Initialize state */
    s->config.cols = 80;
    s->config.rows = 25;
    s->config.max_nr_ports = 1;
    s->config.emerg_wr = 0;

    /* Initialize virtio device */
    virtio_init(vdev, VIRTIO_ID_CONSOLE);

    vdev->priv = s;
    vdev->config_read = virtio_console_config_read;
    vdev->config_write = virtio_console_config_write;
    vdev->queue_notify = virtio_console_queue_notify;

    /* Setup device */
    vdev->device.ops = &virtio_console_ops;
    vdev->device.name = strdup("virtio-console");
    vdev->device.data = vdev;
    vdev->device.gpa_start = VIRTIO_CONSOLE_GPA;
    vdev->device.gpa_end = VIRTIO_CONSOLE_GPA + VIRTIO_CONSOLE_SIZE - 1;
    vdev->device.size = VIRTIO_CONSOLE_SIZE;

    log_info("Created virtio console at GPA 0x%x", VIRTIO_CONSOLE_GPA);
    return &vdev->device;
}
