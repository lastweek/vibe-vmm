#ifndef VIBE_VMM_VCPU_H
#define VIBE_VMM_VCPU_H

#include "hypervisor.h"
#include "vm.h"
#include <stdint.h>
#include <pthread.h>

/* vCPU state */
enum vcpu_state {
    VCPU_STATE_STOPPED,
    VCPU_STATE_RUNNING,
    VCPU_STATE_WAITING,
    VCPU_STATE_ERROR,
};

/* vCPU structure */
struct vcpu {
    struct hv_vcpu *hv_vcpu;    /* Hypervisor vCPU */
    struct vm *vm;              /* Parent VM */
    int index;                  /* vCPU index */

    /* State */
    enum vcpu_state state;

    /* Thread */
    pthread_t thread;
    int should_stop;

    /* Initial register state (for ARM64 where vCPU is created in thread) */
    uint64_t initial_rip;       /* Initial program counter */
    int has_initial_state;      /* Flag indicating if initial state is set */

    /* Exit statistics */
    uint64_t exit_count;        /* Total VM exits */
    uint64_t io_count;          /* I/O exits */
    uint64_t mmio_count;        /* MMIO exits */
    uint64_t halt_count;        /* HLT exits */
    uint64_t shutdown_count;    /* Shutdown exits */
    uint64_t exception_count;   /* Exception exits */
    uint64_t canceled_count;    /* Canceled exits (ARM64) */
    uint64_t vtimer_count;      /* VTimer exits (ARM64) */
    uint64_t unknown_count;     /* Unknown exits */

    /* Timing statistics */
    uint64_t total_run_time_us; /* Total run time in microseconds */

    /* Instruction execution stats (estimated) */
    uint64_t instructions_executed;
};

/* Create/destroy vCPU */
struct vcpu* vcpu_create(struct vm *vm, int index);
void vcpu_destroy(struct vcpu *vcpu);

/* vCPU control */
int vcpu_start(struct vcpu *vcpu);
int vcpu_stop(struct vcpu *vcpu);
int vcpu_reset(struct vcpu *vcpu);

/* vCPU running (main loop) */
int vcpu_run(struct vcpu *vcpu);

/* VM exit handling */
typedef int (*vcpu_exit_handler)(struct vcpu *vcpu, struct hv_exit *exit);

int vcpu_handle_exit(struct vcpu *vcpu, struct hv_exit *exit);
int vcpu_handle_io_exit(struct vcpu *vcpu, struct hv_io *io);
int vcpu_handle_mmio_exit(struct vcpu *vcpu, struct hv_mmio *mmio);
int vcpu_handle_halt(struct vcpu *vcpu);
int vcpu_handle_shutdown(struct vcpu *vcpu);
int vcpu_handle_fail_entry(struct vcpu *vcpu, uint64_t error);

/* Register access (wrappers around hypervisor) */
int vcpu_get_regs(struct vcpu *vcpu, struct hv_regs *regs);
int vcpu_set_regs(struct vcpu *vcpu, const struct hv_regs *regs);

int vcpu_get_sregs(struct vcpu *vcpu, struct hv_sregs *sregs);
int vcpu_set_sregs(struct vcpu *vcpu, const struct hv_sregs *sregs);

/* Statistics */
void vcpu_print_stats(struct vcpu *vcpu);
void vcpu_reset_stats(struct vcpu *vcpu);

#endif /* VIBE_VMM_VCPU_H */
