# Vibe-VMM

A minimal Virtual Machine Monitor (VMM) written in C, supporting both x86_64 and ARM64 architectures.

## Features

- **Multi-Architecture Support**
  - ✅ x86_64 on Linux (KVM)
  - ✅ x86_64 on macOS Intel (HVF)
  - ✅ ARM64 on macOS Apple Silicon (HVF)
- **Hypervisor Abstraction Layer** - Clean separation between VMM core and hypervisor backends
- **Complete VM Lifecycle Management** - Create, configure, run, and destroy VMs
- **Device Emulation** - Virtio console, block, and network devices
- **SR-IOV VF Passthrough** - VFIO-based device passthrough (Linux)

## Platform Support

| Platform | Architecture | Hypervisor | Status |
|----------|--------------|------------|--------|
| Linux | x86_64 | KVM | ✅ Full Support |
| macOS (Intel) | x86_64 | HVF | ✅ Full Support |
| macOS (Apple Silicon) | ARM64 | HVF | ✅ Supported (Basic VM execution) |

### Apple Silicon Notes

**What Works:**
- HVF initialization and VM creation
- Memory mapping (GPA → HVA)
- vCPU creation and execution
- ARM64 code execution
- VM exit handling

**Limitations:**
- ARM64 register read/write is stubbed (TODO)
- ARM64 system register handling is stubbed (TODO)
- No interrupt injection (TODO)

## Quick Start

### Linux (x86_64)

```bash
# Install dependencies
sudo apt install git gcc make nasm

# Build
git clone <repo>
cd vibe-vmm
make

# Build test kernel
cd tests/kernels && make x86_64 && cd ../..

# Run
sudo ./bin/vibevmm --binary tests/kernels/x86_64_simple.bin --entry 0x1000 --mem 128M
```

### macOS Intel (x86_64)

```bash
# Build
make

# Build test kernel
cd tests/kernels && ./build.sh && cd ../..

# Run
./bin/vibevmm --binary tests/kernels/x86_64_simple.bin --entry 0x1000 --mem 128M
```

### macOS Apple Silicon (ARM64)

```bash
# Build
make

# Build test kernel
cd tests/kernels && ./build.sh && cd ../..

# Option 1: Sign and run (recommended)
./run.sh --binary tests/kernels/arm64_simple.raw --entry 0x1000 --mem 128M

# Option 2: Run with sudo
sudo ./bin/vibevmm --binary tests/kernels/arm64_simple.raw --entry 0x1000 --mem 128M

# Quick test
./quicktest.sh
```

## Command Line Options

