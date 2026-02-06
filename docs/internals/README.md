# Internals Documentation

Deep technical details about Vibe-VMM's internal implementation, platform-specific code, and low-level mechanisms.

## Platform-Specific Implementation

### [ARM64 Implementation](arm64.md)
- Virtualization Host Extensions (VHE)
- Exception levels (EL2/EL1/EL0)
- System registers
- VM exit handling
- MMIO instruction emulation

### [HVF Integration (macOS)](hvf.md)
- Hypervisor Framework API
- VM and vCPU creation
- Memory mapping
- Register access
- VM exit reasons

### [KVM Integration (Linux)](kvm.md)
- KVM ioctl interface
- Memory slot management
- vCPU run loop
- Signal handling
- ioctl reference

## Core Internals

### [VM Exit Handling](vm-exits.md)
- Exit types and reasons
- Exit info structures
- Dispatch logic
- Architecture-specific handlers

### [Memory Management](memory.md)
- GPA → HVA translation
- Memory slot allocation
- MMIO region tracking
- Page table handling

### [Device Framework](devices.md)
- Device registration
- MMIO routing
- Callback dispatch
- Virtio queue handling

## Data Structures

### VM Exit Information

#### ARM64 Exception Syndrome
```c
struct hvf_exit_info {
    uint64_t syndrome;      // ESR_EL1
    uint64_t vaddr;        // Virtual fault address
    uint64_t paddr;        // Physical fault address
};
```

#### KVM Exit Reason
```c
struct kvm_run {
    uint32_t exit_reason;
    // ... union of exit structures
    struct kvm_io {
        __u64 direction;
        __u64 size;
        __u64 port;
        __u64 count;
        __u64 data_offset;
    } io;
    // ... more exit types
};
```

### Register State

#### ARM64 Registers
```c
struct arm64_regs {
    uint64_t x[31];        // General-purpose registers
    uint64_t pc;           // Program counter
    uint64_t sp;           // Stack pointer
    uint64_t cpsr;         // Current program status
    // ... system registers
};
```

#### x86_64 Registers
```c
struct x86_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8, r15;
    uint64_t rip;          // Instruction pointer
    uint64_t rflags;       // Flags register
    // ... segment registers
};
```

## Key Algorithms

### VM Exit Dispatch
```c
int vcpu_run(struct vcpu *vcpu) {
    while (1) {
        // Run until exit
        ret = hv_vcpu_run(vcpu);

        // Get exit info
        ret = vcpu_get_exit_info(vcpu, &info);

        // Dispatch
        switch (info.reason) {
            case EXIT_MMIO:
                vcpu_handle_mmio_exit(vcpu, &info.mmio);
                break;
            case EXIT_IO:
                vcpu_handle_io_exit(vcpu, &info.io);
                break;
            case EXIT_EXCEPTION:
                vcpu_handle_exception(vcpu, &info.exception);
                break;
            // ... more cases
        }

        // Advance PC
        vcpu_advance_pc(vcpu);
    }
}
```

### MMIO Device Lookup
```c
struct device* find_device(struct vm *vm, uint64_t addr) {
    for (int i = 0; i < vm->num_devices; i++) {
        struct device *dev = vm->devices[i];
        if (addr >= dev->base && addr < dev->base + dev->size) {
            return dev;
        }
    }
    return NULL;
}
```

### GPA → HVA Translation
```c
void* gpa_to_hva(struct mem_map *map, uint64_t gpa) {
    for (int i = 0; i < map->num_regions; i++) {
        struct mem_region *region = &map->regions[i];
        if (gpa >= region->gpa &&
            gpa < region->gpa + region->size) {
            uint64_t offset = gpa - region->gpa;
            return region->hva + offset;
        }
    }
    return NULL;
}
```

## Performance Considerations

### VM Exit Cost
| Exit Type | Cost (cycles) | Frequency |
|-----------|---------------|-----------|
| MMIO access | 2000-5000 | High |
| Timer interrupt | 1000-2000 | Medium |
| Page fault | 2000-4000 | Low |
| CPUID | 500-1000 | Very low |

### Optimization Strategies

1. **Batch MMIO operations** - Reduce exit frequency
2. **Use shared memory** - Virtio queues in guest RAM
3. **Paravirtualized devices** - Reduce emulation overhead
4. **Huge pages** - Reduce TLB misses
5. **Pre-fault memory** - Avoid page faults during execution

## Platform Differences

### KVM vs HVF

| Feature | KVM (Linux) | HVF (macOS) |
|---------|-------------|-------------|
| VM Creation | ioctl | hv_vm_create() |
| Memory Mapping | ioctl() | hv_vm_map() |
| vCPU Run | ioctl(KVM_RUN) | hv_vcpu_run() |
| Register Access | ioctl structs | hv_vcpu_get/set_reg() |
| Exit Info | struct kvm_run | hv_exit_info_t |
| Interrupts | ioctl(KVM_INTERRUPT) | hv_vcpu_set_vtimer() |

### x86_64 vs ARM64

| Feature | x86_64 | ARM64 |
|---------|--------|-------|
| VM Entry | VMRESUME/VMLAUNCH | ERET to EL1 |
| VM Exit | VM exit | Exception to EL2 |
| Registers | RAX-R15, RFLAGS | X0-X30, CPSR |
| System Regs | CR0, CR4, EFER | SCTLR, TCR, TTBR |
| Page Tables | EPT/NPT | Stage-2 page tables |
| Interrupts | APIC/MSIs | GIC/IRQs/FIQs |

## Debugging Internals

### Enabling Debug Logs
```c
// In source files
#define DEBUG 1
log_debug("Detailed debug info: %x", value);
```

### Tracing VM Exits
```bash
# Run with debug logging
./vibevmm --log 4 --binary kernel.raw --entry 0x10000 --mem 128M 2>&1 | tee exits.log

# Analyze exits
grep "VM exit" exits.log | wc -l  # Count exits
grep "MMIO" exits.log             # MMIO exits only
```

### GDB Debugging
```bash
# Run VMM under GDB
gdb --args ./bin/vibevmm --binary kernel.raw --entry 0x10000 --mem 128M

# Set breakpoints
(gdb) break vcpu_run
(gdb) break vcpu_handle_mmio_exit
(gdb) run
```

## Future Enhancements

### ARM64
- [ ] GIC (Generic Interrupt Controller) support
- [ ] IRQ/FIQ injection
- [ ] Complete system register handling
- [ ] TLB invalidation emulation

### General
- [ ] Live migration
- [ ] Snapshot/restore
- [ ] Memory ballooning
- [ ] Enhanced virtio devices

## References

- [ARM ARM](https://developer.arm.com/documentation/ddi0487/latest) - ARM Architecture Reference Manual
- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - Intel Software Developer's Manual
- [KVM API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html) - Linux KVM API documentation
- [HVF Framework](https://developer.apple.com/documentation/hypervisor) - Apple Hypervisor Framework

## Contributing

When adding new features:
1. Update architecture documentation
2. Add inline comments
3. Document data structures
4. Include usage examples
5. Add error handling

## See Also

- [Architecture Overview](../architecture/overview.md) - High-level architecture
- [API Reference](../api/) - Public interfaces
- [User Guides](../guides/) - Usage documentation
