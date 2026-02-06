# Architecture Documentation

This section contains comprehensive documentation about Vibe-VMM's architecture, design decisions, and internal implementation.

## Overview

- **[System Overview](overview.md)** ⭐ Start here!
  - Complete architecture documentation
  - Component responsibilities
  - Execution workflow
  - Memory management
  - Device emulation
  - Hypervisor abstraction

## Core Components

### VM Management
- VM lifecycle (create, start, stop, destroy)
- vCPU management
- Memory region registration
- Device registration

### vCPU Execution
- vCPU creation and initialization
- Register state management
- VM exit handling
- Instruction emulation

### Memory Management
- GPA → HVA mapping
- Memory slot management
- MMIO region tracking

### Device Emulation
- Device framework
- MMIO devices
- Virtio devices (console, block, network)

## Design Principles

### Minimalism
Essential functionality only, no feature bloat. Focus on core virtualization concepts.

### Portability
Clean abstraction over hypervisor APIs enables multi-platform support.

### Modularity
Well-defined interfaces between components for easy extension.

### Educational
Simple, readable codebase suitable for learning virtualization.

## Architecture Layers

```
┌─────────────────────────────────────────────┐
│         Application Layer (CLI)              │
├─────────────────────────────────────────────┤
│         VMM Core Layer                       │
│  ┌──────┐ ┌──────┐ ┌────┐ ┌──────────┐     │
│  │ VM   │ │ vCPU │ │ MM  │ │ Devices  │     │
│  └──────┘ └──────┘ └────┘ └──────────┘     │
├─────────────────────────────────────────────┤
│      Hypervisor Abstraction Layer           │
│  (KVM | HVF-x86 | HVF-ARM64)                │
├─────────────────────────────────────────────┤
│         Hardware/OS Layer                   │
│  (Linux KVM | macOS HVF)                    │
└─────────────────────────────────────────────┘
```

## Memory Layout

```
Guest Physical Address Space:

0x00000000 ─────────────
│     Low RAM           │
│     (up to 256MB)     │
0x0BFFFFFF ─────────────
│     PCI Hole          │
0x0C000000 ─────────────
│                       │
0x90000000 ─────────────
│  MMIO Console (4KB)   │
0x90000FFF ─────────────
│                       │
0x00A00000 ─────────────
│  Virtio Console (4KB) │
0x00A00FFF ─────────────
│                       │
0x00A01000 ─────────────
│  Virtio Block (4KB)   │
0x00A01FFF ─────────────
│                       │
0x100000000 ────────────
│  High RAM (>4GB)      │
└───────────────────────
```

## Data Flow

### VM Execution Flow

1. **Initialization**
   - Parse CLI arguments
   - Detect platform and hypervisor
   - Create VM and vCPUs
   - Map memory
   - Register devices
   - Load kernel

2. **Execution Loop**
   - Run vCPU in guest mode
   - VM exit occurs
   - Handle exit (MMIO, IO, exception)
   - Return to guest

3. **Shutdown**
   - Stop all vCPUs
   - Destroy devices
   - Unmap memory
   - Clean up resources

### VM Exit Handling

```
VM Exit
  ↓
Get exit info
  ↓
┌─────────────────────────┐
│ MMIO Exit?              │ → Route to device
│ IO Exit?                │ → Port-mapped I/O
│ Exception?              │ → Architecture handler
│ HLT?                    │ → Idle vCPU
└─────────────────────────┘
  ↓
Advance PC
  ↓
Return to guest
```

## Hypervisor Abstraction

Clean interface over multiple hypervisors:

| Operation | KVM (Linux) | HVF (macOS) |
|-----------|-------------|-------------|
| Create VM | ioctl(KVM_CREATE_VM) | hv_vm_create() |
| Map memory | KVM_SET_USER_MEMORY_REGION | hv_vm_map() |
| Create vCPU | ioctl(KVM_CREATE_VCPU) | hv_vcpu_create() |
| Run vCPU | ioctl(KVM_RUN) | hv_vcpu_run() |
| Get regs | KVM_GET_REGS | hv_vcpu_get_reg() |
| Set regs | KVM_SET_REGS | hv_vcpu_set_reg() |

## Platform Support

### Linux (x86_64)
- KVM (Kernel-based Virtual Machine)
- Full feature support
- VFIO device passthrough

### macOS (Intel x86_64)
- HVF (Hypervisor Framework)
- Full feature support
- Native macOS integration

### macOS (Apple Silicon ARM64)
- HVF with VHE (Virtualization Host Extensions)
- Basic VM execution
- MMIO device emulation
- TODO: GIC, interrupt injection

## Further Reading

- [Complete Architecture Overview](overview.md) - Detailed documentation
- [SR-IOV Device Passthrough](sriov.md) - High-performance device assignment
- [Memory Management](memory.md) - GPA→HVA translation
- [Device Emulation](devices.md) - Device framework
- [ARM64 Implementation](../internals/arm64.md) - ARM64 specifics
- [VM Exit Handling](../internals/vm-exits.md) - Exit types and handlers
