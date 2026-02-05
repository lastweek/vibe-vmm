/*
 * Virtio network device
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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>

#ifdef __linux__
#include <linux/if.h>
#include <linux/if_tun.h>
#else
/* Non-Linux platforms don't have TAP support */
#include <net/if.h>
#define IFF_TAP 0x0001
#define IFF_NO_PI 0x1000
#define TUNSETIFF  _IOR('T', 202, int)
#endif

/* Virtio net configuration */
struct virtio_net_config {
    uint8_t  mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
} PACKED;

/* Virtio net header */
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
};

/* Virtio net device state */
struct virtio_net_state {
    struct virtio_net_config config;
    int      tap_fd;
    char     tap_name[IFNAMSIZ];
};

/* Default GPA for virtio network */
#define VIRTIO_NET_GPA   0xa002000
#define VIRTIO_NET_SIZE  0x1000

/*
 * Open TAP device
 */
static int open_tap(const char *ifname)
{
    struct ifreq ifr;
    int fd, ret;

    fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) {
        perror("open /dev/net/tun");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (ifname) {
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    }

    ret = ioctl(fd, TUNSETIFF, (void *)&ifr);
    if (ret < 0) {
        perror("ioctl TUNSETIFF");
        close(fd);
        return -1;
    }

    log_info("Opened TAP device: %s", ifr.ifr_name);
    return fd;
}

/*
 * Net config read
 */
static int virtio_net_config_read(struct virtio_dev *vdev,
                                   uint64_t offset,
                                   void *data, size_t size)
{
    struct virtio_net_state *s = vdev->priv;
    struct virtio_net_config *cfg = &s->config;

    switch (offset) {
    case 0x00:  /* MAC address */
        if (size <= 6)
            memcpy(data, cfg->mac, size);
        break;
    case 0x06:  /* status */
        *(uint16_t *)data = cfg->status;
        break;
    case 0x08:  /* max_virtqueue_pairs */
        *(uint16_t *)data = cfg->max_virtqueue_pairs;
        break;
    default:
        memset(data, 0, size);
        break;
    }

    return 0;
}

/*
 * Net config write
 */
static int virtio_net_config_write(struct virtio_dev *vdev,
                                    uint64_t offset,
                                    const void *data, size_t size)
{
    struct virtio_net_state *s = vdev->priv;
    struct virtio_net_config *cfg = &s->config;

    switch (offset) {
    case 0x00:  /* MAC address (guest can set this) */
        if (size <= 6)
            memcpy(cfg->mac, data, size);
        break;
    case 0x06:  /* status */
        cfg->status = *(const uint16_t *)data;
        break;
    default:
        break;
    }

    return 0;
}

/*
 * Handle RX queue (host -> guest)
 */
static int virtio_net_handle_rx(struct virtio_dev *vdev,
                                 struct virtqueue *vq)
{
    struct vm *vm = vdev->device.vm;
    struct virtio_net_state *s = vdev->priv;
    struct vring_desc *desc;
    struct virtio_net_hdr *hdr;
    uint8_t *data;
    ssize_t ret;

    /* Pop descriptor */
    desc = virtqueue_pop(vq);
    if (!desc)
        return 0;

    /* Get header address */
    hdr = vm_gpa_to_hva(vm, desc->addr, sizeof(*hdr));
    if (!hdr) {
        log_error("Failed to translate header GPA");
        return -1;
    }

    memset(hdr, 0, sizeof(*hdr));

    /* Get data address (next in chain) */
    if (!(desc->flags & VRING_DESC_F_NEXT)) {
        log_error("RX descriptor has no data descriptor");
        return -1;
    }

    desc = &vq->desc[desc->next];
    data = vm_gpa_to_hva(vm, desc->addr, desc->len);
    if (!data) {
        log_error("Failed to translate data GPA");
        return -1;
    }

    /* Read packet from TAP */
    ret = read(s->tap_fd, data, desc->len);
    if (ret < 0) {
        if (errno != EAGAIN)
            perror("read tap");
        return -1;
    }

    /* Complete request */
    virtqueue_push(vq, 0, sizeof(*hdr) + ret);

    return 0;
}

/*
 * Handle TX queue (guest -> host)
 */
