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

    /* For Apple HVF (ARM64), the vCPU must be created in the same thread that runs it.
     * This is because hv_vcpu_create() associates the vCPU with the current thread. */
#if defined(__aarch64__)
    if (!vcpu->hv_vcpu) {
        log_debug("Creating HVF vCPU %d in thread", vcpu->index);
        vcpu->hv_vcpu = hv_create_vcpu(vcpu->vm->hv_vm, vcpu->index);
        if (!vcpu->hv_vcpu) {
            log_error("Failed to create hypervisor vCPU in thread");
            return NULL;
        }

        /* Apply initial register state if it was stored earlier */
        if (vcpu->has_initial_state) {
            struct hv_regs regs;
            memset(&regs, 0, sizeof(regs));
            regs.rip = vcpu->initial_rip;
            regs.rflags = 0x2;  /* Standard RFLAGS value */

            log_debug("Applying initial PC=0x%llx in thread", (unsigned long long)regs.rip);
            if (hv_set_regs(vcpu->hv_vcpu, &regs) < 0) {
                log_error("Failed to set initial registers in thread");
                return NULL;
            }
        }
    }
#endif

    while (!vcpu->should_stop) {
        log_debug("vCPU %d: About to run (iteration %ld)", vcpu->index, vcpu->exit_count);
        ret = vcpu_run(vcpu);
        log_debug("vCPU %d: Run returned, ret=%d, errno=%d", vcpu->index, ret, errno);

        if (ret < 0) {
            if (errno == EINTR) {
                log_debug("vCPU %d: Interrupted by signal", vcpu->index);
                continue;  /* Signal interrupted */
            }
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

        log_debug("vCPU %d: Got exit, reason=%d", vcpu->index, exit.reason);
        ret = vcpu_handle_exit(vcpu, &exit);
        if (ret < 0) {
            log_error("Failed to handle exit");
            break;
        }

        /* Safety: prevent infinite loop if we keep getting the same exit */
        vcpu->exit_count++;
        if (vcpu->exit_count > 1000) {
            log_error("vCPU %d: Too many exits (%ld), stopping", vcpu->index, vcpu->exit_count);
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

    /* For x86_64, create the vCPU now. For ARM64, the vCPU will be created
     * in the vCPU thread because Apple HVF requires creation and execution
     * to happen in the same thread. */
#if defined(__x86_64__)
    vcpu->hv_vcpu = hv_create_vcpu(vm->hv_vm, index);
    if (!vcpu->hv_vcpu) {
        log_error("Failed to create hypervisor vCPU");
        free(vcpu);
        return NULL;
    }
#else
    /* ARM64: vCPU will be created in the thread */
    vcpu->hv_vcpu = NULL;
#endif

    vcpu->vm = vm;
    vcpu->index = index;
    vcpu->state = VCPU_STATE_STOPPED;
    vcpu->should_stop = 0;
    vcpu->has_initial_state = 0;
    vcpu->initial_rip = 0;

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
    log_debug("vcpu_stop() called for vCPU %d, state=%d", vcpu->index, vcpu->state);

    if (vcpu->state != VCPU_STATE_RUNNING)
        return 0;

    vcpu->should_stop = 1;
    log_debug("Set should_stop=1 for vCPU %d", vcpu->index);

#if defined(__aarch64__)
    /* For Apple HVF on ARM64, we need to explicitly request vCPU exit
     * to make hv_vcpu_run() return. Otherwise it will block forever. */
    log_debug("ARM64: vcpu->hv_vcpu=%p for vCPU %d", (void*)vcpu->hv_vcpu, vcpu->index);
    if (vcpu->hv_vcpu) {
        log_debug("Calling hv_vcpu_exit() for vCPU %d", vcpu->index);
        hv_vcpu_exit(vcpu->hv_vcpu);
        log_debug("hv_vcpu_exit() returned for vCPU %d", vcpu->index);
    } else {
        log_warn("vCPU %d: hv_vcpu is NULL, can't request exit", vcpu->index);
    }
#else
    log_debug("Not ARM64, skipping hv_vcpu_exit()");
#endif

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
 *
 * This function handles all possible VM exit reasons from KVM and HVF:
 *
 * === Common Exit Reasons (KVM x86_64, KVM ARM64, HVF) ===
 * - HLT: Guest executed HLT instruction (halt)
 * - IO: I/O port access (IN/OUT instructions on x86)
 * - MMIO: Memory-mapped I/O access
 * - EXTERNAL: External interrupt (NMI/IRQ)
 * - FAIL_ENTRY: Failed to enter guest mode
 * - SHUTDOWN: Guest shutdown (triple fault on x86)
 * - INTERNAL_ERROR: Hypervisor internal error
 * - EXCEPTION: Guest exception (fault, trap, etc)
 *
 * === KVM x86_64 Specific Exit Reasons ===
 * - IRQ_WINDOW_OPEN: Interrupt window opened
 * - SET_TPR: TPR (Task Priority Register) access
 * - TPR_ACCESS: TPR read/write below window
 * - NMI: NMI window opened
 * - SYSTEM_EVENT: System event (reset, shutdown)
 * - X86_RDMSR: Read model-specific register
 * - X86_WRMSR: Write model-specific register
 * - X86_HYPERCALL: x86 hypercall (VMMCALL)
 * - DIRTY_LOG_FULL: Dirty page log full
 * - X86_BUS_LOCK: Bus lock detected
 *
 * === KVM ARM64 Specific Exit Reasons ===
 * - ARM_EXCEPTION: Exception from lower EL
 * - ARM_TRAP: Trap to higher EL
 * - ARM_MMIO: MMIO fault
 * - ARM_IRQ: External IRQ
 *
 * === HVF ARM64 Specific (Apple Silicon) ===
 * - CANCELED: Async exit from hv_vcpus_exit()
 * - VTIMER: Virtual timer activated
 *
 * === KVM PowerPC/S390 Specific ===
 * (Handled generically - not fully implemented)
 * - DCR, OSI, PAPR_HCALL, S390_* exits
 *
 * For more details, see:
 * - Linux: Documentation/virtual/kvm/api.txt
 * - macOS: Hypervisor.framework headers
 */
int vcpu_handle_exit(struct vcpu *vcpu, struct hv_exit *exit)
{
    int ret;

    /* Increment total exit counter */
    vcpu->exit_count++;

    switch (exit->reason) {
    /* Common exit reasons */
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
        log_warn("vCPU %d: Exception - stopping", vcpu->index);
        vcpu->exception_count++;
        vcpu->should_stop = 1;
        ret = 0;  /* Return success to allow cleanup */
        break;

    /* KVM x86_64 specific exit reasons */
    case HV_EXIT_IRQ_WINDOW_OPEN:
        log_debug("vCPU %d: IRQ window open", vcpu->index);
        /* Interrupt window opened - can inject IRQ now */
        ret = 0;
        break;

    case HV_EXIT_SET_TPR:
        log_debug("vCPU %d: TPR access", vcpu->index);
        /* Task Priority Register access (x86 APIC) */
        ret = 0;
        break;

    case HV_EXIT_TPR_ACCESS:
        log_debug("vCPU %d: TPR access below window", vcpu->index);
        /* TPR read/write below window */
        ret = 0;
        break;

    case HV_EXIT_NMI:
        log_debug("vCPU %d: NMI window open", vcpu->index);
        /* NMI window opened - can inject NMI now */
        ret = 0;
        break;

    case HV_EXIT_SYSTEM_EVENT:
        log_info("vCPU %d: System event", vcpu->index);
        vcpu->shutdown_count++;
        vcpu->should_stop = 1;
        ret = 0;
        break;

    case HV_EXIT_X86_RDMSR:
        log_debug("vCPU %d: RDMSR instruction", vcpu->index);
        /* Read Model-Specific Register - intercept for debugging */
        ret = 0;
        break;

    case HV_EXIT_X86_WRMSR:
        log_debug("vCPU %d: WRMSR instruction", vcpu->index);
        /* Write Model-Specific Register - intercept for debugging */
        ret = 0;
        break;

    case HV_EXIT_X86_HYPERCALL:
        log_info("vCPU %d: x86 hypercall (VMMCALL)", vcpu->index);
        /* Guest executed VMMCALL hypercall */
        ret = 0;
        break;

    case HV_EXIT_DIRTY_LOG_FULL:
        log_warn("vCPU %d: Dirty log full", vcpu->index);
        /* Dirty page logging buffer full - need to reset */
        ret = 0;
        break;

    case HV_EXIT_X86_BUS_LOCK:
        log_warn("vCPU %d: Bus lock detected", vcpu->index);
        /* Bus lock detected (for split-lock detection) */
        ret = 0;
        break;

    /* KVM ARM64 specific exit reasons */
    case HV_EXIT_ARM_EXCEPTION:
        log_warn("vCPU %d: ARM64 exception from lower EL", vcpu->index);
        vcpu->exception_count++;
        /* Exception from lower exception level */
        ret = -1;
        break;

    case HV_EXIT_ARM_TRAP:
        log_debug("vCPU %d: ARM64 trap to higher EL", vcpu->index);
        /* Trap to higher exception level (e.g., WFI, MRS, system regs) */
        ret = 0;
        break;

    case HV_EXIT_ARM_MMIO:
        log_debug("vCPU %d: ARM64 MMIO fault", vcpu->index);
        vcpu->mmio_count++;
        /* ARM64 MMIO fault - similar to HV_EXIT_MMIO */
        ret = 0;
        break;

    case HV_EXIT_ARM_IRQ:
        log_debug("vCPU %d: ARM64 external IRQ", vcpu->index);
        /* External IRQ on ARM64 */
        ret = 0;
        break;

    /* HVF ARM64 specific exit reasons (Apple Silicon) */
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

    /* KVM PowerPC/S390 specific (not fully implemented) */
    case HV_EXIT_DCR:
    case HV_EXIT_OSI:
    case HV_EXIT_PAPR_HCALL:
    case HV_EXIT_S390_SIEIC:
    case HV_EXIT_S390_RESET:
    case HV_EXIT_S390_UCONTROL:
    case HV_EXIT_WATCHDOG:
    case HV_EXIT_S390_TSCH:
    case HV_EXIT_EPR:
    case HV_EXIT_S390_STSI:
    case HV_EXIT_IOAPIC_EOI:
    case HV_EXIT_HYPERV:
    case HV_EXIT_ARM_NISV:
        log_warn("vCPU %d: Unsupported exit reason %d (architecture-specific)",
                 vcpu->index, exit->reason);
        vcpu->unknown_count++;
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

    log_info("vcpu_handle_mmio_exit: GPA=0x%lx, size=%zu, is_write=%d, data=0x%lx",
             mmio->addr, mmio->size, mmio->is_write, mmio->data);

    /* Find device at this GPA */
    dev = vm_find_device_at_gpa(vm, mmio->addr);
    if (!dev) {
        log_warn("MMIO to unmapped address: 0x%lx", mmio->addr);
        log_warn("");
        log_warn("The guest kernel is trying to access a device at GPA 0x%lx", mmio->addr);
        log_warn("but no device is registered at that address.");
        log_warn("");
        log_warn("Common causes:");
        log_warn("  1. MMIO console device not enabled (add --console flag if needed)");
        log_warn("  2. Kernel built for different MMIO base address");
        log_warn("  3. Trying to access virtio device before initialization");
        log_warn("");
        log_warn("Expected device addresses:");
        log_warn("  • MMIO console:    0x90000000 (UART)");
        log_warn("  • Virtio console:  0x00a00000");
        log_warn("  • RAM:              0x00000000 - 0x07FFFFFF (128MB)");
        log_warn("");
        /* For reads, return zero */
        return 0;
    }

    log_info("Found device '%s' at GPA 0x%lx (offset=%lu)",
             dev->ops->name, dev->gpa_start, mmio->addr - dev->gpa_start);

    /* Handle device access */
    if (mmio->is_write) {
        log_info("Calling device write handler");
        ret = dev->ops->write(dev, mmio->addr - dev->gpa_start,
                              &mmio->data, mmio->size);
        log_info("Device write handler returned: %d", ret);
    } else {
        log_info("Calling device read handler");
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
