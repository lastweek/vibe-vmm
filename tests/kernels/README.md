# Vibe-VMM Test Kernels

## Important: Platform Compatibility

**The Vibe-VMM is designed for x86_64 VMs only.** It does NOT support ARM64 VMs on Apple Silicon.

### Why Apple Silicon Can't Run These Kernels

1. **Vibe-VMM is x86_64-only**
   - The VMM emulates x86_64 architecture (registers, instructions, boot setup)
   - All VM exit handling is x86_64-specific
   - Memory layout follows x86_64 conventions

2. **Apple's Hypervisor.framework has different APIs**
   - **Intel Macs**: Hypervisor.framework supports x86_64 VMs (using VMX)
   - **Apple Silicon**: Hypervisor.framework ONLY supports ARM64 VMs
   - These are completely different APIs with different data structures

3. **The VMM would need to be rewritten** to support ARM64 VMs on Apple Silicon

---

## Test Kernels in This Directory

### x86_64_simple.bin
- **Works with**: VMM on Linux (KVM) or Intel Mac (HVF)
- **Size**: 24 bytes
- **What it does**: Infinite loop with HLT instruction
- **Purpose**: Test basic vCPU execution and VM exit handling

### arm64_simple.S
- **Purpose**: Educational/reference only
- **Does NOT work** with the current VMM
- Shows what an ARM64 bare-metal program looks like

---

## How to Actually Test the VMM

Since you're on Apple Silicon, you have these options:

### Option 1: Linux VM (Recommended)

1. **Install a Linux x86_64 VM** on your Apple Silicon Mac:
   - **UTM** (free, open source)
   - **Parallels Desktop** (paid)
   - **VMware Fusion** (paid)
   - **VirtualBox** (free, but slower)

2. **Inside the Linux VM**:
   ```bash
   # Install build dependencies
   sudo apt install git gcc make nasm  # Debian/Ubuntu

   # Clone and build VMM
   git clone <your-repo>
   cd vibe-vmm
   make

   # Run with test kernel
   ./bin/vibevmm --binary tests/kernels/x86_64_simple.bin \
                   --entry 0x1000 \
                   --mem 128M \
                   --log 4
   ```

### Option 2: Intel Mac

If you have access to an Intel Mac:
- The VMM will work natively with HVF backend
- No additional setup required

### Option 3: Native Linux Machine

Any x86_64 Linux machine with KVM support:
```bash
# Verify KVM is available
ls /dev/kvm

# Run VMM
./bin/vibevmm --binary tests/kernels/x86_64_simple.bin \
                --entry 0x1000 \
                --mem 128M
```

---

## Building the Test Kernels

### Prerequisites
```bash
# macOS
brew install nasm

# Linux
sudo apt install nasm  # Debian/Ubuntu
sudo yum install nasm  # Fedora/RHEL
```

### Build Commands
```bash
cd tests/kernels

# Build x86_64 kernel (works with VMM)
make x86_64

# Build ARM64 kernel (reference only)
make arm64

# Clean
make clean
```

---

## Expected Output

When running the x86_64 test kernel with proper hypervisor backend:

```
[INFO] Initializing hypervisor...
[INFO] Created VM
[INFO] Allocated guest memory: 128 MB
[INFO] Created vCPU 0
[INFO] Setting up x86_64 boot environment...
[INFO) vCPU 0: Running vCPU...
[DEBUG] vCPU 0: VM exit: HLT
[INFO] vCPU 0: HLT - pausing vCPU
```

On Apple Silicon (current attempt):
```
[ERROR] HVF x86_64 support requires an Intel-based Mac
[ERROR] Apple Silicon (M1/M2/M3) does not support x86_64 virtualization
[ERROR] For testing on Apple Silicon, use KVM backend in a Linux VM
```

---

## Technical Details

### x86_64 Kernel Layout
```
Entry Point: 0x1000
Code Segment: 0x08 (flat model)
Data Segment: 0x10 (flat model)
Stack:       0x2000 (grows downward)
```

### What the Test Kernel Does
1. Disables interrupts (`cli`)
2. Sets up stack pointer
3. Enters infinite loop:
   - Executes `hlt` (causes VM exit)
   - Jumps back to start
   - Repeats

This is perfect for testing:
- VM creation and initialization
- vCPU creation and execution
- VM exit handling (HLT)
- vCPU register state

---

## Advanced Testing

For more comprehensive testing, you would need:

1. **Real Linux Kernel**
   ```bash
   wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz
   # Extract and build...
   ```

2. **Boot Parameters**
   ```bash
   ./bin/vibevmm \
     --kernel bzImage \
     --initrd initrd.img \
     --cmdline "console=hvc0 earlyprintk=serial panic=1" \
     --mem 512M \
     --cpus 2
   ```

3. **With Devices**
   ```bash
   ./bin/vibevmm \
     --kernel bzImage \
     --disk rootfs.ext4 \
     --console
   ```

---

## Summary

| Platform | VMM Works? | Backend | Notes |
|----------|-------------|---------|-------|
| Linux x86_64 | ✅ Yes | KVM | Primary target platform |
| Intel Mac (x86_64) | ✅ Yes | HVF | Uses Hypervisor.framework |
| Apple Silicon (ARM64) | ❌ No | N/A | Would require complete rewrite for ARM64 |

**Bottom Line**: You cannot run x86_64 VMs on Apple Silicon natively. Use a Linux VM to test the VMM.
