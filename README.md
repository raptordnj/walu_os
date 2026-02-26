# WaluOS (C + Rust)

WaluOS is a minimal hobby OS kernel for x86_64.

## MVP Features
- Multiboot2 boot via GRUB
- GRUB animated boot splash with branding text: `Walu OS` and `A dreaming OS from Bangladesh.`
- Long mode transition in boot assembly
- VGA text console boot logs with optional framebuffer text backend (when available)
- ANSI CSI parser (colors, cursor motion, clear controls) on console path
- Physical memory manager (frame bitmap)
- Virtual memory manager (2 MiB paging mapper)
- IDT setup with exception handling
- PIC remap + PIT timer interrupt
- Keyboard IRQ key-event queue + UTF-8 byte queue
- Extended key handling (arrows/home/end/insert/delete/page keys, F1-F12 to ANSI escapes)
- Modifier/lock tracking (Shift/Ctrl/Alt/AltGr/Meta, Caps/Num/Scroll lock)
- Unicode compose input (`Ctrl+Shift+U` + hex + Enter/Space) for arbitrary code points
- Runtime keyboard controls (`kbdctl`) for layout (`us`, `us-intl`) and repeat tuning
- Key-event inspection (`showkey`, optional live mode) with buffered recent events
- TTY line discipline (canonical mode + echo + safe input filtering)
- PTY channel skeleton (master/slave ring buffers)
- Subsystem fault counters for keyboard/TTY/PTY overflow and invalid operations
- Tiny shell commands: `help`, `clear`, `pwd`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `write`, `append`, `nano`, `reboot`, `reset`, `poweroff`, `shutdown`, `ui`, `meminfo`, `kbdinfo`, `kbdctl`, `showkey`, `format`, `install`, `storaged`, `ttyinfo`, `health`, `ansi`, `echo`
- Shell file UX polish: `ls -a/-l`, `mkdir -p`, `cd ~`, `cd -`, and clearer path-aware errors
- Nano-like in-kernel editor: `nano <path>` with arrows, `Ctrl+O` save, `Ctrl+X` exit
- TUI comfort upgrades: path-aware color prompt, typo suggestions, grouped help, and `ui compact|comfortable` font-scale controls
- Shell control input support (`Ctrl-C`, `Ctrl-L`) via TTY pipeline
- Rust `#![no_std]` static library linked into the C kernel
- Architecture blueprint and implementation roadmap in `docs/`
- Userland service scaffolding in `userland/` (`walud`, `authd`, `storaged`)
- `storaged` now includes Linux-like disk operations (`lsblk`, `blkid`, `mount`, `umount`, `fsck`, `format`) with strict safety gates
- Kernel `storaged install` can seed a Unix-like root tree (`/bin`, `/etc`, `/usr`, `/var`, `/home`, `/dev`, `/proc`, `/sys`, `/run`) and base account files
- Host installer `scripts/install_storage.sh` can create a real UEFI-bootable GPT disk/image (ESP + ext4 root)

## Build locally (toolchain on host)
```bash
make iso
```

## Install and Boot Guide
- See [docs/INSTALL_GUIDE.md](docs/INSTALL_GUIDE.md) for ISO boot, UEFI storage install, and troubleshooting.

## Build userland scaffolding (host binaries)
```bash
make userland
```

## Run userland safety tests
```bash
make test-userland
```

## Kernel host checks
```bash
make test-kernel
```
Includes TTY/PTY, keyboard mapping/Unicode compose, and storage policy host tests.

## Full local validation pipeline
```bash
make test
```

## Boot smoke test (QEMU + ISO)
Requires host tools: `grub-mkrescue`, `xorriso`, `mtools` (`mformat`), `qemu-system-x86_64`, `timeout`, and UEFI firmware files from `ovmf`.
```bash
make boot-smoke
```

## Prepare toolchain wrappers
Creates `toolchain/bin/x86_64-elf-*` wrappers when cross tools are not installed.
```bash
make toolchain-bootstrap
```

## Package artifacts
Packages kernel ISO and userland binaries into `dist/waluos-artifacts.tar.gz` plus SHA-256.
```bash
make package-artifacts
```

## CI-equivalent local run
```bash
make ci
```

## Run locally in QEMU
```bash
make run
# or headless
make run-headless
```

