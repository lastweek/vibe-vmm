# Vibe-VMM Architecture Documentation

This document provides a comprehensive overview of the Vibe-VMM architecture, component design, and execution workflow.

## Table of Contents

1. [System Overview](#system-overview)
2. [Architecture Layers](#architecture-layers)
3. [Core Components](#core-components)
4. [Execution Workflow](#execution-workflow)
5. [Memory Management](#memory-management)
6. [Device Emulation](#device-emulation)
7. [Hypervisor Abstraction](#hypervisor-abstraction)
8. [ARM64-Specific Implementation](#arm64-specific-implementation)

---

## System Overview

Vibe-VMM is a minimal Virtual Machine Monitor designed for educational purposes and research. It provides a clean abstraction layer over hardware virtualization technologies (KVM on Linux, HVF on macOS) while supporting multiple architectures (x86_64 and ARM64).

### Design Philosophy

- **Minimalism**: Essential functionality only, no feature bloat
- **Portability**: Multi-platform and multi-architecture support
- **Modularity**: Clean separation of concerns with well-defined interfaces
- **Educational**: Simple codebase suitable for learning virtualization internals

### Key Features

- Multi-architecture support (x86_64, ARM64)
- Multi-hypervisor backends (KVM, HVF)
- Pluggable device model
- Raw binary and kernel image loading
- Memory-mapped I/O (MMIO) device emulation
- Virtio device framework (console, block, network)

---

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                        │
│                     (CLI & Configuration)                       │
│                                                                   │
│  • main.c - Command-line parsing                                │
│  • Boot configuration (kernel, initrd, cmdline)                 │
│  • Device configuration (--disk, --net, --console)              │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                          VMM Core Layer                          │
│                                                                   │
│  ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌──────────────┐    │
│  │    VM    │  │   vCPU   │  │    MM   │  │   Devices    │    │
│  │ Management│ │Execution │  │Management│  │  Emulation   │    │
│  └──────────┘  └──────────┘  └─────────┘  └──────────────┘    │
│                                                                   │
│  • vm.c - VM lifecycle, device registration                      │
│  • vcpu.c - vCPU execution, VM exit handling                     │
│  • mm.c - GPA→HVA mapping, memory slots                          │
│  • devices.c - Device framework                                  │
│  • boot.c - Kernel loading, boot setup                           │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                    Hypervisor Abstraction Layer                  │
│                                                                   │
│  • hypervisor.h - Abstract interface                             │
│  • hvf.c - macOS Hypervisor Framework (x86_64)                   │
│  • hvf_arm64.c - macOS Hypervisor Framework (ARM64)              │
│  • kvm.c - Linux Kernel-based Virtual Machine (x86_64)           │
│  • hvf_stub.c, kvm_stub.c - Platform stubs                       │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│                      Hardware/OS Layer                           │
│                                                                   │
│  • Linux KVM (kernel module)                                     │
│  • macOS HVF (Hypervisor.framework)                              │
│  • Hardware virtualization extensions (Intel VT-x, ARM VHE)      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Components

### 1. VM Management (`vm.c`, `include/vm.h`)

**Responsibilities:**
- VM creation and destruction
- vCPU lifecycle management
- Memory region registration
- Device registration and MMIO routing
- VM execution control

**Key Data Structures:**
```c
struct vm {
    void *vm_handle;           // Hypervisor-specific VM handle
    struct vcpu **vcpus;       // Array of vCPUs
    int num_vcpus;             // Number of vCPUs
    struct mem_map *mem_map;   // GPA → HVA memory mapping
    struct device **devices;   // Registered devices
    int num_devices;           // Number of devices
    void *private;             // Hypervisor-specific private data
};
```

**Key Operations:**
- `vm_create()` - Initialize VM and hypervisor resources
- `vm_add_memory()` - Register guest physical memory region
- `vm_register_device()` - Add MMIO device
- `vm_create_vcpu()` - Create and initialize vCPU
- `vm_start()` - Begin VM execution
- `vm_stop()` - Halt all vCPUs
- `vm_destroy()` - Clean up resources

### 2. vCPU Execution (`vcpu.c`, `include/vcpu.h`)

**Responsibilities:**
- vCPU creation and initialization
- Register state management
- VM exit handling and routing
- Instruction emulation
- Interrupt injection

**Key Data Structures:**
```c
struct vcpu {
    struct vm *vm;             // Parent VM
    void *vcpu_handle;         // Hypervisor-specific vCPU handle
    int vcpu_id;               // vCPU index
    void *run;                 // Hypervisor run structure
    void *private;             // Hypervisor-specific data
    struct hvf_ops *ops;       // Hypervisor operations
};
```

**VM Exit Handling:**
VM exits are categorized and dispatched to appropriate handlers:
- **MMIO exits** → `vcpu_handle_mmio_exit()` → Device callbacks
- **IO exits** → `vcpu_handle_io_exit()` → Port-mapped devices
- **Exception exits** → Architecture-specific handlers
- **Interrupt exits** → Interrupt injection logic

### 3. Memory Management (`mm.c`, `include/mm.h`)

**Responsibilities:**
- Guest physical address (GPA) to host virtual address (HVA) mapping
- Memory slot management
- MMIO region tracking
- Address translation for device emulation

**Key Data Structures:**
```c
struct mem_region {
    uint64_t gpa;              // Guest physical address
    void *hva;                 // Host virtual address
    uint64_t size;             // Region size
    int flags;                 // Protection flags
};

struct mem_map {
    struct mem_region *regions; // Array of memory regions
    int num_regions;            // Number of regions
};
```

**Memory Layout:**
```
Guest Physical Address Space:

0x00000000 ──────────────────────────────────────────────
│                                                            │
│                   Low RAM (up to 256MB)                   │
│                   (Code, data, stack)                     │
│                                                            │
0x0BFFFFFF ──────────────────────────────────────────────
│                   PCI Hole (unmapped)                     │
0x0C000000 ──────────────────────────────────────────────
│                                                            │
0x90000000 ──────────────────────────────────────────────
│                   MMIO Debug Console (4KB)               │
0x90000FFF ──────────────────────────────────────────────
│                                                            │
0x00A00000 ──────────────────────────────────────────────
│                   Virtio Console (4KB)                   │
0x00A00FFF ──────────────────────────────────────────────
│                                                            │
0x00A01000 ──────────────────────────────────────────────
│                   Virtio Block (4KB)                     │
0x00A01FFF ──────────────────────────────────────────────
│                                                            │
0x00A02000 ──────────────────────────────────────────────
│                   Virtio Network (4KB)                   │
0x00A02FFF ──────────────────────────────────────────────
│                                                            │
0x100000000 ──────────────────────────────────────────────
│                   High RAM (above 4GB)                    │
│                                                            │
└───────────────────────────────────────────────────────────
```

### 4. Device Emulation (`devices/`, `include/devices.h`)

**Device Framework:**
```c
struct device {
    char *name;                  // Device name
    uint64_t base;               // Base GPA address
    uint64_t size;               // Address range size
    void *private;               // Device-specific data

    // Callbacks
    int (*read)(struct device *dev, uint64_t offset,
                void *data, size_t size);
    int (*write)(struct device *dev, uint64_t offset,
                 void *data, size_t size);
    void (*destroy)(struct device *dev);
};
```

**Device Types:**

1. **MMIO Debug Console** (`devices/mmio.c`)
   - Base: `0x90000000`
   - Simple UART-like character output
   - Writes go to stdout and log file

2. **Virtio Console** (`devices/virtio-console.c`)
   - Base: `0x00A00000`
   - Virtio transport for console I/O
   - Queue-based communication

3. **Virtio Block** (`devices/virtio-block.c`)
   - Base: `0x00A01000`
   - Block device emulation
   - Disk image backend

4. **Virtio Network** (`devices/virtio-net.c`)
   - Base: `0x00A02000`
   - Network device emulation
   - TAP device backend

---

## Execution Workflow

### Phase 1: Initialization

```
main.c:main()
    │
    ├─→ Parse command-line arguments
    │   └─→ args: kernel, initrd, memory, cpus, devices
    │
    ├─→ hypervisor_detect()
    │   └─→ Auto-detect platform (Linux/macOS, x86_64/ARM64)
    │
    ├─→ hypervisor_init()
    │   ├─→ Load hypervisor framework (KVM or HVF)
    │   └─→ Initialize hypervisor-specific resources
    │
    ├─→ vm_create()
    │   ├─→ Create VM handle (hv_vm_create() or kvm_create_vm())
    │   ├─→ Initialize memory map
    │   └─→ Allocate device array
    │
    ├─→ vm_add_memory() [for each memory region]
    │   ├─→ Allocate host memory (malloc, mmap, or hv_map())
    │   ├─→ Map GPA → HVA
    │   └─→ Register with hypervisor (KVM_SET_USER_MEMORY_REGION)
    │
    ├─→ vm_create_vcpu() [for each vCPU]
    │   ├─→ Create vCPU handle
    │   ├─→ Allocate vCPU run structure
    │   ├─→ Initialize registers (PC, SP, general-purpose)
    │   └─→ Set up special registers (system registers, page tables)
    │
    ├─→ vm_register_device() [for each device]
    │   ├─→ Create device instance
    │   ├─→ Initialize device-specific state
    │   └─→ Add to device array for MMIO routing
    │
    └─→ boot_load_kernel()
        ├─→ Detect kernel format (ELF, raw binary, bzImage)
        ├─→ Load kernel into guest memory
        ├─→ Set entry point (PC register)
        └─→ Configure boot parameters (if any)
```

### Phase 2: VM Execution Loop

```
For each vCPU:
    │
    └─→ vcpu_run() [Main execution loop]
        │
        ├─→ hypervisor run (HvVCPURun() or ioctl(KVM_RUN))
        │   └─→ Transitions to guest mode
        │       Guest executes until VM exit
        │
        ├─→ ◄─── VM Exit occurs ───┐
        │                           │
        ├─→ Get exit info          │
        │   ├─→ Exit reason        │
        │   ├─→ Exception info     │
        │   └─→ Register state     │
        │                           │
        ├─→ Dispatch exit handler  │
        │   │                       │
        │   ├─→ MMIO Exit?         │
        │   │   └─→ vcpu_handle_mmio_exit()
        │   │       ├─→ Find device by GPA
        │   │       ├─→ Call device->read() or device->write()
        │   │       ├─→ Emulate device behavior
        │   │       └─→ Advance PC past MMIO instruction
        │   │
        │   ├─→ IO Exit?           │
        │   │   └─→ vcpu_handle_io_exit()
        │   │       └─→ Port-mapped I/O (x86 only)
        │   │
        │   ├─→ Exception?         │
        │   │   └─→ Architecture-specific handler
        │   │       ├─→ Page fault → MMIO or EPT fault
        │   │       ├─→ Invalid opcode → Emulation or trap
        │   │       └─→ General protection → Guest error
        │   │
        │   └─→ HLT?               │
        │       └─→ Idle vCPU      │
        │                           │
        └─→ Loop back to run ───────┘

```

### Phase 3: VM Exit Handling Detail

#### MMIO Exit Handling (ARM64 Example)

```
ARM64 MMIO Exit Flow (hvf_arm64.c):

1. VM Exit occurs with exception syndrome
   ├─→ Exit reason: EXCEPTION
   ├─→ Syndrome: 0x93000046 (MMIO write)
   ├─→ Virtual address: 0x90000000 (MMIO base)
   └─→ Physical address: 0x90000000

2. Extract MMIO info from syndrome
   ├─→ Decode instruction (STRB, STRH, STR, LDRB, etc.)
   ├─→ Get register operand (which register?)
   ├─→ Get data size (byte, half-word, word)
   └─→ Extract data value from register

3. Call vcpu_handle_mmio_exit()
   ├─→ Search device array for matching GPA range
   │   └─→ Found: mmio-console at 0x90000000
   ├─→ Calculate device offset: addr - base
   └─→ Dispatch to device callback:
       └─→ mmio_console_write()
           ├─→ Write character to stdout
           ├─→ Write to log file (vmm_console.log)
           └─→ Return success

4. Advance PC
   └─→ PC += instruction_size (4 bytes for ARM64)

5. Return to hypervisor run loop
```

### Phase 4: Shutdown

```
Shutdown Flow:

1. User presses Ctrl+C or guest halts
    │
2. Signal handler (SIGINT, SIGTERM)
    │   └─→ Set shutdown flag
    │
3. VM execution stops
    │
4. vm_stop()
    │   └─→ Signal all vCPUs to exit
    │
5. vm_destroy()
    ├─→ Destroy all vCPUs
    │   └─→ vcpu_destroy() for each vCPU
    ├─→ Unmap memory regions
    ├─→ Destroy all devices
    │   └─→ device->destroy() for each device
    └─→ Free VM structure
    │
6. hypervisor_cleanup()
    └─→ Release hypervisor resources
```

---

## Memory Management

### GPA to HVA Translation

The VMM maintains a mapping from Guest Physical Addresses (GPA) to Host Virtual Addresses (HVA):

```
Guest Access: 0x10000 (instruction fetch)
    │
    ├─→ Lookup in mem_map
    │   └─→ Find region containing 0x10000
    │       └─→ Region: 0x0 - 0x07FFFFFF
    │           └─→ HVA base: 0x...
    │
    ├─→ Calculate HVA
    │   └─→ HVA = base_hva + (GPA - base_gpa)
    │       └─→ HVA = base_hva + 0x10000
    │
    └─→ Access host memory at HVA
        └─→ Fetch instruction from host memory
```

### Memory Slot Registration

**On KVM (Linux):**
```c
struct kvm_userspace_memory_region region = {
    .slot = slot_id,
    .flags = KVM_MEM_LOG_DIRTY_PAGES,
    .guest_phys_addr = gpa,
    .memory_size = size,
    .userspace_addr = (uint64_t)hva,
};
ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region);
```

**On HVF (macOS):**
```c
hv_vm_map(hva, gpa, size, HV_MEMORY_READ | HV_MEMORY_WRITE);
```

### MMIO Region Handling

MMIO regions are NOT mapped to host memory. Instead, they trigger VM exits:

```
Guest Access: 0x90000000 (MMIO write)
    │
    ├─→ Hardware checks EPT/NPT tables
    │   └─→ No translation found → VM exit
    │
    ├─→ Hypervisor reports MMIO exit
    │
    └─→ VMM handles MMIO
        └─→ Device emulation callback
```

---

## Device Emulation

### Device Registration

```c
struct device *dev = mmio_console_create();
vm_register_device(vm, dev);
    │
    ├─→ Add to vm->devices array
    ├─→ Record GPA range (base, size)
    └─→ For MMIO routing in exit handler
```

### MMIO Access Routing

```c
// In vcpu_handle_mmio_exit()

// 1. Find device covering this GPA
for (int i = 0; i < vm->num_devices; i++) {
    struct device *dev = vm->devices[i];
    if (addr >= dev->base && addr < dev->base + dev->size) {
        // 2. Found! Calculate offset and dispatch
        offset = addr - dev->base;
        if (is_write) {
            dev->write(dev, offset, data, size);
        } else {
            dev->read(dev, offset, data, size);
        }
        return 0;
    }
}

// 3. Not found → Unmapped MMIO access
log_warn("MMIO to unmapped address: 0x%lx", addr);
```

### Virtio Device Lifecycle

```
1. Device Creation
   └─→ virtio_xxx_create()
       ├─→ Allocate device structure
       ├─→ Initialize virtio queues
       ├─→ Set device features
       └─→ Register device with VM

2. Guest Driver Initialization (in guest OS)
   ├─→ Detect device (MMIO probe)
   ├─→ Handshake (ACK, DRIVER, FEATURES_OK)
   ├─→ Configure queues
   └─→ Set DRIVER_OK

3. Device Operation
   ├─→ Guest puts requests in virtqueue
   ├─→ VM exit on queue notification
   ├─→ VMM processes requests
   └─→ VMM pushes responses to virtqueue

4. Device Destruction
   └─→ virtio_xxx_destroy()
       ├─→ Clean up queues
       └─→ Free device structure
```

---

## Hypervisor Abstraction

### Abstract Interface

The hypervisor abstraction layer (`include/hypervisor.h`) defines a common API:

```c
// Hypervisor operations
struct hypervisor {
    int (*init)(void);
    void (*cleanup)(void);
    void* (*vm_create)(void);
    int (*vm_destroy)(void *vm);
    void* (*vcpu_create)(void *vm, int vcpu_id);
    int (*vcpu_destroy)(void *vcpu);
    int (*vcpu_run)(void *vcpu);
    int (*vcpu_get_regs)(void *vcpu, struct regs *regs);
    int (*vcpu_set_regs)(void *vcpu, struct regs *regs);
    // ... more operations
};
```

### Platform Detection

```c
// Auto-detection logic in hypervisor.c

#if defined(__APPLE__)
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

### Operation Mapping

Each hypervisor backend implements the interface:

| Operation | KVM (Linux) | HVF (macOS ARM64) | HVF (macOS x86) |
|-----------|-------------|-------------------|-----------------|
| Create VM | ioctl(KVM_CREATE_VM) | hv_vm_create() | hv_vm_create() |
| Map memory | KVM_SET_USER_MEMORY_REGION | hv_vm_map() | hv_vm_map() |
| Create vCPU | ioctl(KVM_CREATE_VCPU) | hv_vcpu_create() | hv_vcpu_create() |
| Run vCPU | ioctl(KVM_RUN) | hv_vcpu_run() | hv_vcpu_run() |
| Get regs | KVM_GET_REGS | hv_vcpu_get_reg() | hv_vcpu_get_state() |
| Set regs | KVM_SET_REGS | hv_vcpu_set_reg() | hv_vcpu_set_state() |

---

## ARM64-Specific Implementation

### ARM64 Virtualization Extensions

Apple Silicon ARM64 uses the Virtualization Host Extensions (VHE):

- **EL2** - Hypervisor exception level (VMM runs here)
- **EL1** - Guest OS exception level
- **EL0** - Guest application exception level

### ARM64 VM Exit Reasons

HVF reports ARM64 VM exits through exception syndrome (ESR_EL1):

```
Exception Syndrome Format:

┌─────────────────────────────────────────────────────────────┐
│ ISS [23:0] │ │ EC │ │ ISS [31:26] │ IL │ │ EC [5:0] │      │
└─────────────────────────────────────────────────────────────┘

EC (Exception Class):
  0x20 = Instruction Abort
  0x24 = Data Abort
  0x30 = Breakpoint
  0x01 = Trapped WFI/WFE

ISS (Instruction Specific Syndrome):
  For MMIO: Contains fault address and access details
```

### ARM64 Register State

```c
struct hvf_vcpu_state {
    // General-purpose registers
    uint64_t x[31];          // x0-x30
    uint64_t pc;             // Program Counter
    uint64_t sp;             // Stack Pointer

    // System registers
    uint64_t cpsr;           // Current Program Status Register
    uint64_t spsr;           // Saved Program Status Register
    uint64_t elr_el1;        // Exception Link Register
    uint64_t esr_el1;        // Exception Syndrome Register
    uint64_t far_el1;        // Fault Address Register

    // System control registers
    uint64_t sctlr_el1;      // System Control Register
    uint64_t tcr_el1;        // Translation Control Register
    uint64_t ttbr0_el1;      // Translation Table Base Register 0
    uint64_t ttbr1_el1;      // Translation Table Base Register 1

    // Virtualization registers
    uint64_t vmpidr_el2;     // Virtual Multiprocessor ID
    uint64_t hcr_el2;        // Hypervisor Configuration Register
};
```

### ARM64 MMIO Instruction Emulation

When a VM exit occurs on an MMIO access, HVF provides:

```c
// Example: STRB W0, [X1]  (Store Byte)

Exit Info:
  ├─→ syndrome: 0x93000046
  │   ├─→ EC = 0x24 (Data Abort)
  │   ├─→ ISV = 1 (Instruction Syndrome Valid)
  │   ├─→ SAS = 0b00 (Byte access)
  │   ├─→ SSE = 0 (Store operation)
  │   ├─→ SRT = 0 (Source Register: X0)
  │   └─→ WnR = 1 (Write, not Read)
  ├─→ virtual_address: 0x90000000
  ├─→ physical_address: 0x90000000
  └─→ PC: 0x1000c

Emulation steps:
  1. Decode instruction type (STRB)
  2. Extract data from X0
  3. Call device write handler with data
  4. Advance PC by 4 bytes
```

### ARM64 Interrupt Injection

Currently stubbed in `hvf_arm64.c`. To be implemented:

```c
// Future implementation
int hvf_arm64_inject_irq(struct vcpu *vcpu, int irq) {
    // 1. Set IRQ pending in virtual GIC
    // 2. Set HCR_EL2.VI (Virtual IRQ) flag
    // 3. On next VM entry, hardware will inject IRQ to guest
    return 0;
}
```

---

## Performance Considerations

### VM Exit Frequency

VM exits are expensive. Common causes:

| Exit Type | Frequency | Optimization |
|-----------|-----------|--------------|
| MMIO access | High (per device access) | Batch operations, use virtio |
| Timer interrupt | Medium (~1000 Hz) | Use paravirtualized clock |
| Page fault | Low (after warmup) | Pre-fault pages, use huge pages |
| CPUID access | Low (boot time only) | Cache results |

### Memory Access Patterns

- **DMA**: Guest can access host memory directly (after mapping)
- **MMIO**: Every access triggers VM exit (slow)
- **Virtio**: Queues in shared memory, notification via MMIO (fast)

### VMM Overhead

Typical VMM overhead breakdown:

- **VM exit/entry**: ~2000-5000 cycles
- **Context switch**: ~1000-2000 cycles
- **Instruction emulation**: ~100-500 cycles
- **Device processing**: Variable (I/O bound)

---

## Debugging and Troubleshooting

### Common Issues

1. **Instruction Abort at NULL**
   - Symptom: ESR=0x7e00000, FA=0x0
   - Cause: Position-dependent code with literal pools
   - Fix: Use position-independent code (movz/movk)

2. **MMIO to unmapped address**
   - Symptom: Data abort on MMIO access
   - Cause: Device not registered or wrong GPA
   - Fix: Enable device with correct base address

3. **Guest hangs on boot**
   - Cause: Wrong entry point or missing boot parameters
   - Fix: Verify entry point matches kernel load address

### Debug Flags

```bash
# Enable debug logging
./vibevmm --log 4 --binary kernel.raw --entry 0x10000 --mem 128M

# Log levels:
# 0 = none
# 1 = error
# 2 = warn
# 3 = info
# 4 = debug
```

### Log Analysis

Check `vmm_console.log` for guest output and VM exit traces:

```
[INFO] VM exit: MMIO access at GPA 0x90000000, data=0x48 (from X0)
[INFO] Found device 'mmio-console' at GPA 0x90000000 (offset=0)
[INFO] MMIO console write: offset=0, data=0x48 (H), size=1
```

---

## Future Enhancements

### Planned Features

1. **ARM64 GIC Support**
   - Virtual Generic Interrupt Controller
   - IRQ/FIQ injection
   - SPI (Shared Peripheral Interrupt) routing

2. **ARM64 System Register Handling**
   - Complete read/write support for all system registers
   - TLB invalidation emulation
   - Cache management operations

3. **Live Migration**
   - VM state serialization
   - Memory migration
   - Network-based transfer

4. **Snapshot/Restore**
   - Save VM to disk
   - Restore from snapshot
   - Checkpointing support

5. **Advanced Device Emulation**
   - Full virtio-blk with async I/O
   - Virtio-net with packet filtering
   - PCI device passthrough with VFIO

### Architecture Improvements

- Multi-threaded vCPU execution (one thread per vCPU)
- IOMMU support for device passthrough
- Huge page backing for guest memory
- Dynamic memory ballooning
- Paravirtualized devices (virtio-balloon, virtio-rng)

---

## References

- [ARM Virtualization Extensions](https://developer.arm.com/documentation/ddi0487/latest)
- [Apple Hypervisor Framework](https://developer.apple.com/documentation/hypervisor)
- [Linux KVM API](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Virtio Specification](https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.html)
- [Intel VT-x Specifications](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)

---

**Document Version:** 1.0
**Last Updated:** 2026-02-06
**Author:** Vibe-VMM Project
