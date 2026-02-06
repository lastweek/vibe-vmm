#!/bin/bash
# run.sh - Sign and run Vibe-VMM with proper entitlements on Apple Silicon

# Don't exit on error - we need to handle signals
# set -e

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "════════════════════════════════════════════════════════════════"
echo "  Vibe-VMM Runner for Apple Silicon"
echo "════════════════════════════════════════════════════════════════"
echo ""

# Check if entitlements.plist exists
if [ ! -f "entitlements.plist" ]; then
    echo "Error: entitlements.plist not found"
    exit 1
fi

# Sign the binary with entitlements
echo "Signing vibevmm with hypervisor entitlements..."
codesign --entitlements entitlements.plist --force --deep -s - bin/vibevmm

if [ $? -eq 0 ]; then
    echo "✓ Signing successful"
else
    echo "✗ Signing failed"
    echo ""
    echo "You may need to run with sudo instead:"
    echo "  sudo ./bin/vibevmm \"$@\""
    exit 1
fi

echo ""
echo "Running Vibe-VMM..."
echo ""

# Run the VMM with provided arguments
# Use exec to replace shell process, ensuring proper signal delivery
exec ./bin/vibevmm "$@"