## Install Unix-like base tree to a device (inside WaluOS shell)
Interactive wizards (recommended):
```text
format
install
```

Scripted one-shot installer command:
```text
install --device /dev/usb0 --target /media/usb0 --dry-run
install --device /dev/usb0 --target /media/usb0 --fstype ext4 --label WALUROOT --force --confirm /dev/usb0 --yes
```

Equivalent low-level steps:
```text
storaged format --device /dev/usb0 --fstype ext4 --force --confirm /dev/usb0 --yes
storaged install --device /dev/usb0 --target /media/usb0 --force --confirm /dev/usb0 --yes
```
Safety:
- Destructive actions still require `--force --confirm <exact-device> --yes`.
- Use `--dry-run` first to validate command intent.

Note:
- The in-kernel `storaged install` seeds a filesystem tree but does not partition media or place a UEFI bootloader.
- For a bootable install on real media/image, use the host installer below.

## Create a UEFI-bootable disk install (host side)
Dry run first:
```bash
make install-image IMAGE=build/waluos-disk.img SIZE_MIB=2048 DRY_RUN=1 FORCE=1 CONFIRM=build/waluos-disk.img YES=1
```

Create bootable raw image:
```bash
sudo make install-image IMAGE=build/waluos-disk.img SIZE_MIB=2048 FORCE=1 CONFIRM=build/waluos-disk.img YES=1
```

Install directly to USB drive (dangerous, wipes target):
```bash
sudo make install-storage DEVICE=/dev/sdb FORCE=1 CONFIRM=/dev/sdb YES=1
```

What it does:
- Creates GPT with `ESP(FAT32)` + `ROOT(ext4)`
- Seeds Unix-like root (`/bin`, `/etc`, `/usr`, `/var`, `/home`, etc.)
- Places kernel at `/boot/kernel.elf`
- Writes `EFI/BOOT/BOOTX64.EFI` via `grub-mkstandalone` (removable media path)
- Runs host open-source storage driver preflight (`modprobe` best effort for SATA/USB/NVMe/virtio)

Disable host driver preflight if needed:
```bash
sudo make install-storage DEVICE=/dev/sdb FORCE=1 CONFIRM=/dev/sdb YES=1 SKIP_DRIVER_LOAD=1 DRY_RUN=1
sudo bash scripts/install_storage.sh --device /dev/sdb --force --confirm /dev/sdb --yes --skip-driver-load --dry-run
```

## Build with Docker
```bash
docker build -t waluos .
```

## Build ISO with Docker
```bash
docker run --rm -v "$PWD":/workspace waluos make iso
```

## Run in QEMU with Docker
```bash
docker run --rm -it -v "$PWD":/workspace waluos make run-headless
```

## Run in VirtualBox
1. Build ISO: `make iso` or `docker run --rm -v "$PWD":/workspace waluos make iso`
2. Create VM in VirtualBox:
   - Type: `Other/Unknown (64-bit)`
   - Memory: `256 MB`+
   - Enable `EFI (special OSes only)`
3. Attach ISO: `build/waluos.iso` as optical disk
4. Boot VM

## Project Layout
- `kernel/` kernel C and boot assembly
- `rust/` Rust `no_std` staticlib
- `grub/` GRUB config
- `linker.ld` linker script
- `Makefile` build and run targets
- `scripts/` fault-tolerant validation, boot smoke, toolchain bootstrap, packaging helpers
- `.github/workflows/ci.yml` CI pipeline for host tests, boot smoke, and artifact upload
- `Dockerfile` reproducible build/run environment
- `docs/INSTALL_GUIDE.md` practical build/install/boot/troubleshooting guide
- `AGENTS.md` repository instructions for coding agents
- `agents.nd` compatibility pointer to `AGENTS.md`
- `docs/WALUOS_SYSTEM_BLUEPRINT.md` full architecture and implementation plan
- `docs/examples/` example system config files from the blueprint
- `docs/interfaces/` syscall/procfs/input/tty interface sketches
- `skills/` local reusable skill workflows (`skills/*/SKILL.md`)
- `userland/` host-buildable service scaffolding binaries

## Project Policies
- Contribution guide: `CONTRIBUTING.md`
- Security policy: `SECURITY.md`
- Ownership rules: `.github/CODEOWNERS`
