# ARM64 Test Kernels for Vibe-VMM

This directory contains ARM64 test kernels for demonstrating VMM functionality on Apple Silicon.

**Note**: This repo does not include x86_64 test binaries. On Linux or macOS Intel, provide your own kernel/initrd or build a minimal test kernel.

## Available Kernels

### arm64_hello.raw ⭐ **Recommended**
- **Purpose**: Repeatedly prints "Hello World!" with delays
- **Size**: 172 bytes
- **Behavior**:
  - Continuously prints "Hello World!" to MMIO console
  - Uses busy-wait delay loop (~5 seconds between prints)
  - Never stops - runs indefinitely
- **Usage**: Testing MMIO console output and VMM stability
- **Example**:
  ```bash
  ./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M
  ```

### arm64_minimal.raw
- **Purpose**: Minimal MMIO test
- **Size**: 32 bytes
- **Behavior**:
  - Prints "Hi" once to MMIO console
  - Enters WFI (Wait For Interrupt) loop
  - Demonstrates basic MMIO write handling
- **Usage**: Quick MMIO functionality test
- **Example**:
  ```bash
  ./run.sh --binary tests/kernels/arm64_minimal.raw --entry 0x10000 --mem 128M
  ```

## Technical Details

### Position-Independent Code
All kernels use position-independent code (PIC) with `movz`/`movk` instructions:
- **No literal pools** - works with raw binary loading at any address
- **Direct address loading** - MMIO base address (0x90000000) loaded via immediate moves
- **Simple relocation** - can be loaded at any 4KB-aligned address

### MMIO Console Interface
- **Base Address**: 0x90000000 (GPA)
- **Register**: UART_TX at offset 0x00
- **Operation**: Write byte to UART_TX to output character
- **Output**: Appears on stdout and in `vmm_console.log`

## Building Kernels

Kernels can be assembled and linked manually:

```bash
# Assemble the kernel
clang -arch arm64 -c arm64_hello.S -o arm64_hello.o

# Disassemble to get instructions
otool -t arm64_hello.o

# Extract raw binary (requires Python script or manual conversion)
# See build.sh for the complete build process
```

## Running the Kernels

```bash
# Run the Hello World kernel (repeated output)
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M

# Run the minimal kernel (single "Hi")
./run.sh --binary tests/kernels/arm64_minimal.raw --entry 0x10000 --mem 128M

# With console output explicitly (now auto-enabled)
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M --console
```

**Apple Silicon only**: `tests/kernels/build.sh` and `./quicktest.sh` are ARM64-only.

## Memory Layout

- **RAM**: 0x00000000 - 0x07FFFFFF (128 MB)
- **MMIO Console**: 0x90000000 - 0x90000FFF (4 KB)
- **Virtio Console**: 0x00A00000 - 0x00A00FFF (4 KB)

Kernels can be loaded at any 4KB-aligned address within RAM (e.g., 0x1000, 0x10000).

## Output

MMIO console output appears in two places:
1. **stdout** - Your terminal
2. **vmm_console.log** - Log file in current directory

## Debugging

If you see "MMIO to unmapped address" errors:
1. Check that the MMIO console device is created (should see "Created MMIO console" in logs)
2. Verify kernel is using correct MMIO base address (0x90000000)
3. Ensure kernel uses position-independent code (no literal pools)
