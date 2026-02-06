# API Reference

This section contains API documentation for Vibe-VMM's public interfaces.

## Core APIs

### Hypervisor API
- **[hypervisor.h](../../include/hypervisor.h)** - Hypervisor abstraction layer
- Platform detection and initialization
- Hypervisor operations interface

### VM Management API
- **[vm.h](../../include/vm.h)** - VM lifecycle management
- VM creation, configuration, and destruction
- Memory and device registration

### vCPU API
- **[vcpu.h](../../include/vcpu.h)** - vCPU management
- vCPU creation and execution
- Register access and VM exit handling

### Memory Management API
- **[mm.h](../../include/mm.h)** - Memory mapping
- GPA → HVA translation
- Memory slot management

### Device API
- **[devices.h](../../include/devices.h)** - Device framework
- Device registration and callbacks
- MMIO handling

### Boot API
- **[boot.h](../../include/boot.h)** - Kernel loading
- ELF, raw binary, and bzImage support
- Entry point configuration

## Architecture-Specific APIs

### x86_64
- **[arch_x86_64.h](../../include/arch_x86_64.h)** - x86_64 definitions
- Register structures
- Segment descriptors
- Page tables

### ARM64
- **[arch_arm64.h](../../include/arch_arm64.h)** - ARM64 definitions
- System registers
- Exception levels
- Virtualization extensions

## Utility APIs

### Utilities
- **[utils.h](../../include/utils.h)** - Utility functions
- Logging macros
- Error handling
- Assertions

## Data Structures

### VM Structure
```c
struct vm {
    void *vm_handle;           // Hypervisor-specific VM handle
    struct vcpu **vcpus;       // Array of vCPUs
    int num_vcpus;             // Number of vCPUs
    struct mem_map *mem_map;   // GPA → HVA mapping
    struct device **devices;   // Registered devices
    int num_devices;           // Number of devices
    void *private;             // Hypervisor-specific data
};
```

### vCPU Structure
```c
struct vcpu {
    struct vm *vm;             // Parent VM
    void *vcpu_handle;         // Hypervisor-specific handle
    int vcpu_id;               // vCPU index
    void *run;                 // Hypervisor run structure
    void *private;             // Hypervisor-specific data
    struct hvf_ops *ops;       // Hypervisor operations
};
```

### Device Structure
```c
struct device {
    char *name;                // Device name
    uint64_t base;             // Base GPA address
    uint64_t size;             // Address range size
    void *private;             // Device-specific data

    // Callbacks
    int (*read)(struct device *dev, uint64_t offset,
                void *data, size_t size);
    int (*write)(struct device *dev, uint64_t offset,
                 void *data, size_t size);
    void (*destroy)(struct device *dev);
};
```

## Function Reference

### VM Lifecycle

#### `vm_create()`
Create a new virtual machine.
```c
struct vm* vm_create(void);
```

#### `vm_start()`
Start VM execution (begin vCPU runs).
```c
int vm_start(struct vm *vm);
```

#### `vm_stop()`
Stop all vCPUs.
```c
int vm_stop(struct vm *vm);
```

#### `vm_destroy()`
Destroy VM and free resources.
```c
void vm_destroy(struct vm *vm);
```

### Memory Management

#### `vm_add_memory()`
Add a memory region to the VM.
```c
int vm_add_memory(struct vm *vm, uint64_t gpa,
                  void *hva, uint64_t size);
```

### Device Management

#### `vm_register_device()`
Register an MMIO device.
```c
int vm_register_device(struct vm *vm, struct device *dev);
```

### vCPU Management

#### `vcpu_run()`
Run vCPU until VM exit.
```c
int vcpu_run(struct vcpu *vcpu);
```

#### `vcpu_get_regs()`
Read vCPU registers.
```c
int vcpu_get_regs(struct vcpu *vcpu, struct regs *regs);
```

#### `vcpu_set_regs()`
Write vCPU registers.
```c
int vcpu_set_regs(struct vcpu *vcpu, struct regs *regs);
```

## Hypervisor Abstraction

### Platform Detection
```c
enum hypervisor_type {
    HYPERVISOR_UNKNOWN,
    HYPERVISOR_KVM,      // Linux KVM
    HYPERVISOR_HVF_X86,  // macOS HVF (x86_64)
    HYPERVISOR_HVF_ARM64 // macOS HVF (ARM64)
};
```

### Operations Table
```c
struct hypervisor_ops {
    int (*init)(void);
    void (*cleanup)(void);
    void* (*vm_create)(void);
    int (*vm_destroy)(void *vm);
    void* (*vcpu_create)(void *vm, int vcpu_id);
    int (*vcpu_run)(void *vcpu);
    // ... more operations
};
```

## Device Callbacks

### Read Callback
Handle MMIO read from device.
```c
int device_read(struct device *dev, uint64_t offset,
                void *data, size_t size);
```

### Write Callback
Handle MMIO write to device.
```c
int device_write(struct device *dev, uint64_t offset,
                 void *data, size_t size);
```

### Destroy Callback
Clean up device resources.
```c
void device_destroy(struct device *dev);
```

## Return Values

- **0**: Success
- **Negative**: Error code (errno-style)
- **Positive**: Informational value (exit reason, etc.)

## Error Handling

All API functions return error codes:
- `-EINVAL`: Invalid argument
- `-ENOMEM`: Out of memory
- `-EIO**: I/O error
- `-ENOSYS**: Function not implemented

Check return values and handle errors appropriately.

## Thread Safety

- VM creation/destruction: Not thread-safe
- vCPU execution: Thread-safe (one vCPU per thread)
- Memory registration: Not thread-safe during VM execution
- Device operations: Depends on device implementation

## Usage Example

```c
#include "hypervisor.h"
#include "vm.h"
#include "vcpu.h"

int main() {
    // Initialize hypervisor
    hypervisor_init();

    // Create VM
    struct vm *vm = vm_create();

    // Add memory
    vm_add_memory(vm, 0x0, mem_ptr, 128*1024*1024);

    // Create vCPU
    struct vcpu *vcpu = vm_create_vcpu(vm, 0);

    // Start VM
    vm_start(vm);

    // Run vCPU
    while (running) {
        int ret = vcpu_run(vcpu);
        if (ret < 0) break;
        // Handle VM exit...
    }

    // Cleanup
    vm_destroy(vm);
    hypervisor_cleanup();

    return 0;
}
```

## Further Reading

- [Architecture Overview](../architecture/overview.md)
- [VM Exit Handling](../internals/vm-exits.md)
- [Platform-Specific APIs](../internals/)
