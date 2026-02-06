# ARM64 Test Kernels for Vibe-VMM

This directory contains ARM64 test kernels for demonstrating VMM functionality on Apple Silicon.

## Available Kernels

### arm64_simple.raw
- **Purpose**: Minimal test kernel
- **Size**: 8 bytes
- **Behavior**: Infinite loop of WFI (Wait For Interrupt) instructions
- **Usage**: Basic VM execution test

### arm64_test.raw
- **Purpose**: Computation and loop test
- **Size**: 68 bytes
- **Behavior**:
  - Loads value (42), performs arithmetic (add 10, multiply by 2)
  - Loops 5 times with WFI instructions
  - Demonstrates VM exits and basic execution
- **Usage**: Testing computation and controlled exits

### arm64_hello.raw
- **Purpose**: Long-running timing test
- **Size**: 164 bytes
- **Behavior**:
  - Continuously increments counter (acts as timer)
  - Updates message count every 10,000,000 iterations
  - Uses WFI to yield control back to host
  - Sets memory markers for debugging:
    - `0xDEADBEEF`: Kernel initialized
    - `0xCAFEBABE`: In main loop
    - `0xF00DF00D`: Message counter updated
- **Usage**: Testing VMM stability under sustained load
- **Note**: Demonstrates ~1 billion VM exits in 8 seconds on Apple Silicon

## Building Kernels

All kernels can be built using the provided build script:

```bash
cd tests/kernels
./build.sh
```

Or build individually:

```bash
# Assemble ARM64 kernel
clang -arch arm64 -c arm64_hello.S -o arm64_hello.o

# Extract raw binary (requires otool and Python)
python3 extract_binary.py arm64_hello.o arm64_hello.raw
```

## Running the Kernels

```bash
# From project root
./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M

# Or directly with signed binary
codesign --entitlements entitlements.plist --force --deep -s - bin/vibevmm
./bin/vibevmm --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M
```

## Memory Layout

Kernels are loaded at guest physical address `0x10000` by default.

### Known Memory Regions:
- `0x0 - 0x7FFFFFF`: Guest RAM (128 MB default)
- `0x9000000`: MMIO debug console
- `0xa000000`: VirtIO console device

## Exit Statistics

When running ARM64 kernels, you'll see statistics like:

```
╔══════════════════════════════════════════════════════════════════╗
║  vCPU 0 Statistics                                                 ║
╠══════════════════════════════════════════════════════════════════╣
║  Exit Statistics:                                                   ║
║    Total VM Exits:                948925536                             ║
║    HLT Exits:                     948925536                             ║
╚══════════════════════════════════════════════════════════════════╝
```

The high exit count demonstrates efficient VM exit handling and the WFI instruction's role in yielding control back to the hypervisor.

## Future Enhancements

- Implement proper MMIO exit handling to allow console output
- Add ARM64 system register access support
- Implement virtual timer interrupt injection
- Add more complex test scenarios (syscalls, page faults, etc.)
