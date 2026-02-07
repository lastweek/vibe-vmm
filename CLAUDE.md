# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Vibe-VMM is a minimal Virtual Machine Monitor (VMM) written in C (~7000 lines). It provides a clean abstraction layer over hardware virtualization technologies (KVM on Linux, HVF on macOS) while supporting multiple architectures (x86_64 and ARM64).

**Core Design Philosophy:** The VMM works by intercepting physical address accesses. Guest physical addresses (GPA) are virtualized - accesses to RAM go directly to host memory, while MMIO accesses trigger VM exits for device emulation. This gives the VMM control over I/O devices and privileged operations while maintaining the illusion of direct hardware access.

## Build and Test Commands

### Building

```bash
make              # Build VMM (auto-detects platform)
make clean        # Clean build artifacts (use `rm -f` in Makefile for files that may not exist)
make debug        # Debug build
make release      # Optimized release build
```

### Building Test Kernels (ARM64 on Apple Silicon)

```bash
cd tests/kernels
./build.sh        # Builds arm64_hello.raw and arm64_minimal.raw
```

**Important:** ARM64 test kernels must use position-independent code (movz/movk instructions, not literal pools). Raw binary loading means absolute addresses won't work.

### Running Tests

```bash
# Quick automated test
./quicktest.sh

# Manual test - ARM64 (prints "Hello World!" repeatedly)
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M

# Manual test - minimal ARM64 (prints "Hi" once)
./run.sh --binary tests/kernels/arm64_minimal.raw --entry 0x10000 --mem 128M

# With debug logging
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M --log 4
```

### macOS-Specific: Code Signing

The VMM binary must be signed with hypervisor entitlements on Apple Silicon:

```bash
# run.sh handles this automatically, or manually:
codesign --entitlements entitlements.plist --force --deep -s - bin/vibevmm
```

## Architecture Overview

### Layer Structure

```
Application Layer (CLI - main.c)
    ↓
VMM Core Layer
  - vm.c: VM lifecycle, device registration
  - vcpu.c: vCPU execution, VM exit handling loop
  - mm.c: GPA → HVA memory mapping
  - devices.c: Device framework and MMIO routing
  - boot.c: Kernel loading (ELF, raw binary, bzImage)
    ↓
Hypervisor Abstraction Layer (hypervisor.h)
  - src/hypervisor/kvm.c: Linux KVM backend (x86_64)
  - src/hypervisor/hvf.c: macOS HVF backend (x86_64)
  - src/hypervisor/hvf_arm64.c: macOS HVF backend (ARM64)
  - Stub implementations for non-native platforms
    ↓
Hardware/OS Layer (KVM or HVF)
```

### The VM Exit Loop (Heart of the VMM)

Located in `src/vcpu.c` - this is the core execution pattern:

```c
while (!vcpu->should_stop) {
    ret = vcpu_run(vcpu);                    // Enter guest mode

    if (ret < 0) {
        // Handle errors or signals
        break;
    }

    ret = hv_get_exit(vcpu->hv_vcpu, &exit); // Get exit info

    switch (exit.reason) {
        case HV_EXIT_MMIO:                   // Device access
            vcpu_handle_mmio_exit(vcpu, &exit);
            break;
        case HV_EXIT_EXCEPTION:              // Guest exception
            vcpu_handle_exception(vcpu, &exit);
            break;
        // ... other exit types
    }

    advance_pc_past_instruction(vcpu);       // Resume guest
}
```

### Memory Management: GPA → HVA Translation

The core virtualization primitive in `src/mm.c`:

- Guest sees physical addresses (GPA): 0x00000000 - 0x0BFFFFFF (RAM)
- VMM translates to host virtual addresses (HVA)
- MMIO regions (0x90000000, 0x00A00000, etc.) are NOT mapped - they trigger VM exits
- Translation lookup is done on every VM exit for MMIO handling

### Device Emulation Flow

```
Guest executes: strb w0, [x1]  (write to 0x90000000)
    ↓
VM Exit (MMIO access to unmapped address)
    ↓
vcpu_handle_mmio_exit() in src/vcpu.c
    ↓
Searches vm->devices[] for matching GPA range
    ↓
Calls device->write() callback
    ↓
Device emulates behavior (e.g., mmio_console prints character)
    ↓
VMM advances PC past the instruction
    ↓
Guest resumes
```

## Platform-Specific Implementation Details

### ARM64 on Apple Silicon (HVF)

**File:** `src/hypervisor/hvf_arm64.c`

**Key Implementation Details:**
- ARM64 uses Exception Syndrome Register (ESR_EL1) to encode exit reasons
- MMIO exits are reported as data aborts with specific syndrome bits
- WFI (Wait For Interrupt) instruction causes VM exits
- vCPU must be created in the same thread that runs it (HVF limitation)
- Position-independent code is **critical** for ARM64 test kernels

