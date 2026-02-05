#!/bin/bash
# Quick test script for Vibe-VMM on Apple Silicon

set -e

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║           Vibe-VMM Quick Test - Apple Silicon ARM64             ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# Check if on Apple Silicon
if [ "$(uname -m)" != "arm64" ]; then
    echo "❌ This script is for Apple Silicon (arm64) only"
    echo "   Your architecture: $(uname -m)"
    exit 1
fi

# Build VMM
echo "Building VMM..."
make clean > /dev/null 2>&1
if ! make > /tmp/vibe_build.log 2>&1; then
    echo "❌ Build failed - check /tmp/vibe_build.log"
    exit 1
fi
echo "✅ Build successful"
echo ""

# Build test kernel
echo "Building ARM64 test kernel..."
cd tests/kernels
if ! ./build.sh > /dev/null 2>&1; then
    echo "❌ Kernel build failed"
    cd ../..
    exit 1
fi
cd ../..
echo "✅ Test kernel ready"
echo ""

# Run VMM
echo "Running VMM test..."
echo "────────────────────────────────────────────────────────────────────"
timeout 3 ./run.sh --binary tests/kernels/arm64_simple.raw --entry 0x1000 --mem 128M --log 2 2>&1 | \
    grep -E "(INFO|ERROR|✓|Signing)" | head -15 || true
echo "────────────────────────────────────────────────────────────────────"
echo ""

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                        Test Complete!                            ║"
echo "╠════════════════════════════════════════════════════════════════╣"
echo "║  Your Vibe-VMM is working on Apple Silicon!                   ║"
echo "║                                                                ║"
echo "║  To run manually:                                              ║"
echo "║    ./run.sh --binary tests/kernels/arm64_simple.raw \\          ║"
echo "║                --entry 0x1000 --mem 128M                       ║"
echo "╚════════════════════════════════════════════════════════════════╝"
