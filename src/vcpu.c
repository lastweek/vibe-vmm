/*
 * vCPU management and VM exit handling
 */

#include "vcpu.h"
#include "hypervisor.h"
#include "utils.h"
#include "devices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/*
 * vCPU thread function
 */
static void* vcpu_thread_func(void *arg)
{
    struct vcpu *vcpu = arg;
    int ret;

    log_debug("vCPU %d thread started", vcpu->index);

    while (!vcpu->should_stop) {
        ret = vcpu_run(vcpu);
        if (ret < 0) {
            if (errno == EINTR)
                continue;  /* Signal interrupted */
            log_error("vCPU %d run failed", vcpu->index);
            break;
        }

        /* Check for exit */
        struct hv_exit exit;
        ret = hv_get_exit(vcpu->hv_vcpu, &exit);
        if (ret < 0) {
            log_error("Failed to get exit info");
            break;
        }

        ret = vcpu_handle_exit(vcpu, &exit);
        if (ret < 0) {
            log_error("Failed to handle exit");
            break;
        }
    }

    log_debug("vCPU %d thread stopped", vcpu->index);
    return NULL;
}

/*
 * Create a vCPU
 */
struct vcpu* vcpu_create(struct vm *vm, int index)
{
    struct vcpu *vcpu;

    vcpu = calloc(1, sizeof(*vcpu));
    if (!vcpu) {
        log_error("Failed to allocate vCPU");
        return NULL;
    }

    vcpu->hv_vcpu = hv_create_vcpu(vm->hv_vm, index);
    if (!vcpu->hv_vcpu) {
        log_error("Failed to create hypervisor vCPU");
        free(vcpu);
        return NULL;
    }

    vcpu->vm = vm;
    vcpu->index = index;
    vcpu->state = VCPU_STATE_STOPPED;
    vcpu->should_stop = 0;

    /* Initialize statistics counters */
    vcpu->exit_count = 0;
    vcpu->io_count = 0;
    vcpu->mmio_count = 0;
    vcpu->halt_count = 0;
    vcpu->shutdown_count = 0;
    vcpu->exception_count = 0;
    vcpu->canceled_count = 0;
    vcpu->vtimer_count = 0;
    vcpu->unknown_count = 0;
    vcpu->total_run_time_us = 0;
    vcpu->instructions_executed = 0;

    log_info("vCPU %d created", index);
    return vcpu;
}

/*
 * Destroy a vCPU
 */
void vcpu_destroy(struct vcpu *vcpu)
{
    if (!vcpu)
        return;

    /* Stop if running */
    if (vcpu->state == VCPU_STATE_RUNNING)
        vcpu_stop(vcpu);

    hv_destroy_vcpu(vcpu->hv_vcpu);
    free(vcpu);

    log_info("vCPU %d destroyed", vcpu->index);
}

/*
 * Start a vCPU
 */
int vcpu_start(struct vcpu *vcpu)
{
    int ret;

    if (vcpu->state == VCPU_STATE_RUNNING)
        return 0;

    vcpu->should_stop = 0;

    ret = pthread_create(&vcpu->thread, NULL, vcpu_thread_func, vcpu);
    if (ret != 0) {
        log_error("Failed to create vCPU thread");
        return -1;
    }

    vcpu->state = VCPU_STATE_RUNNING;
    return 0;
}

/*
 * Stop a vCPU
 */
int vcpu_stop(struct vcpu *vcpu)
{
    if (vcpu->state != VCPU_STATE_RUNNING)
        return 0;

    vcpu->should_stop = 1;

    /* Cancel and join thread */
    pthread_cancel(vcpu->thread);
    pthread_join(vcpu->thread, NULL);

    vcpu->state = VCPU_STATE_STOPPED;
    return 0;
}

/*
 * Reset a vCPU
 */
int vcpu_reset(struct vcpu *vcpu)
{
    struct hv_regs regs;

    /* Reset registers to known state */
    memset(&regs, 0, sizeof(regs));

    regs.rip = 0x100000;  /* Default kernel entry point */
    regs.rflags = 0x2;    /* Reserved bit set */

    if (hv_set_regs(vcpu->hv_vcpu, &regs) < 0) {
        log_error("Failed to reset vCPU registers");
        return -1;
    }

    log_debug("vCPU %d reset", vcpu->index);
    return 0;
}

/*
 * Run the vCPU
 */
int vcpu_run(struct vcpu *vcpu)
{
    return hv_run(vcpu->hv_vcpu);
}

/*
 * Handle VM exit
 */
