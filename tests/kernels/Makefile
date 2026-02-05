# Makefile for test kernels

.PHONY: all clean help arm64 x86_64

# Default target
all: x86_64

help:
	@echo "Vibe-VMM Test Kernels"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build x86_64 kernel (default)"
	@echo "  x86_64   - Build x86_64 test kernel"
	@echo "  arm64    - Build ARM64 test kernel (reference only)"
	@echo "  clean    - Remove built kernels"
	@echo "  help     - Show this message"
	@echo ""
	@echo "Important Notes:"
	@echo "  - x86_64 kernel works with VMM on Linux (KVM) or Intel Mac (HVF)"
	@echo "  - ARM64 kernel is REFERENCE ONLY - VMM doesn't support ARM64 VMs"
	@echo "  - Apple Silicon Macs cannot run x86_64 VMs natively"
	@echo ""
	@echo "To test on Apple Silicon:"
	@echo "  1. Install Linux in a VM (UTM, Parallels, VMware Fusion)"
	@echo "  2. Build and run VMM inside Linux VM"
	@echo "  3. Use: ./bin/vibevmm --binary tests/kernels/x86_64_simple.bin --entry 0x1000"

# Build x86_64 kernel (works with VMM)
x86_64: x86_64_simple.bin

x86_64_simple.bin: x86_64_simple.S
	@echo "Building x86_64 test kernel..."
	@which nasm >/dev/null 2>&1 || (echo "Error: nasm not installed. Install with: brew install nasm" && exit 1)
	nasm -f bin $< -o $@
	@echo "Built: $@"
	@echo "To test: ./bin/vibevmm --binary $@ --entry 0x1000 --mem 128M --log 4"

# Build ARM64 kernel (REFERENCE ONLY)
arm64: arm64_simple.bin

arm64_simple.bin: arm64_simple.S
	@echo "Building ARM64 test kernel (REFERENCE ONLY)..."
	@which clang >/dev/null 2>&1 || (echo "Error: clang not installed" && exit 1)
	clang -o $@.o $< -c
	ld -o $@ $@.o -e _start -static -pagezero_size 0x1000 -segaddr __TEXT 0x40000000
	@echo "Built: $@ (NOTE: VMM does not support ARM64 VMs!)"

# Clean
clean:
	rm -f *.bin *.o

# List targets
.PHONY: list
list:
	@echo "Available test kernels:"
	@ls -1 *.S 2>/dev/null | sed 's/.S//' || echo "None"
