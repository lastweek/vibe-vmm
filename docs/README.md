# Vibe-VMM Documentation

Welcome to Vibe-VMM - A minimal Virtual Machine Monitor for understanding virtualization fundamentals.

## 📚 Documentation

### Core Concepts

- **[The Essence of Virtualization](#essence-of-virtualization)** - Start here! The fundamental principles
- **[System Architecture](architecture-overview.md)** - Complete architecture and design
- **[SR-IOV Device Passthrough](architecture-sriov.md)** - High-performance device assignment

### Quick Links

- [Getting Started](#getting-started) - Installation and first VM
- [API Reference](#api-reference) - Programming interfaces
- [Platform Guides](#platform-guides) - Platform-specific setup
- [Troubleshooting Guide](troubleshooting.md) - Common issues and solutions

---

## Where to Start

If you are new to virtualization:
- Read [The Essence of Virtualization](#essence-of-virtualization)
- Skim [System Architecture](architecture-overview.md)
- Run a test kernel from [Getting Started](#getting-started)

If you want to contribute or extend features:
- Read [System Architecture](architecture-overview.md) end-to-end
- Review [SR-IOV Device Passthrough](architecture-sriov.md) if you need VFIO
- Keep [Troubleshooting Guide](troubleshooting.md) handy while iterating

## The Essence of Virtualization

At its core, a Virtual Machine Monitor (VMM) is about **creating a controlled illusion** - making a guest operating system believe it has exclusive access to physical hardware, when in reality it's sharing everything with other guests and the host.

### The Three Fundamental Mechanisms

#### 1. Physical Address Interception (The Core Mechanism)

**This is the essence:** The VMM virtualizes physical addresses so that every memory access can be intercepted.

```
Guest writes to MMIO address 0x90000000
    ↓
Guest's "Physical Address" is actually a Virtualized Physical Address
    ↓
MMU/IOPT (Memory Management Unit / I/O Page Table) checks if address is mapped
    ↓
If mapped to RAM → Direct access (fast path)
If unmapped (MMIO) → VM EXIT to VMM (intercept)
    ↓
VMM emulates the device behavior
```

**Why this matters:**
- **I/O Devices**: When the guest accesses device memory (MMIO), the VMM intercepts and emulates the device
- **Privileged Operations**: When the guest tries to configure page tables or control registers, the VMM validates and virtualizes
- **Security**: The guest can never access physical memory it hasn't been assigned

**Key insight:** By controlling physical address translation, the VMM controls **everything** - I/O, memory configuration, and privileged state.

#### 2. Privileged Instruction Trapping

The guest OS believes it's running directly on hardware, but privileged instructions are intercepted:

```c
Guest executes: WRMSR (Write Model-Specific Register)
    ↓
VM Exit (privileged operation)
    ↓
VMM: "Let me check if this is allowed..."
    ↓
VMM updates virtual MSR or emulates the effect
    ↓
Guest resumes (none the wiser)
```

**What gets trapped:**
- **Control register access** (CR0, CR4 on x86; SCTLR on ARM)
- **System register access** (MSRs on x86; System regs on ARM)
- **Page table manipulation** (writing to CR3/TTBR0)
- **I/O port access** (IN/OUT instructions on x86)
- **Model-specific registers** (interrupt controls, timers)

**The beauty:** The guest doesn't know it's being trapped. It just looks like the instruction worked.

#### 3. Execution Context Switching

The VMM switches between two execution modes:

```
┌─────────────────────────────────────────────────────────────┐
│  Host Mode (VMX Root / EL2)                                 │
│  - VMM runs here                                            │
│  - Has full hardware privileges                             │
│  - Controls all resources                                   │
└─────────────────────────────────────────────────────────────┘
              ↑ VM Entry / VM Exit ↓
┌─────────────────────────────────────────────────────────────┐
│  Guest Mode (VMX Non-Root / EL1)                            │
│  - Guest OS runs here                                       │
│  - Believes it has full privileges (but doesn't)            │
│  - All privileged operations trap to host                   │
└─────────────────────────────────────────────────────────────┘
```

**VM Entry (Host → Guest):**
- Load guest register state (PC, SP, general-purpose regs)
- Load guest memory map (EPT/NPT/Stage-2 page tables)
- Jump to guest entry point
- Guest executes until VM exit

**VM Exit (Guest → Host):**
- Save guest register state
- Report exit reason (why did we exit?)
- VMM handles the exit
- VMM resumes guest when ready

### The VM Exit: Heartbeat of Virtualization

Every VM exit tells a story:

```
Exit Type                │ Meaning                          │ VMM Response
─────────────────────────┼──────────────────────────────────┼────────────────────────
MMIO Read/Write         │ Guest accessing device memory     │ Emulate device
Exception               │ Guest triggered exception         │ Inject to guest or emulate
HLT                     │ Guest is idle                     │ Schedule other vCPU
Interrupt Window        │ Guest ready for interrupts        │ Inject pending IRQ
External Interrupt      │ Host needs attention              │ Handle and possibly inject
EPT Violation           │ Page fault or MMIO access         │ Setup MMIO or page in
```

**The cost:** Every VM exit costs ~2000-5000 CPU cycles. This is why:
- **Paravirtualization** exists (guest knows it's virtualized, cooperates)
- **Direct assignment** exists (VFIO passthrough - no emulation)
- **Batch operations** matter (reduce exit frequency)

### Resource Multiplexing

The VMM presents virtual copies of physical resources:

```
Physical NIC ──────┬──→ VM 1 sees: Exclusive NIC
(SR-IOV)           ├──→ VM 2 sees: Exclusive NIC
                   ├──→ VM 3 sees: Exclusive NIC
                   └──→ VM 4 sees: Exclusive NIC

Physical RAM ──────┬──→ VM 1 sees: 0x00000000-0x7FFFFFFF (2GB)
                   ├──→ VM 2 sees: 0x00000000-0x7FFFFFFF (2GB)
                   └──→ VM 3 sees: 0x00000000-0x7FFFFFFF (2GB)

Physical CPU ──────┬──→ vCPU 0 on VM 1
(4 cores)          ├──→ vCPU 0-1 on VM 2
                   └──→ vCPU 0-1 on VM 3
```

**The illusion:** Each VM believes it has exclusive access to its assigned resources.

### Two Approaches to Device Emulation

#### Full Emulation (Slowest, Most Compatible)

```
Guest → MMIO write → VM Exit → VMM emulates entire device → Return
```

- Every device access traps to VMM
- VMM software emulates all device behavior
- ~1000-5000 CPU cycles per access

#### Paravirtualization (Faster, Requires Guest Changes)

```
Guest → virtqueue (shared memory) → notification → VMM processes → response
```

- Guest knows it's virtualized
- Uses virtio standard for communication
- Most data transfer via shared memory (no exits)
- ~100-500 cycles per operation

#### Direct Assignment (Fastest, No Emulation)

```
Guest → Direct VF access → IOMMU → Physical Device
```

- VFIO passthrough
- Guest gets direct hardware access
- IOMMU provides isolation (DMA protection)
- ~0 cycles overhead (native performance)

### The Essence: Trust and Isolation

**Trust:**
- Guest trusts VMM to present consistent hardware behavior
- VMM must faithfully emulate devices or pass through correctly
- Bugs break the illusion (guest crashes or detects virtualization)

**Isolation:**
- VM must not escape its assigned resources
- IOMMU prevents DMA attacks
- VMM validates all privileged operations
- Malicious guest must not harm host or other guests

**Shared Resources:**
- Everyone gets a slice
- No one knows they're sharing
- VMM is the impartial referee

### The VMM's Job

```c
while (vm_running) {
    // 1. Run guest (until VM exit)
    vcpu_run(vcpu);

    // 2. Why did we exit?
    exit_info = get_exit_info(vcpu);

    // 3. Handle the exit
    switch (exit_info->reason) {
        case MMIO_ACCESS:
            // Guest is accessing a device
            device = find_device(exit_info->addr);
            device->emulate_access(exit_info);
            break;

        case EXCEPTION:
            // Guest triggered exception
            if (is_guest_fault(exit_info)) {
                inject_exception_to_guest(exit_info);
            } else {
                handle_in_vmm(exit_info);
            }
            break;

        case HALT:
            // Guest is idle
            schedule_other_vcpus();
            break;
    }

    // 4. Resume guest
    advance_pc_past_instruction(vcpu);
}
```

**That's the essence.** Everything else is details.

---

## Getting Started

### Prerequisites

- **Linux x86_64**: KVM enabled, kernel 4.14+
- **macOS Intel**: Hypervisor.framework (OS X 10.10+)
- **macOS ARM64**: Hypervisor.framework (macOS 11+)

### Quick Start

```bash
# Clone and build
git clone https://github.com/lastweek/vibe-vmm.git
cd vibe-vmm
make

# Run test kernel (ARM64)
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M

# Run a kernel (x86_64, provide your own kernel/initrd)
./bin/vibevmm --kernel /path/to/bzImage --initrd /path/to/initrd --cmdline "console=ttyS0" --mem 512M

# Quick test (Apple Silicon only)
./quicktest.sh
```

### Building Test Kernels

```bash
cd tests/kernels
./build.sh    # ARM64 (Apple Silicon) only
```

---

## API Reference

### Core Structures

```c
struct vm {
    void *vm_handle;           // Hypervisor-specific VM handle
    struct vcpu **vcpus;       // Array of vCPUs
    struct mem_map *mem_map;   // GPA → HVA mapping
    struct device **devices;   // Registered devices
};

struct vcpu {
    struct vm *vm;             // Parent VM
    void *vcpu_handle;         // Hypervisor handle
    int vcpu_id;               // vCPU index
};

struct device {
    char *name;                // Device name
    uint64_t base;             // Base GPA address
    int (*read)(...);          // Read callback
    int (*write)(...);         // Write callback
};
```

### Key Functions

```c
// VM lifecycle
struct vm* vm_create(void);
int vm_add_memory(struct vm *vm, uint64_t gpa, void *hva, uint64_t size);
int vm_register_device(struct vm *vm, struct device *dev);
int vm_start(struct vm *vm);
void vm_destroy(struct vm *vm);

// vCPU execution
int vcpu_run(struct vcpu *vcpu);
int vcpu_get_regs(struct vcpu *vcpu, struct regs *regs);
int vcpu_set_regs(struct vcpu *vcpu, struct regs *regs);
```

For detailed API docs, see [architecture-overview.md](architecture-overview.md).

---

## Platform Guides

### Linux (x86_64) - KVM

```bash
# Check KVM support
ls /dev/kvm
lscpu | grep Virtualization

# Build
make clean && make

# Run with VFIO device passthrough
sudo ./bin/vibevmm \
  --kernel vmlinuz \
  --mem 2G \
  --vfio 0000:01:00.1
```

**Features:** Full feature support, VFIO passthrough, IOMMU

### macOS Intel - HVF

```bash
# Build
make clean && make

# Run (codesign via run.sh, provide your own x86_64 kernel)
./run.sh --kernel /path/to/bzImage --initrd /path/to/initrd --cmdline "console=ttyS0" --mem 512M
```

**Features:** Full feature support, no VFIO

### macOS ARM64 (Apple Silicon) - HVF

```bash
# Build
make clean && make

# Run test kernel
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M
```

**Features:**
- ✅ HVF initialization and VM creation
- ✅ Memory mapping (GPA → HVA)
- ✅ vCPU creation and execution
- ✅ ARM64 code execution
- ✅ VM exit handling (MMIO, exceptions)
- ✅ Position-independent code support

**Limitations:**
- ⚠️ ARM64 register read/write partially stubbed
- ⚠️ System register handling incomplete
- ❌ No interrupt injection (TODO)
- ❌ No VFIO (macOS doesn't have VFIO)
 
**Note**: `./quicktest.sh` and `tests/kernels/build.sh` are Apple Silicon only.

---

## Troubleshooting

For a full list of issues and fixes, see [Troubleshooting Guide](troubleshooting.md).

### Common Issues

#### "Instruction Abort at NULL" (ARM64)

**Cause:** Position-dependent code with literal pools

```
Example: ldr x0, =message  // Literal pool contains absolute address
```

**Solution:** Use position-independent code
```assembly
// Instead of literal pools:
movz x0, #0x0048
movk x0, #0x0000, lsl #16  // x0 = 0x00000048 ('H')
```

See [arm64_hello.S](../tests/kernels/arm64_hello.S) for example.

#### "MMIO to unmapped address"

**Cause:** Device not registered or wrong GPA

**Solution:**
- MMIO console is now auto-enabled (no --console flag needed)
- Verify device base address matches kernel
- Check device registration in vm_create()

#### Guest hangs on boot

**Cause:** Wrong entry point or kernel format mismatch

**Solution:**
- Verify entry point matches load address
- Check kernel is raw binary, not ELF (unless using ELF loader)
- Enable debug logging: `--log 4`

#### quicktest.sh fails

**Cause:** Old build artifacts

**Solution:**
```bash
make clean
make
./quicktest.sh
```

### Debug Logging

```bash
# Enable debug output
./run.sh --binary kernel.raw --entry 0x10000 --mem 128M --log 4

# Log levels:
# 0 = none
# 1 = error
# 2 = warn
# 3 = info
# 4 = debug
```

### Checking Logs

```bash
# VMM console log
cat vmm_console.log

# Build log
cat /tmp/vibe_build.log

# Test log
cat /tmp/vibevmm_test.log
```

---

## Architecture Deep Dive

For comprehensive architecture documentation, see:
- **[System Architecture](architecture-overview.md)** - Complete architecture with diagrams
- **[SR-IOV Passthrough](architecture-sriov.md)** - High-performance device assignment

### Key Architecture Points

**Hypervisor Abstraction:**
```c
// One VMM codebase, multiple hypervisors
#ifdef __APPLE__
    #if defined(__aarch64__)
        return &hvf_arm64_ops;  // macOS Apple Silicon
    #elif defined(__x86_64__)
        return &hvf_x86_ops;    // macOS Intel
    #endif
#elif defined(__linux__)
    #if defined(__x86_64__)
        return &kvm_ops;        // Linux x86_64
    #endif
#endif
```

**Memory Layout:**
```
Guest Physical Address Space:

0x00000000 ───────── Low RAM (up to 256MB)
0x90000000 ───────── MMIO Console (4KB)
0x00A00000 ───────── Virtio Console (4KB)
0x00A01000 ───────── Virtio Block (4KB)
0x00A02000 ───────── Virtio Network (4KB)
0x100000000 ──────── High RAM (>4GB)
```

**VM Exit Flow:**
```
VM Exit → Get Info → Dispatch → Emulate → Advance PC → Resume
```

---

## Contributing

We welcome contributions! Areas needing help:
- ARM64 GIC implementation
- Interrupt injection
- Complete system register handling
- Enhanced virtio devices
- Additional hypervisor backends
- Documentation improvements

See [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

---

## Resources

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [Intel VT-x Specifications](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [Linux KVM API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Apple Hypervisor Framework](https://developer.apple.com/documentation/hypervisor)
- [Virtio Specification](https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.html)

---

## License

MIT License - See [LICENSE](../LICENSE) for details.

---

**Last Updated:** 2026-02-07
**Project:** [Vibe-VMM](https://github.com/lastweek/vibe-vmm)
