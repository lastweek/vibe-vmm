/*
 * MMIO debug console - UART16550-like serial port
 */

#include "devices.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

/* UART16550 register offsets */
#define UART_RX         0   /* Receive buffer (read) */
#define UART_TX         0   /* Transmit buffer (write) */
#define UART_IER        1   /* Interrupt enable */
#define UART_IIR        2   /* Interrupt identification (read) */
#define UART_FCR        2   /* FIFO control (write) */
#define UART_LCR        3   /* Line control */
#define UART_MCR        4   /* Modem control */
#define UART_LSR        5   /* Line status */
#define UART_MSR        6   /* Modem status */
#define UART_SCR        7   /* Scratch register */
#define UART_DLL        0   /* Divisor latch low (if DLAB=1) */
#define UART_DLM        1   /* Divisor latch high (if DLAB=1) */

/* LSR bits */
#define UART_LSR_DR     0x01  /* Data ready */
#define UART_LSR_THRE   0x20  /* Transmit-hold-register empty */
#define UART_LSR_TEMT   0x40  /* Transmitter empty */

/* IIR bits */
#define UART_IIR_NO_INT 0x01  /* No interrupt pending */

/* Device state */
struct mmio_console_state {
    uint8_t  rx_buf;       /* Receive buffer */
    uint8_t  tx_buf;       /* Transmit buffer */
    uint8_t  ier;          /* Interrupt enable */
    uint8_t  iir;          /* Interrupt identification */
    uint8_t  lcr;          /* Line control */
    uint8_t  mcr;          /* Modem control */
    uint8_t  lsr;          /* Line status */
    uint8_t  msr;          /* Modem status */
    uint8_t  scr;          /* Scratch */
    uint8_t  dll;          /* Divisor latch low */
    uint8_t  dlm;          /* Divisor latch high */
    int      dlab;         /* Divisor latch access bit */
    int      stdin_fd;     /* stdin file descriptor */
};

/* Default MMIO console GPA */
#define MMIO_CONSOLE_GPA  0x9000000
#define MMIO_CONSOLE_SIZE 0x1000

/*
 * MMIO console read handler
 */
static int mmio_console_read(struct device *dev, uint64_t offset,
                              void *data, size_t size)
{
    struct mmio_console_state *s = dev->data;
    uint8_t val = 0;

    switch (offset) {
    case UART_RX:
        /* Check DLAB */
        if (s->dlab) {
            val = s->dll;
        } else {
            val = s->rx_buf;
            s->lsr &= ~UART_LSR_DR;
        }
        break;

    case UART_IER:
        if (s->dlab) {
            val = s->dlm;
        } else {
            val = s->ier;
        }
        break;

    case UART_IIR:
        val = s->iir;
        break;

    case UART_LCR:
        val = s->lcr;
        break;

    case UART_MCR:
        val = s->mcr;
        break;

    case UART_LSR:
        val = s->lsr;
        break;

    case UART_MSR:
        val = s->msr;
        break;

    case UART_SCR:
        val = s->scr;
        break;

    default:
        log_debug("MMIO console: read from unknown offset %ld", offset);
        break;
    }

    *(uint8_t *)data = val;
    return 0;
}

/*
 * MMIO console write handler
 */
static int mmio_console_write(struct device *dev, uint64_t offset,
                               const void *data, size_t size)
{
    struct mmio_console_state *s = dev->data;
    uint8_t val = *(const uint8_t *)data;

    switch (offset) {
    case UART_TX:
        if (s->dlab) {
            s->dll = val;
        } else {
            /* Write character to stdout */
            putchar(val);
            fflush(stdout);
            s->lsr |= UART_LSR_TEMT | UART_LSR_THRE;
        }
        break;

    case UART_IER:
        if (s->dlab) {
            s->dlm = val;
        } else {
            s->ier = val;
        }
        break;

    case UART_FCR:
        /* FIFO control - not implemented */
        break;

    case UART_LCR:
        s->lcr = val;
        s->dlab = (val & 0x80) ? 1 : 0;
        break;

    case UART_MCR:
        s->mcr = val;
        break;

    case UART_SCR:
        s->scr = val;
        break;

    default:
        log_debug("MMIO console: write to unknown offset %ld", offset);
        break;
    }

    return 0;
}

/*
 * Destroy MMIO console
 */
static void mmio_console_destroy(struct device *dev)
{
    struct mmio_console_state *s = dev->data;

    if (s && s->stdin_fd >= 0)
        close(s->stdin_fd);

    log_info("MMIO console destroyed");
}

/* Device operations */
static const struct device_ops mmio_console_ops = {
    .name = "mmio-console",
    .read = mmio_console_read,
    .write = mmio_console_write,
    .destroy = mmio_console_destroy,
};

/*
 * Create MMIO debug console device
 */
struct device* mmio_console_create(void)
{
    struct device *dev;
    struct mmio_console_state *s;
    struct termios tc;

    dev = device_create("mmio-console", sizeof(*s));
    if (!dev)
        return NULL;

    s = dev->data;
    memset(s, 0, sizeof(*s));

    /* Initialize UART state */
    s->lsr = UART_LSR_TEMT | UART_LSR_THRE;  /* Transmitter empty */
    s->iir = UART_IIR_NO_INT;
    s->stdin_fd = -1;

    /* Setup stdin for non-blocking reads (optional) */
    s->stdin_fd = STDIN_FILENO;
    if (isatty(s->stdin_fd)) {
        tcgetattr(s->stdin_fd, &tc);
        /* Set raw mode - but save this for later implementation */
        /* For now, just use blocking reads */
    }

    dev->ops = &mmio_console_ops;
    dev->gpa_start = MMIO_CONSOLE_GPA;
    dev->gpa_end = MMIO_CONSOLE_GPA + MMIO_CONSOLE_SIZE - 1;
    dev->size = MMIO_CONSOLE_SIZE;

    log_info("Created MMIO console at GPA 0x%x", MMIO_CONSOLE_GPA);
    return dev;
}
