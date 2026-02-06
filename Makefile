# Vibe-VMM Makefile
# A minimal Virtual Machine Monitor with multi-architecture support

# Detect platform and architecture
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -std=gnu99
LDFLAGS = -lpthread

# Platform-specific settings
ifeq ($(UNAME_S),Linux)
    # Linux - use KVM backend (x86_64 only)
    CFLAGS += -D_GNU_SOURCE
    LDFLAGS += -lrt
    HYPERVISOR_SRCS = src/hypervisor/kvm.c
    # Check architecture
    ifneq ($(UNAME_M),x86_64)
        $(warning Linux on non-x86_64 is not supported)
    endif
else ifeq ($(UNAME_S),Darwin)
    # macOS - use HVF, detect architecture
    # Detect architecture for macOS
    ARCH_DETECT := $(shell arch)
    ifeq ($(ARCH_DETECT),arm64)
        # Apple Silicon (ARM64) - use ARM64 HVF backend
        CFLAGS += -D__aarch64__
        HYPERVISOR_SRCS = src/hypervisor/hvf_arm64.c src/hypervisor/kvm_stub.c src/hypervisor/hvf_stub.c
        # Link against Hypervisor.framework
        LDFLAGS += -framework Hypervisor
    else ifeq ($(ARCH_DETECT),i386)
        # Intel Mac (x86_64) - use x86_64 HVF backend
        HYPERVISOR_SRCS = src/hypervisor/hvf.c
        # Link against Hypervisor.framework
        LDFLAGS += -framework Hypervisor
    else
        $(error Unsupported macOS architecture: $(ARCH_DETECT))
    endif
else
    $(error Unsupported platform: $(UNAME_S))
endif

# Optional: Always compile all backends for debugging
# Comment out the above HYPERVISOR_SRCS and enable this instead
# HYPERVISOR_SRCS = src/hypervisor/kvm.c src/hypervisor/hvf.c src/hypervisor/hvf_arm64.c

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin

# Source files
SRCS = $(wildcard $(SRCDIR)/*.c)
SRCS += $(HYPERVISOR_SRCS)

# Device source files
DEVICE_SRCS = $(wildcard $(SRCDIR)/devices/*.c)

# All source files
ALL_SRCS = $(SRCS) $(DEVICE_SRCS)

# Object files
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEVICE_OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(DEVICE_SRCS))
ALL_OBJS = $(OBJS) $(DEVICE_OBJS)

# Target binary
TARGET = $(BINDIR)/vibevmm

# Include paths
INCLUDES = -I$(INCDIR)

# Default target
all: dirs $(TARGET)

# Create directories
dirs:
	@mkdir -p $(OBJDIR)
	@mkdir -p $(OBJDIR)/hypervisor
	@mkdir -p $(OBJDIR)/devices
	@mkdir -p $(BINDIR)

# Link binary
$(TARGET): $(ALL_OBJS)
	@echo "Linking $@..."
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/hypervisor/%.o: $(SRCDIR)/hypervisor/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/devices/%.o: $(SRCDIR)/devices/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(OBJDIR) $(BINDIR)
	rm -f vmm_console.log

# Install (copy to /usr/local/bin)
install: $(TARGET)
	@echo "Installing vibevmm to /usr/local/bin..."
	install -m 755 $(TARGET) /usr/local/bin/vibevmm

# Uninstall
uninstall:
	@echo "Removing vibevmm from /usr/local/bin..."
	rm -f /usr/local/bin/vibevmm

# Run tests (requires root)
test: $(TARGET)
	@echo "Running VMM..."
	sudo $(TARGET) --help

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Release build
release: CFLAGS += -O3 -DNDEBUG
release: clean all

# Show help
help:
	@echo "Vibe-VMM Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build the VMM (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install to /usr/local/bin (requires root)"
	@echo "  uninstall- Remove from /usr/local/bin"
	@echo "  test     - Run help test"
	@echo "  debug    - Build with debug symbols and no optimization"
	@echo "  release  - Build optimized release binary"
	@echo "  help     - Show this help message"

.PHONY: all dirs clean install uninstall test debug release help
