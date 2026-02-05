#!/bin/bash
# Build test kernels for Vibe-VMM
# Supports both x86_64 (Linux/Intel Mac) and ARM64 (Apple Silicon)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

ARCH=$(uname -m)

echo "Building test kernels..."
echo "Architecture: $ARCH"
echo ""

# Build x86_64 kernel (if nasm available)
if command -v nasm &> /dev/null; then
    echo "Building x86_64 kernel..."
    nasm -f bin x86_64_simple.S -o x86_64_simple.bin
    echo "  ✓ x86_64_simple.bin ($(stat -f%z x86_64_simple.bin) bytes)"
else
    echo "  ⊘ nasm not found, skipping x86_64 kernel"
fi

# Build ARM64 kernel (if on ARM64)
if [ "$ARCH" = "arm64" ]; then
    if command -v clang &> /dev/null; then
        echo "Building ARM64 kernel..."
        
        # Assemble
        clang -arch arm64 -c arm64_simple.raw.S -o .arm64_temp.o 2>/dev/null
        
        # Create raw binary (8 bytes: WFI + branch to self)
        python3 << 'PYTHON'
import struct
# WFI instruction: 0xD5037FFF
# Branch to self: 0xFFFFFFF7 (B #-4)
code = struct.pack('<I', 0xD5037FFF)
code += struct.pack('<I', 0xFFFFFFF7)
with open('arm64_simple.raw', 'wb') as f:
    f.write(code)
print("  ✓ arm64_simple.raw (8 bytes)")
PYTHON
        
        rm -f .arm64_temp.o
    else
        echo "  ⊘ clang not found, skipping ARM64 kernel"
    fi
fi

echo ""
echo "Done! Available kernels:"
ls -lh *.bin *.raw 2>/dev/null | awk '{printf "  • %s (%s)\n", $9, $5}'
echo ""
echo "Usage:"
echo "  x86_64:  ./bin/vibevmm --binary tests/kernels/x86_64_simple.bin --entry 0x1000 --mem 128M"
echo "  ARM64:   ./run.sh --binary tests/kernels/arm64_simple.raw --entry 0x1000 --mem 128M"