int vcpu_handle_exit(struct vcpu *vcpu, struct hv_exit *exit)
{
    int ret;

    /* Increment total exit counter */
    vcpu->exit_count++;

    switch (exit->reason) {
    case HV_EXIT_HLT:
        vcpu->halt_count++;
        ret = vcpu_handle_halt(vcpu);
        break;

    case HV_EXIT_IO:
        vcpu->io_count++;
        ret = vcpu_handle_io_exit(vcpu, &exit->u.io);
        break;

    case HV_EXIT_MMIO:
        vcpu->mmio_count++;
        ret = vcpu_handle_mmio_exit(vcpu, &exit->u.mmio);
        break;

    case HV_EXIT_EXTERNAL:
        /* External interrupt - just continue */
        ret = 0;
        break;

    case HV_EXIT_SHUTDOWN:
        log_info("vCPU %d: Shutdown (triple fault)", vcpu->index);
        vcpu->shutdown_count++;
        vcpu->should_stop = 1;
        ret = vcpu_handle_shutdown(vcpu);
        break;

    case HV_EXIT_FAIL_ENTRY:
        log_error("vCPU %d: Failed entry (error=%ld)",
                  vcpu->index, exit->u.error_code);
        ret = vcpu_handle_fail_entry(vcpu, exit->u.error_code);
        break;

    case HV_EXIT_INTERNAL_ERROR:
        log_error("vCPU %d: Internal error", vcpu->index);
        vcpu->should_stop = 1;
        ret = -1;
        break;

    case HV_EXIT_EXCEPTION:
        log_warn("vCPU %d: Exception", vcpu->index);
        vcpu->exception_count++;
        ret = -1;
        break;

    /* ARM64-specific exit reasons (Apple Silicon) */
    case HV_EXIT_CANCELED:
        log_info("vCPU %d: Exit canceled (async request)", vcpu->index);
        vcpu->canceled_count++;
        vcpu->should_stop = 1;
        ret = 0;
        break;

    case HV_EXIT_VTIMER:
        log_debug("vCPU %d: Virtual timer activated", vcpu->index);
        vcpu->vtimer_count++;
        /* VTimer activated - inject timer interrupt into guest */
        /* For now, just continue - proper implementation would inject IRQ */
        ret = 0;
        break;

    default:
        log_warn("vCPU %d: Unknown exit reason %d",
                 vcpu->index, exit->reason);
        vcpu->unknown_count++;
        ret = -1;
        break;
    }

    return ret;
}

/*
 * Handle I/O exit
 */
int vcpu_handle_io_exit(struct vcpu *vcpu, struct hv_io *io)
{
    struct vcpu *vcpu_local = vcpu;  /* For future use */
    uint8_t data[8];
    int i;

    /* Handle debug console on port 0x3f8 (COM1) */
    if (io->port == 0x3f8 || io->port == 0x3f9) {
        if (io->direction == HV_IO_OUT) {
            /* Write to console */
            for (i = 0; i < io->size; i++) {
                uint8_t byte = (io->data >> (i * 8)) & 0xff;
                putchar(byte);
            }
            fflush(stdout);
        } else {
            /* Read from console (not implemented) */
            io->data = 0;
        }
        (void)vcpu_local;
        return 0;
    }

    /* Port 0x3c0-0x3da - VGA */
    if (io->port >= 0x3c0 && io->port <= 0x3da) {
        /* Not implemented */
        if (io->direction == HV_IO_IN)
            io->data = 0;
        return 0;
    }

    log_warn("Unhandled I/O: port=0x%x, size=%d, direction=%s",
             io->port, io->size,
             io->direction == HV_IO_IN ? "IN" : "OUT");

    return 0;
}

/*
 * Handle MMIO exit
 */
int vcpu_handle_mmio_exit(struct vcpu *vcpu, struct hv_mmio *mmio)
{
    struct vm *vm = vcpu->vm;
    struct device *dev;
    uint64_t data = mmio->data;
    int ret;

    /* Find device at this GPA */
    dev = vm_find_device_at_gpa(vm, mmio->addr);
    if (!dev) {
        log_warn("MMIO to unmapped address: 0x%lx", mmio->addr);
        /* For reads, return zero */
        return 0;
    }

    /* Handle device access */
    if (mmio->is_write) {
        ret = dev->ops->write(dev, mmio->addr - dev->gpa_start,
                              &mmio->data, mmio->size);
    } else {
        ret = dev->ops->read(dev, mmio->addr - dev->gpa_start,
                             &data, mmio->size);
        /* For KVM, we need to write back the data */
        if (ret == 0) {
            /* Data will be written back to vCPU run structure */
        }
    }

    return ret;
}

