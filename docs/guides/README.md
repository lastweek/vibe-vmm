# User Guides

Step-by-step guides for using and developing with Vibe-VMM.

## Getting Started

### [Installation](getting-started.md)
- Platform requirements
- Building from source
- Running your first VM

### [Platform Setup](platform-setup.md)
- Linux setup (KVM)
- macOS setup (HVF)
- Permissions and entitlements

### [Building Test Kernels](building-kernels.md)
- ARM64 assembly kernels
- Position-independent code
- MMIO device access

## Development

### [Debugging](debugging.md)
- Common issues and solutions
- Debug logging
- VM exit analysis

### [Adding Devices](adding-devices.md)
- Device framework
- MMIO device implementation
- Virtio device development

### [Porting to New Platforms](porting.md)
- Hypervisor abstraction
- Platform detection
- Backend implementation

## Testing

### Quick Test
```bash
./quicktest.sh
```

### Manual Test (ARM64)
```bash
./run.sh --binary tests/kernels/arm64_hello.raw \
         --entry 0x10000 --mem 128M
```

### Manual Test (x86_64)
```bash
./bin/vibevmm --binary tests/kernels/x86_64_simple.bin \
               --entry 0x1000 --mem 128M
```

## Common Workflows

### Running a Custom Kernel

1. Build your kernel (raw binary)
2. Load at appropriate address
3. Set entry point
4. Run VMM

```bash
./run.sh --binary mykernel.raw --entry 0x10000 --mem 256M
```

### Enabling Debug Output

```bash
./run.sh --binary kernel.raw --entry 0x10000 \
         --mem 128M --log 4
```

Log levels:
- `0` = none
- `1` = error
- `2` = warn
- `3` = info
- `4` = debug

### Adding MMIO Devices

Edit device GPA in kernel source:
```assembly
.equ MMIO_DEVICE_BASE, 0x90000000  // Match device address
```

Then register device in VMM or enable via CLI.

## Troubleshooting

### Common Issues

**"Instruction Abort at NULL"**
- Cause: Position-dependent code with literal pools
- Fix: Use position-independent code (movz/movk)

**"MMIO to unmapped address"**
- Cause: Device not registered or wrong GPA
- Fix: Enable device or correct base address

**Guest hangs on boot**
- Cause: Wrong entry point or kernel format
- Fix: Verify entry point matches load address

See [Debugging Guide](debugging.md) for more details.

## Performance Tips

1. **Use virtio devices** instead of MMIO for high-bandwidth I/O
2. **Pre-fault memory pages** before VM execution
3. **Minimize VM exits** in hot paths
4. **Batch operations** when possible

## Advanced Usage

### Custom Memory Layout
```bash
# Add multiple memory regions
./vibevmm --mem-low 128M --mem-high 1G ...
```

### Device Passthrough (Linux)
```bash
# Use VFIO for device passthrough
./vibevmm --device vfio,passthrough=0000:01:00.0 ...
```

### Network Configuration
```bash
# Use TAP interface for networking
./vibevmm --net tap=virbr0 ...
```

## Resources

- [Main README](../../README.md)
- [Architecture Overview](../architecture/overview.md)
- [API Reference](../api/)
- [GitHub Issues](https://github.com/lastweek/vibe-vmm/issues)

## Getting Help

1. Check [Troubleshooting Guide](debugging.md)
2. Review [Architecture Documentation](../architecture/overview.md)
3. Search [GitHub Issues](https://github.com/lastweek/vibe-vmm/issues)
4. Open a new issue with detailed logs

## Contributing

We welcome contributions! See the main README for guidelines.

Areas needing help:
- ARM64 GIC implementation
- Interrupt injection
- Virtio device enhancements
- Additional hypervisor backends