**ARM64 VM Exit Detection Pattern:**
```c
// Check syndrome bits for MMIO access
if (syndrome == 0x93000046) {  // Data abort, write, SIMD
    // This is an MMIO write - decode instruction
    // Extract data from register, call device handler
}

// Instruction Abort at NULL (common bug)
if (syndrome == 0x7e00000 && fault_addr == 0) {
    // Guest tried to execute from address 0
    // Usually caused by literal pools in position-dependent code
}
```

**Important:** ARM64 register read/write is partially stubbed. System register handling is incomplete. Interrupt injection is not implemented.

### x86_64 on Linux (KVM)

**File:** `src/hypervisor/kvm.c`

Uses ioctl interface:
- `KVM_CREATE_VM`, `KVM_CREATE_VCPU` for creation
- `KVM_RUN` for execution (blocks until VM exit)
- `KVM_GET_REGS`, `KVM_SET_REGS` for register access
- `KVM_SET_USER_MEMORY_REGION` for memory mapping

### Device Registration

Devices are registered in `src/main.c`:

```c
struct device *dev = mmio_console_create();
vm_register_device(vm, dev);
    // Adds to vm->devices[] array
    // Records GPA range (base, size)
    // Used for MMIO routing in exit handler
```

**Current Devices:**
- `src/devices/mmio.c`: MMIO debug console at 0x90000000 (auto-enabled)
- `src/devices/virtio-*.c`: Virtio console, block, network devices

## Critical Code Patterns

### Position-Independent Code (ARM64)

**WRONG:** Uses literal pools (position-dependent)
```assembly
ldr x0, =message     // Literal pool contains absolute address
strb w0, [x1]
```

**CORRECT:** Uses movz/movk (position-independent)
```assembly
movz x0, #0x0048
movk x0, #0x0000, lsl #16   // x0 = 0x00000048 ('H')
strb w0, [x1]
```

See `tests/kernels/arm64_hello.S` for correct patterns.

### Error Message Boxes

ARM64 implementation includes helpful error detection for common issues:

- **Instruction Abort at NULL (0x7e00000)**: Suggests position-dependent code issue
- **MMIO to unmapped address**: Suggests device not enabled or wrong GPA

Located in `src/hypervisor/hvf_arm64.c` around line 409-437.

### Hypervisor Abstraction Pattern

Each hypervisor backend implements the `hv_ops` interface:

```c
const struct hv_ops hvf_arm64_ops = {
    .init = hvf_arm64_init,
    .vm_create = hvf_arm64_vm_create,
    .vcpu_create = hvf_arm64_vcpu_create,
    .vcpu_run = hvf_arm64_vcpu_run,
    // ... more operations
};
```

Platform detection in `src/hypervisor.c` selects the appropriate backend.

## Memory Layout (Guest Physical Address Space)

```
0x00000000 - 0x0BFFFFFF  : Low RAM (up to 256MB) - Mapped to host memory
0x0C000000 - 0x8FFFFFFF  : PCI hole (unmapped)
0x90000000               : MMIO debug console (4KB)
0x00A00000               : Virtio console (4KB)
0x00A01000               : Virtio block (4KB)
0x00A02000               : Virtio network (4KB)
0x100000000+             : High RAM (above 4GB)
```

MMIO regions intentionally unmapped to trigger VM exits for device emulation.

## Common Pitfalls

1. **Literal pools in ARM64 kernels**: Causes "Instruction Abort at NULL". Use movz/movk instead.
2. **Wrong entry point**: Kernel won't start. Ensure entry point matches load address.
3. **Missing device registration**: "MMIO to unmapped address" errors. MMIO console is now auto-enabled.
4. **Not signing binary on macOS**: HVF operations fail. Use `run.sh` or sign manually.
5. **Clean target failing**: Use `rm -f` instead of `rm` for files that may not exist.

## Future Development Areas

### ARM64 Enhancements (High Priority)
- Implement complete register read/write
- Add system register handling (SCTLR, TCR, TTBR)
- Implement GIC (Generic Interrupt Controller)
- Add interrupt injection

### General Enhancements
- Live migration support
- Snapshot/restore functionality
- Memory ballooning
- Enhanced virtio device implementations

## Key Files for Understanding

1. **`docs/README.md`**: Essence of virtualization - physical address interception
2. **`src/vcpu.c`**: VM exit handling loop - core VMM logic
3. **`src/hypervisor/hvf_arm64.c`**: ARM64 virtualization implementation
4. **`src/mm.c`**: GPA → HVA translation
5. **`tests/kernels/arm64_hello.S`**: Position-independent ARM64 code example
6. **`include/hypervisor.h`**: Hypervisor abstraction interface

## References

- [Architecture Overview](docs/README.md) - Start here for understanding
- [ARM Architecture](docs/architecture-overview.md) - Complete system architecture
- [SR-IOV Documentation](docs/architecture-sriov.md) - Device passthrough (Linux)