/*
 * Handle HLT
 */
int vcpu_handle_halt(struct vcpu *vcpu)
{
    (void)vcpu;
    /* VCPU is halted, wait for interrupt */
    /* In a real implementation, we'd wait on an eventfd */
    return 0;
}

/*
 * Handle shutdown
 */
int vcpu_handle_shutdown(struct vcpu *vcpu)
{
    log_info("vCPU %d shutdown", vcpu->index);
    vcpu->should_stop = 1;
    return 0;
}

/*
 * Handle failed entry
 */
int vcpu_handle_fail_entry(struct vcpu *vcpu, uint64_t error)
{
    log_error("vCPU %d failed entry: %ld", vcpu->index, error);
    vcpu->should_stop = 1;
    return -1;
}

/*
 * Get registers
 */
int vcpu_get_regs(struct vcpu *vcpu, struct hv_regs *regs)
{
    return hv_get_regs(vcpu->hv_vcpu, regs);
}

/*
 * Set registers
 */
int vcpu_set_regs(struct vcpu *vcpu, const struct hv_regs *regs)
{
    return hv_set_regs(vcpu->hv_vcpu, regs);
}

/*
 * Get special registers
 */
int vcpu_get_sregs(struct vcpu *vcpu, struct hv_sregs *sregs)
{
    return hv_get_sregs(vcpu->hv_vcpu, sregs);
}

/*
 * Set special registers
 */
int vcpu_set_sregs(struct vcpu *vcpu, const struct hv_sregs *sregs)
{
    return hv_set_sregs(vcpu->hv_vcpu, sregs);
}

/*
 * Print vCPU statistics
 */
void vcpu_print_stats(struct vcpu *vcpu)
{
    if (!vcpu) {
        fprintf(stderr, "Invalid vCPU\n");
        return;
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║  vCPU %d Statistics                                                 ║\n", vcpu->index);
    fprintf(stderr, "╠══════════════════════════════════════════════════════════════════╣\n");
    fprintf(stderr, "║  Exit Statistics:                                                   ║\n");
    fprintf(stderr, "║    Total VM Exits:     %20llu                             ║\n", vcpu->exit_count);
    fprintf(stderr, "║    I/O Exits:          %20llu                             ║\n", vcpu->io_count);
    fprintf(stderr, "║    MMIO Exits:         %20llu                             ║\n", vcpu->mmio_count);
    fprintf(stderr, "║    HLT Exits:          %20llu                             ║\n", vcpu->halt_count);
    fprintf(stderr, "║    Shutdown Exits:     %20llu                             ║\n", vcpu->shutdown_count);
    fprintf(stderr, "║    Exception Exits:   %20llu                             ║\n", vcpu->exception_count);
    fprintf(stderr, "║    Canceled Exits:     %20llu (ARM64)                   ║\n", vcpu->canceled_count);
    fprintf(stderr, "║    VTimer Exits:       %20llu (ARM64)                   ║\n", vcpu->vtimer_count);
    fprintf(stderr, "║    Unknown Exits:      %20llu                             ║\n", vcpu->unknown_count);
    fprintf(stderr, "║                                                                     ║\n");
    fprintf(stderr, "║  Performance Statistics:                                           ║\n");
    fprintf(stderr, "║    Total Run Time:     %20llu microseconds            ║\n", vcpu->total_run_time_us);
    if (vcpu->total_run_time_us > 0) {
        uint64_t exits_per_sec = (vcpu->exit_count * 1000000ULL) / vcpu->total_run_time_us;
        fprintf(stderr, "║    Exits/Second:       %20llu                             ║\n", exits_per_sec);
    }
    fprintf(stderr, "║    Instructions:       %20llu (estimated)               ║\n", vcpu->instructions_executed);
    fprintf(stderr, "╚══════════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "\n");
}

/*
 * Reset vCPU statistics
 */
void vcpu_reset_stats(struct vcpu *vcpu)
{
    if (!vcpu) {
        return;
    }

    vcpu->exit_count = 0;
    vcpu->io_count = 0;
    vcpu->mmio_count = 0;
    vcpu->halt_count = 0;
    vcpu->shutdown_count = 0;
    vcpu->exception_count = 0;
    vcpu->canceled_count = 0;
    vcpu->vtimer_count = 0;
    vcpu->unknown_count = 0;
    vcpu->total_run_time_us = 0;
    vcpu->instructions_executed = 0;

    log_info("vcpu_reset_stats: vCPU %d statistics reset", vcpu->index);
}