static int virtio_net_handle_tx(struct virtio_dev *vdev,
                                 struct virtqueue *vq)
{
    struct vm *vm = vdev->device.vm;
    struct virtio_net_state *s = vdev->priv;
    struct vring_desc *desc;
    struct virtio_net_hdr *hdr;
    uint8_t *data;
    ssize_t ret;

    /* Pop descriptor */
    desc = virtqueue_pop(vq);
    if (!desc)
        return 0;

    /* Get header address */
    hdr = vm_gpa_to_hva(vm, desc->addr, sizeof(*hdr));
    if (!hdr) {
        log_error("Failed to translate header GPA");
        return -1;
    }

    /* Get data address (next in chain) */
    if (!(desc->flags & VRING_DESC_F_NEXT)) {
        log_error("TX descriptor has no data descriptor");
        return -1;
    }

    desc = &vq->desc[desc->next];
    data = vm_gpa_to_hva(vm, desc->addr, desc->len);
    if (!data) {
        log_error("Failed to translate data GPA");
        return -1;
    }

    /* Write packet to TAP */
    ret = write(s->tap_fd, data, desc->len);
    if (ret < 0) {
        perror("write tap");
        return -1;
    }

    /* Complete request */
    virtqueue_push(vq, 0, 0);

    return 0;
}

/*
 * Handle queue notification
 */
static int virtio_net_queue_notify(struct virtio_dev *vdev,
                                    struct virtqueue *vq)
{
    /* Handle based on queue index */
    if (vq->index == 0)
        return virtio_net_handle_rx(vdev, vq);
    else if (vq->index == 1)
        return virtio_net_handle_tx(vdev, vq);

    return 0;
}

/* Device operations */
static int virtio_net_read(struct device *dev, uint64_t offset,
                            void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_read(vdev, offset, data, size);
}

static int virtio_net_write(struct device *dev, uint64_t offset,
                             const void *data, size_t size)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    return virtio_mmio_write(vdev, offset, data, size);
}

static void virtio_net_destroy(struct device *dev)
{
    struct virtio_dev *vdev = container_of(dev, struct virtio_dev, device);
    struct virtio_net_state *s = vdev->priv;

    if (s && s->tap_fd >= 0)
        close(s->tap_fd);

    virtio_cleanup(vdev);
    free(s);
    free(vdev);
}

static const struct device_ops virtio_net_ops = {
    .name = "virtio-net",
    .read = virtio_net_read,
    .write = virtio_net_write,
    .destroy = virtio_net_destroy,
};

/*
 * Create virtio network device
 */
struct device* virtio_net_create(const char *tap_name)
{
    struct virtio_dev *vdev;
    struct virtio_net_state *s;

    vdev = calloc(1, sizeof(*vdev));
    if (!vdev)
        return NULL;

    s = calloc(1, sizeof(*s));
    if (!s) {
        free(vdev);
        return NULL;
    }

    /* Open TAP device */
    s->tap_fd = open_tap(tap_name);
    if (s->tap_fd < 0) {
        free(s);
        free(vdev);
        return NULL;
    }

    /* Set non-blocking */
    int flags = fcntl(s->tap_fd, F_GETFL, 0);
    fcntl(s->tap_fd, F_SETFL, flags | O_NONBLOCK);

    /* Initialize config */
    s->config.mac[0] = 0x02;
    s->config.mac[1] = 0x00;
    s->config.mac[2] = 0x00;
    s->config.mac[3] = 0x00;
    s->config.mac[4] = 0x00;
    s->config.mac[5] = 0x01;
    s->config.status = 0x01;  /* Link up */
    s->config.max_virtqueue_pairs = 1;

    /* Initialize virtio device */
    virtio_init(vdev, VIRTIO_ID_NET);

    vdev->priv = s;
    vdev->config_read = virtio_net_config_read;
    vdev->config_write = virtio_net_config_write;
    vdev->queue_notify = virtio_net_queue_notify;

    /* Setup device */
    vdev->device.ops = &virtio_net_ops;
    vdev->device.name = strdup("virtio-net");
    vdev->device.data = vdev;
    vdev->device.gpa_start = VIRTIO_NET_GPA;
    vdev->device.gpa_end = VIRTIO_NET_GPA + VIRTIO_NET_SIZE - 1;
    vdev->device.size = VIRTIO_NET_SIZE;

    log_info("Created virtio network at GPA 0x%x", VIRTIO_NET_GPA);
    return &vdev->device;
}
