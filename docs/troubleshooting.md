# Troubleshooting

Common setup and runtime issues for Vibe-VMM.

## KVM Not Available (Linux)

- **Symptom**: `/dev/kvm` is missing or you see permission errors.
- **Fix**: Enable virtualization in BIOS/UEFI.
- **Fix**: Ensure KVM modules are loaded: `lsmod | grep kvm`.
- **Fix**: Run as root or add your user to the `kvm` group.

## HVF Entitlements / Codesign (macOS)

- **Symptom**: Codesign fails or VM creation fails on macOS.
- **Fix**: Run `./run.sh` to codesign with Hypervisor entitlements.
- **Fix**: If signing fails, run `sudo ./bin/vibevmm ...` as a fallback.

## MMIO to Unmapped Address

- **Symptom**: "MMIO to unmapped address" errors.
- **Fix**: Ensure the MMIO console device is enabled and registered.
- **Fix**: Verify the guest uses the MMIO base address `0x90000000`.
- **Fix**: Check logs and `vmm_console.log` for device init messages.

## TAP Interface Errors (virtio-net)

- **Symptom**: "Cannot open /dev/net/tun" or "tap: Permission denied".
- **Fix**: Create and bring up a TAP interface.
- **Fix**: Example:

```bash
sudo ip tuntap add dev tap0 mode tap
sudo ip link set tap0 up
```

- **Fix**: Run VMM with `--net tap=tap0` and root privileges.

## VFIO Device Errors (Linux)

- **Symptom**: "VFIO: failed to open device" or "IOMMU not enabled".
- **Fix**: Enable IOMMU in your kernel boot params.
- **Fix**: Unbind the device from the host driver and bind it to `vfio-pci`.
- **Fix**: Verify the BDF with `lspci -nn` and pass it to `--vfio`.

## Guest Hangs or No Output

- **Symptom**: Guest boots but hangs or produces no console output.
- **Fix**: Verify the entry point and kernel format.
- **Fix**: Increase logging with `--log 4` to see VM exits.
