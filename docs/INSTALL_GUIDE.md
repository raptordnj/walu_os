# WaluOS Install and Boot Guide

This guide covers:
- Booting from ISO (VirtualBox/QEMU)
- Installing to a bootable disk image or USB drive (UEFI)
- Common boot/display troubleshooting

## 1) Host Requirements

Install required host tools:
- `grub-mkrescue`
- `grub-mkstandalone`
- `xorriso`
- `mtools` (for `mformat`)
- `qemu-system-x86_64`
- `parted`
- `mkfs.ext4`
- `mkfs.vfat` (or `mkfs.fat`)
- `losetup`, `mount`, `umount`, `blkid`
- `ovmf` (UEFI firmware for QEMU tests)

Prepare wrappers/toolchain path:

```bash
make toolchain-bootstrap
export PATH="$PWD/toolchain/bin:$PATH"
```

## 2) Build and Boot ISO

Build ISO:

```bash
make iso
```

Run in QEMU:

```bash
make run
# or
make run-headless
```

Boot smoke test (UEFI):

```bash
make boot-smoke
```

## 3) VirtualBox UEFI Setup

Create VM with:
- Type: `Other/Unknown (64-bit)`
- Memory: `256 MB` or higher
- `System -> Enable EFI` = ON
- Attach `build/waluos.iso` as optical disk

Recommended display settings:
- Graphics Controller: `VMSVGA`
- Video Memory: `64 MB` or higher
- 3D Acceleration: OFF (for stability)

## 4) Install to Bootable Storage (Host Installer)

Important:
- In-kernel `storaged install` seeds files only.
- Real bootable media install is done from host with `scripts/install_storage.sh`.

### 4.1 Dry run (always first)

```bash
make install-image \
  IMAGE=build/waluos-disk.img \
  SIZE_MIB=2048 \
  DRY_RUN=1 FORCE=1 CONFIRM=build/waluos-disk.img YES=1
```

### 4.2 Create bootable raw image (UEFI GPT)

```bash
sudo make install-image \
  IMAGE=build/waluos-disk.img \
  SIZE_MIB=2048 \
  FORCE=1 CONFIRM=build/waluos-disk.img YES=1
```

### 4.3 Install directly to USB device (destructive)

Double-check the target device first:

```bash
lsblk -o NAME,SIZE,MODEL,TYPE,MOUNTPOINT
```

Install:

```bash
sudo make install-storage \
  DEVICE=/dev/sdX \
  FORCE=1 CONFIRM=/dev/sdX YES=1
```

The installer will:
- Wipe target and create GPT
- Create `ESP` (FAT32) + `ROOT` (ext4)
- Copy kernel to `/boot/kernel.elf`
- Install UEFI loader at `EFI/BOOT/BOOTX64.EFI`
- Seed Unix-like base tree (`/bin`, `/etc`, `/usr`, `/var`, `/home`, etc.)
- Preload host open-source storage drivers (best effort): `ahci`, `usb_storage`, `uas`, `nvme`, `virtio_blk`, etc.

Skip driver preload if needed:

```bash
sudo make install-storage \
  DEVICE=/dev/sdX \
  FORCE=1 CONFIRM=/dev/sdX YES=1 \
  SKIP_DRIVER_LOAD=1
```

## 5) Boot Installed Disk Image in QEMU (UEFI)

```bash
qemu-system-x86_64 \
  -machine q35 \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=/usr/share/OVMF/OVMF_VARS_4M.fd \
  -drive format=raw,file=build/waluos-disk.img
```

If your distro uses different OVMF filenames, adjust the two pflash file paths.

## 6) In-Kernel Storage Commands (Inside WaluOS Shell)

Interactive wizards:

```text
format
install
```

Scripted one-shot install command:

```text
install --device /dev/usb0 --target /media/usb0 --dry-run
install --device /dev/usb0 --target /media/usb0 --fstype ext4 --label WALUROOT --force --confirm /dev/usb0 --yes
```

Low-level `storaged` equivalents:

List devices:

```text
storaged lsblk
```

Probe one device:

```text
storaged probe --device /dev/usb0
```

Format and mount (simulated kernel storage layer):

```text
storaged format --device /dev/usb0 --fstype ext4 --force --confirm /dev/usb0 --yes
storaged mount --device /dev/usb0 --target /media/usb0 --read-write --trusted --force
```

## 7) Troubleshooting

### A) `No bootable medium found`
- ISO is not attached, or VM boots wrong disk first.
- Fix:
  1. Attach `build/waluos.iso` as optical drive.
  2. Enable EFI in VM settings.
  3. Put Optical first in boot order.

### B) `BdsDxe: failed to start Boot0001 ...` / `No bootable option`
- UEFI firmware cannot find valid EFI loader.
- Fix:
  1. Rebuild ISO: `make iso`
  2. For disk install, re-run host installer with exact confirmation flags.
  3. Confirm `EFI/BOOT/BOOTX64.EFI` exists on ESP.

### C) Black blank screen after boot
- Usually display controller/resolution issue in VirtualBox.
- Fix:
  1. Set Graphics Controller to `VMSVGA`.
  2. Disable 3D acceleration.
  3. Increase video memory.
  4. Try headless/serial boot in QEMU to verify kernel still runs.

### D) `storaged/storaged unavailable in kernel-only ISO`
- Userland daemon is not part of kernel-only ISO path.
- Use shell builtin command: `storaged ...`.

### E) Installer refuses to run
- Missing force confirmation or root privileges.
- Required pattern:
  - `--force --confirm <exact target> --yes`
  - Run with `sudo` for real installs.

## 8) Safety Notes

- Never run install on uncertain device names.
- Always run `DRY_RUN=1` first.
- Never target current root disk.
- The installer intentionally blocks unsafe destructive defaults.
