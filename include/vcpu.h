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

    /* Exit statistics */
    uint64_t exit_count;
    uint64_t io_count;
    uint64_t mmio_count;
    uint64_t halt_count;
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

#endif /* VIBE_VMM_VCPU_H */
