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
        echo "Building ARM64 kernels..."

        # Build arm64_hello.raw (timing test)
        if [ -f "arm64_hello.S" ]; then
            clang -arch arm64 -c arm64_hello.S -o arm64_hello.o 2>/dev/null
            python3 << 'PYTHON'
import subprocess
import re

# Get the text section
result = subprocess.run(['otool', '-t', 'arm64_hello.o'],
                       capture_output=True, text=True)

# Parse and extract
hex_data = []
for line in result.stdout.split('\n'):
    if '(' in line or not line.strip():
        continue
    parts = line.split()
    if len(parts) > 1 and ':' not in parts[0]:
        for part in parts[1:]:
            if re.match(r'^[0-9a-fA-F]{8}$', part):
                val = int(part, 16)
                hex_data.extend([
                    val & 0xff,
                    (val >> 8) & 0xff,
                    (val >> 16) & 0xff,
                    (val >> 24) & 0xff
                ])

# Trim trailing zeros
while len(hex_data) > 0 and hex_data[-1] == 0:
    hex_data.pop()

# Write raw binary
with open('arm64_hello.raw', 'wb') as f:
    f.write(bytes(hex_data))
print(f"  ✓ arm64_hello.raw ({len(hex_data)} bytes)")
PYTHON
            rm -f arm64_hello.o
        else
            echo "  ⊘ arm64_hello.S not found"
        fi

        # Build arm64_simple.raw (minimal test)
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
echo "  ARM64:   ./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M"
