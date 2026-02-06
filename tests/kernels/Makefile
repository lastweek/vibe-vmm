# Makefile for ARM64 test kernels

.PHONY: all clean help list

# Default target
all: list

help:
	@echo "Vibe-VMM ARM64 Test Kernels"
	@echo ""
	@echo "Available kernels:"
	@echo "  arm64_hello.raw    - Prints 'Hello World!' repeatedly (recommended)"
	@echo "  arm64_minimal.raw   - Prints 'Hi' once (minimal test)"
	@echo ""
	@echo "Building:"
	@echo "  Kernels are pre-built. Use the .S source files to modify."
	@echo "  See README.md for build instructions."
	@echo ""
	@echo "Running:"
	@echo "  ./run.sh --binary tests/kernels/arm64_hello.raw --entry 0x10000 --mem 128M"
	@echo "  ./run.sh --binary tests/kernels/arm64_minimal.raw --entry 0x10000 --mem 128M"

# List available kernels
list:
	@echo "Available ARM64 test kernels:"
	@ls -lh *.raw 2>/dev/null | awk '{printf "  • %s (%s)\n", $$9, $$5}'
	@echo ""
	@echo "Source files:"
	@ls -lh *.S 2>/dev/null | awk '{printf "  • %s (%s)\n", $$9, $$5}'

# Clean build artifacts
clean:
	rm -f *.o *.bin
	@echo "Cleaned build artifacts"
