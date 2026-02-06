# Vibe-VMM Documentation

Welcome to the Vibe-VMM documentation. This minimal Virtual Machine Monitor supports both x86_64 and ARM64 architectures across Linux and macOS platforms.

## Quick Start

- **New Users?** Start with the [main README](../README.md)
- **Want to understand the architecture?** See [Architecture Overview](architecture/overview.md)
- **Looking for API reference?** Check [API Documentation](api/)
- **Building from source?** See [Build Guides](guides/)

## Documentation Structure

### 📚 Architecture
- [System Overview](architecture/overview.md) - Complete architecture documentation
- [Memory Management](architecture/memory.md) - GPA→HVA mapping and memory slots
- [Device Emulation](architecture/devices.md) - Device framework and virtio

### 🔌 API Reference
- [Hypervisor API](api/hypervisor.md) - Hypervisor abstraction layer
- [VM API](api/vm.md) - VM lifecycle and management
- [vCPU API](api/vcpu.md) - vCPU execution and exits
- [Device API](api/devices.md) - Device framework

### 📖 Guides
- [Getting Started](guides/getting-started.md) - Installation and first run
- [Building Test Kernels](guides/building-kernels.md) - ARM64 kernel development
- [Debugging VMs](guides/debugging.md) - Tips and troubleshooting
- [Platform Setup](guides/platform-setup.md) - Platform-specific requirements

### 🔧 Internals
- [ARM64 Implementation](internals/arm64.md) - ARM64-specific details
- [HVF Integration](internals/hvf.md) - macOS Hypervisor Framework
- [KVM Integration](internals/kvm.md) - Linux KVM backend
- [VM Exit Handling](internals/vm-exits.md) - Exit types and handlers

## Key Concepts

### What is Vibe-VMM?

Vibe-VMM is a **minimal VMM** designed for education and research. It provides:
- Multi-architecture support (x86_64, ARM64)
- Multi-platform support (Linux KVM, macOS HVF)
- Clean hypervisor abstraction layer
- Pluggable device model
- Raw binary and kernel image loading

### Architecture Layers

```
Application Layer (CLI)
         ↓
    VMM Core Layer
   (VM, vCPU, MM, Devices)
         ↓
 Hypervisor Abstraction
   (KVM, HVF backends)
         ↓
Hardware/OS Layer
```

### Supported Platforms

| Platform | Architecture | Hypervisor | Status |
|----------|--------------|------------|--------|
| Linux | x86_64 | KVM | ✅ Full Support |
| macOS (Intel) | x86_64 | HVF | ✅ Full Support |
| macOS (Apple Silicon) | ARM64 | HVF | ✅ Supported |

## Common Tasks

### Run a Test Kernel (ARM64)

```bash
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M
```

### Run a Test Kernel (x86_64)

```bash
./bin/vibevmm --binary tests/kernels/x86_64_simple.bin --entry 0x1000 --mem 128M
```

### Build from Source

```bash
make
cd tests/kernels && ./build.sh
```

### Quick Test

```bash
./quicktest.sh
```

## Getting Help

- **Issues?** Check [Troubleshooting Guide](guides/debugging.md)
- **Questions?** Review [Architecture Overview](architecture/overview.md)
- **Contributing?** See [Main README](../README.md)

## Documentation Index

- [Architecture Documentation](architecture/) - System design and internals
- [API Documentation](api/) - Programming interfaces
- [User Guides](guides/) - Tutorials and how-tos
- [Internals](internals/) - Platform-specific details

---

**Last Updated:** 2026-02-06