| Option | Description |
|--------|-------------|
| `--kernel <path>` | Guest kernel (bzImage/vmlinux) |
| `--initrd <path>` | Initial ramdisk |
| `--cmdline <string>` | Kernel command line |
| `--mem <size>` | Guest memory (e.g., 512M, 1G) |
| `--cpus <num>` | Number of vCPUs (default: 1) |
| `--disk <path>` | Disk image for virtio-blk |
| `--net tap=<if>` | TAP interface for virtio-net |
| `--console` | Enable MMIO debug console |
| `--binary <path>` | Load raw binary |
| `--entry <addr>` | Entry point for raw binary (hex) |
| `--log <level>` | Log level: 0=none, 1=error, 2=warn, 3=info, 4=debug |
| `--help` | Show help message |

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                Application Layer                    │
│                  (main.c / CLI)                   │
├─────────────────────────────────────────────────────┤
│                   VMM Core                         │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────────┐ │
│  │  VM    │ │  vCPU  │ │   MM   │ │  Devices   │ │
│  │ Mgmt   │ │  Run   │ │  Mgmt  │ │ Emulation  │ │
│  └────────┘ └────────┘ └────────┘ └────────────┘ │
├─────────────────────────────────────────────────────┤
│         Hypervisor Abstraction Layer                │
│  ┌──────────────────────────────────────────┐     │
│  │    hypervisor.h (abstract interface)     │     │
│  ├──────────────────────────────────────────┤     │
│  │ KVM (x86) │ HVF (x86) │ HVF (ARM64)    │     │
│  └────────────┴────────────┴───────────────┘     │
├─────────────────────────────────────────────────────┤
│                  Host OS Layer                     │
│         (Linux KVM / macOS HVF x86/ARM64)          │
└─────────────────────────────────────────────────────┘
```

## File Structure

```
vibe-vmm/
├── include/                  # Public headers
│   ├── hypervisor.h         # Hypervisor abstraction
│   ├── vm.h                 # VM management
│   ├── vcpu.h               # vCPU management
│   ├── mm.h                 # Memory management
│   ├── devices.h            # Device framework
│   ├── virtio.h             # Virtio definitions
│   ├── vfio.h               # VFIO/SR-IOV support
│   ├── boot.h               # Boot loader
│   ├── arch_x86_64.h        # x86_64 definitions
│   ├── arch_arm64.h         # ARM64 definitions
│   └── utils.h              # Utilities
├── src/
│   ├── hypervisor/          # Hypervisor backends
│   │   ├── kvm.c           # Linux KVM (x86_64)
│   │   ├── hvf.c           # macOS HVF (x86_64)
│   │   ├── hvf_arm64.c     # macOS HVF (ARM64)
│   │   ├── kvm_stub.c      # KVM stub for non-Linux
│   │   └── hvf_stub.c      # x86_64 HVF stub for ARM64
│   ├── devices/             # Device emulation
│   │   ├── mmio.c          # MMIO debug console
│   │   ├── virtio.c        # Virtio common
│   │   ├── virtio-console.c
│   │   ├── virtio-block.c
│   │   └── virtio-net.c
│   ├── vm.c
│   ├── vcpu.c
│   ├── mm.c
│   ├── boot.c
│   ├── vfio.c
│   ├── devices.c
│   └── main.c
├── tests/kernels/           # Test kernels
│   ├── build.sh            # Build script (multi-arch)
│   ├── x86_64_simple.S     # x86_64 test kernel
│   └── arm64_simple.raw.S  # ARM64 test kernel
├── Makefile
├── run.sh                  # Auto-sign and run script
├── quicktest.sh            # Quick test script
├── entitlements.plist      # Hypervisor entitlements
└── README.md
```

## Building from Source

### Requirements

**Linux:**
- GCC or Clang
- Make
- NASM (for x86_64 test kernels)
- KVM support in kernel

**macOS:**
- Xcode command-line tools
- Clang
- Make

### Build Commands

```bash
make              # Build VMM
make clean        # Clean build artifacts
make debug        # Debug build
make release      # Optimized release build
```

### Building Test Kernels

```bash
cd tests/kernels
./build.sh        # Builds all applicable kernels for your platform
```

## Memory Layout

```
Guest Physical Address Space:
0x00000000 - 0x0BFFFFFF  : Low RAM (up to 256MB)
0x0C000000 - 0x0CFFFFFF  : PCI hole (not mapped)
0x9000000                : MMIO debug console
0xa000000                : Virtio console
0xa001000                : Virtio block
0xa002000                : Virtio network
0xb000000+               : VFIO device BARs
0x100000000+             : High RAM (above 4GB)
```

## Testing

### Quick Test

```bash
# Automated test
./quicktest.sh

# Manual test
./run.sh --binary tests/kernels/arm64_simple.raw --entry 0x1000 --mem 128M
```

### Expected Output

```
[INFO] Auto-detected: Apple Silicon (ARM64) with HVF
[INFO] HVF ARM64 initialized
[INFO] Created ARM64 VM
[INFO] Added memory region: GPA 0x0 -> HVA 0x...
[INFO] Created ARM64 vCPU 0
[INFO] Loaded raw binary: 8 bytes at 0x1000
[INFO] VM started
```

## Security Notes

- This VMM requires root/capabilities to access hypervisor
- VFIO passthrough requires proper IOMMU setup
- Network devices should be properly isolated
- Always use signed/trusted kernel images

## Limitations

- No live migration or snapshotting
- No advanced features like memory ballooning
- Minimal PCI emulation
- ARM64 register handling is incomplete (stubs)

## Future Work

**ARM64 Enhancements:**
- [ ] Implement ARM64 register read/write
- [ ] Implement ARM64 system register handling
- [ ] Add GIC (Generic Interrupt Controller) support
- [ ] Add ARM64 interrupt injection

**General:**
- [ ] Live migration support
- [ ] Snapshot/restore functionality
- [ ] Memory ballooning
- [ ] Enhanced device emulation

## Contributing

This is a minimal VMM for educational purposes. Contributions welcome!

## License

Educational/research code. Use at your own risk.

## References

- [Linux KVM API Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)
- [Apple Hypervisor.framework](https://developer.apple.com/documentation/hypervisor)
- [Virtio Specification](https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.html)
- [VFIO Documentation](https://www.kernel.org/doc/html/latest/driver-api/vfio.html)
