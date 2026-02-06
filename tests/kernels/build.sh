#!/bin/bash
# Build test kernels for Vibe-VMM
# Supports ARM64 (Apple Silicon)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

ARCH=$(uname -m)

echo "Building test kernels..."
echo "Architecture: $ARCH"
echo ""

# Build ARM64 kernel (if on ARM64)
if [ "$ARCH" = "arm64" ]; then
    if command -v clang &> /dev/null; then
        echo "Building ARM64 kernels..."

        # Build arm64_hello.raw (prints "Hello World!" repeatedly)
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

        # Build arm64_minimal.raw (minimal MMIO test)
        if [ -f "arm64_minimal.S" ]; then
            clang -arch arm64 -c arm64_minimal.S -o arm64_minimal.o 2>/dev/null
            python3 << 'PYTHON'
import subprocess
import re

# Get the text section
result = subprocess.run(['otool', '-t', 'arm64_minimal.o'],
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
with open('arm64_minimal.raw', 'wb') as f:
    f.write(bytes(hex_data))
print(f"  ✓ arm64_minimal.raw ({len(hex_data)} bytes)")
PYTHON
            rm -f arm64_minimal.o
        else
            echo "  ⊘ arm64_minimal.S not found"
        fi
    else
        echo "  ⊘ clang not found, skipping ARM64 kernel"
    fi
else
    echo "  ⊘ This build script is for ARM64 (Apple Silicon) only"
    echo "    Your architecture: $ARCH"
fi

echo ""
echo "Done! Available kernels:"
ls -lh *.raw 2>/dev/null | awk '{printf "  • %s (%s)\n", $9, $5}'
echo ""
echo "Usage:"
echo "  ARM64:   ./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M"
echo "  ARM64:   ./run.sh --binary tests/kernels/arm64_minimal.raw --entry 0x10000 --mem 128M"
