#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

usage() {
  cat <<'EOF'
Usage:
  install_storage.sh (--device <path> | --image <path>) [options]

Creates a UEFI-bootable GPT install target with:
- Partition 1: FAT32 ESP
- Partition 2: ext4 root filesystem
- GRUB EFI loader at EFI/BOOT/BOOTX64.EFI
- Walu kernel at /boot/kernel.elf
- Seeded Unix-like base tree under root

Safety:
- Destructive run requires: --force --confirm <exact-target> --yes
- Use --dry-run first to inspect actions.

Options:
  --device <path>        Block device to wipe/install (example: /dev/sdb)
  --image <path>         Raw disk image path to create/use
  --size-mib <num>       Image size in MiB when --image is used (default: 2048)
  --esp-size-mib <num>   ESP partition size in MiB (default: 256)
  --kernel <path>        Kernel ELF path (default: build/kernel.elf)
  --grub-config <path>   Optional source GRUB cfg (default: grub/grub.cfg)
  --root-label <name>    Root ext4 label (default: WALU_ROOT)
  --efi-label <name>     ESP FAT label (default: WALU_EFI)
  --force                Required for destructive install
  --confirm <target>     Must exactly match --device or --image
  --yes                  Final confirmation flag
  --skip-driver-load     Skip host open-source storage driver preflight
  --dry-run              Print actions without changing anything
  --help                 Show this help
EOF
}

die() {
  echo "install-storage: $*" >&2
  exit 1
}

log() {
  echo "install-storage: $*"
}

require_tool() {
  local name="$1"
  command -v "$name" >/dev/null 2>&1 || die "missing required host tool: $name"
}

run_cmd() {
  printf '+ '
  printf '%q ' "$@"
  printf '\n'
  if [[ "$DRY_RUN" -eq 0 ]]; then
    "$@"
  fi
}

is_positive_int() {
  [[ "${1:-}" =~ ^[1-9][0-9]*$ ]]
}

partition_path() {
  local disk="$1"
  local index="$2"
  if [[ "$disk" =~ [0-9]$ ]]; then
    echo "${disk}p${index}"
  else
    echo "${disk}${index}"
  fi
}

wait_for_block() {
  local path="$1"
  local tries=60
  while [[ "$tries" -gt 0 ]]; do
    if [[ -b "$path" ]]; then
      return 0
    fi
    sleep 0.1
    tries=$((tries - 1))
  done
  return 1
}

refresh_partition_table() {
  local disk="$1"
  if command -v partprobe >/dev/null 2>&1; then
    partprobe "$disk" || true
  fi
  if command -v blockdev >/dev/null 2>&1; then
    blockdev --rereadpt "$disk" >/dev/null 2>&1 || true
  fi
}

load_open_source_storage_drivers() {
  local modules=(
    ahci
    sd_mod
    usb_storage
    uas
    xhci_pci
    xhci_hcd
    nvme
    nvme_core
    virtio_pci
    virtio_blk
  )

  if ! command -v modprobe >/dev/null 2>&1; then
    log "warning: modprobe not found; skipping driver load preflight"
    return 0
  fi

  log "loading host open-source storage drivers (best effort)"
  for mod in "${modules[@]}"; do
    modprobe "$mod" >/dev/null 2>&1 || true
  done
}

seed_base_system() {
  local root="$1"
  local root_uuid="$2"

  mkdir -p \
    "$root/bin" \
    "$root/sbin" \
    "$root/usr/bin" \
    "$root/usr/sbin" \
    "$root/etc/skel" \
    "$root/var/log" \
    "$root/var/tmp" \
    "$root/home/walu" \
    "$root/root" \
    "$root/tmp" \
    "$root/dev" \
    "$root/proc" \
    "$root/sys" \
    "$root/run" \
    "$root/boot/grub" \
    "$root/media"

  chmod 1777 "$root/tmp" "$root/var/tmp"

  cat >"$root/etc/passwd" <<'EOF'
root:x:0:0:root:/root:/bin/sh
walu:x:1000:1000:Walu User:/home/walu:/bin/sh
EOF

  cat >"$root/etc/group" <<'EOF'
root:x:0:
walu:x:1000:walu
wheel:x:10:walu
EOF

  cat >"$root/etc/shadow" <<'EOF'
root:!:19000:0:99999:7:::
walu:!:19000:0:99999:7:::
EOF
  chmod 640 "$root/etc/shadow"

  cat >"$root/etc/hostname" <<'EOF'
waluos
EOF

  cat >"$root/etc/os-release" <<'EOF'
NAME="WaluOS"
ID=waluos
PRETTY_NAME="WaluOS"
EOF

  if [[ -n "$root_uuid" ]]; then
    cat >"$root/etc/fstab" <<EOF
UUID=$root_uuid / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
EOF
  else
    cat >"$root/etc/fstab" <<'EOF'
/dev/root / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
EOF
  fi

  cat >"$root/etc/profile" <<'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/home/walu
EOF

  cat >"$root/etc/motd" <<'EOF'
Welcome to WaluOS
A dreaming OS from Bangladesh.
EOF

  cat >"$root/home/walu/.profile" <<'EOF'
export PATH=/bin:/usr/bin
cd /home/walu
EOF

  cat >"$root/var/log/install.log" <<'EOF'
install_storage.sh: unix-like base system prepared
EOF
}

prepare_disk_grub_config() {
  local source_cfg="$1"
  local out_cfg="$2"

  cat >"$out_cfg" <<'EOF'
set timeout=1
set default=0
terminal_input console
insmod all_video
insmod efi_gop
insmod efi_uga
insmod gfxterm
if loadfont /boot/grub/fonts/unicode.pf2; then
    terminal_output gfxterm
else
    terminal_output console
fi
insmod part_gpt
insmod fat
search --no-floppy --file /boot/kernel.elf --set=root
menuentry "WaluOS" {
    multiboot2 ($root)/boot/kernel.elf
    boot
}
EOF
  if [[ -n "$source_cfg" ]]; then
    :
  fi
}

TARGET_DEVICE=""
TARGET_IMAGE=""
TARGET=""
SIZE_MIB=2048
ESP_SIZE_MIB=256
KERNEL_PATH="build/kernel.elf"
GRUB_CONFIG_PATH="grub/grub.cfg"
ROOT_LABEL="WALU_ROOT"
EFI_LABEL="WALU_EFI"
FORCE=0
DRY_RUN=0
CONFIRM=""
YES=0
SKIP_DRIVER_LOAD=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)
      [[ $# -ge 2 ]] || die "--device requires a value"
      TARGET_DEVICE="$2"
      shift 2
      ;;
    --image)
      [[ $# -ge 2 ]] || die "--image requires a value"
      TARGET_IMAGE="$2"
      shift 2
      ;;
    --size-mib)
      [[ $# -ge 2 ]] || die "--size-mib requires a value"
      SIZE_MIB="$2"
      shift 2
      ;;
    --esp-size-mib)
      [[ $# -ge 2 ]] || die "--esp-size-mib requires a value"
      ESP_SIZE_MIB="$2"
      shift 2
      ;;
    --kernel)
      [[ $# -ge 2 ]] || die "--kernel requires a value"
      KERNEL_PATH="$2"
      shift 2
      ;;
    --grub-config)
      [[ $# -ge 2 ]] || die "--grub-config requires a value"
      GRUB_CONFIG_PATH="$2"
      shift 2
      ;;
    --root-label)
      [[ $# -ge 2 ]] || die "--root-label requires a value"
      ROOT_LABEL="$2"
      shift 2
      ;;
    --efi-label)
      [[ $# -ge 2 ]] || die "--efi-label requires a value"
      EFI_LABEL="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --confirm)
      [[ $# -ge 2 ]] || die "--confirm requires a value"
      CONFIRM="$2"
      shift 2
      ;;
    --yes)
      YES=1
      shift
      ;;
    --skip-driver-load)
      SKIP_DRIVER_LOAD=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

if [[ -n "$TARGET_DEVICE" && -n "$TARGET_IMAGE" ]]; then
  die "use either --device or --image, not both"
fi
if [[ -z "$TARGET_DEVICE" && -z "$TARGET_IMAGE" ]]; then
  die "one target is required: --device <path> or --image <path>"
fi
if ! is_positive_int "$SIZE_MIB"; then
  die "--size-mib must be a positive integer"
fi
if ! is_positive_int "$ESP_SIZE_MIB"; then
  die "--esp-size-mib must be a positive integer"
fi
if [[ "$ESP_SIZE_MIB" -ge "$SIZE_MIB" ]]; then
  die "--esp-size-mib must be smaller than --size-mib"
fi
if [[ ! -f "$KERNEL_PATH" ]]; then
  die "kernel not found at $KERNEL_PATH (build it first: make iso or make $KERNEL_PATH)"
fi
if [[ ! -f "$GRUB_CONFIG_PATH" ]]; then
  die "grub config not found at $GRUB_CONFIG_PATH"
fi

if [[ -n "$TARGET_DEVICE" ]]; then
  TARGET="$TARGET_DEVICE"
else
  TARGET="$TARGET_IMAGE"
fi

if [[ "$DRY_RUN" -eq 0 ]]; then
  if [[ "$FORCE" -ne 1 || "$YES" -ne 1 || "$CONFIRM" != "$TARGET" ]]; then
    die "destructive install requires: --force --confirm $TARGET --yes"
  fi
fi

if [[ "$DRY_RUN" -eq 0 && "$(id -u)" -ne 0 ]]; then
  die "this installer needs root privileges for partitioning/mkfs/mount"
fi

require_tool parted
require_tool mkfs.ext4
require_tool losetup
require_tool mount
require_tool umount
require_tool blkid
require_tool grub-mkstandalone

if command -v mkfs.vfat >/dev/null 2>&1; then
  MKFS_FAT_BIN="mkfs.vfat"
elif command -v mkfs.fat >/dev/null 2>&1; then
  MKFS_FAT_BIN="mkfs.fat"
else
  die "missing required host tool: mkfs.vfat (or mkfs.fat)"
fi

if command -v udevadm >/dev/null 2>&1; then
  HAS_UDEVADM=1
else
  HAS_UDEVADM=0
fi

if [[ "$DRY_RUN" -eq 0 && "$SKIP_DRIVER_LOAD" -eq 0 ]]; then
  load_open_source_storage_drivers
fi

WORK_DIR=""
ROOT_MNT=""
ESP_MNT=""
LOOP_DEV=""

cleanup() {
  set +e

  if [[ -n "$ESP_MNT" ]] && findmnt -rn "$ESP_MNT" >/dev/null 2>&1; then
    umount "$ESP_MNT"
  fi
  if [[ -n "$ROOT_MNT" ]] && findmnt -rn "$ROOT_MNT" >/dev/null 2>&1; then
    umount "$ROOT_MNT"
  fi
  if [[ -n "$LOOP_DEV" ]]; then
    losetup -d "$LOOP_DEV"
  fi
  if [[ -n "$WORK_DIR" && -d "$WORK_DIR" ]]; then
    rm -rf "$WORK_DIR"
  fi
}
trap cleanup EXIT

DISK=""
if [[ -n "$TARGET_IMAGE" ]]; then
  if [[ -e "$TARGET_IMAGE" && "$DRY_RUN" -eq 0 && "$FORCE" -ne 1 ]]; then
    die "image already exists; rerun with --force --confirm $TARGET_IMAGE --yes"
  fi
  run_cmd mkdir -p "$(dirname "$TARGET_IMAGE")"
  run_cmd truncate -s "${SIZE_MIB}M" "$TARGET_IMAGE"
  if [[ "$DRY_RUN" -eq 0 ]]; then
    LOOP_DEV="$(losetup --find --show --partscan "$TARGET_IMAGE")"
    DISK="$LOOP_DEV"
    log "using loop device $DISK"
  else
    DISK="/dev/loopX"
  fi
else
  [[ "$TARGET_DEVICE" == /dev/* ]] || die "--device must be under /dev"
  [[ -b "$TARGET_DEVICE" ]] || die "not a block device: $TARGET_DEVICE"

  if [[ "$DRY_RUN" -eq 0 ]]; then
    root_source="$(findmnt -no SOURCE / 2>/dev/null || true)"
    if [[ "$root_source" == /dev/* ]]; then
      root_parent="$(lsblk -no PKNAME "$root_source" 2>/dev/null | head -n1 || true)"
      if [[ -n "$root_parent" ]]; then
        root_parent="/dev/$root_parent"
      fi
      if [[ "$TARGET_DEVICE" == "$root_source" || "$TARGET_DEVICE" == "$root_parent" ]]; then
        die "refusing to overwrite current root device: $TARGET_DEVICE"
      fi
    fi

    if lsblk -nrpo MOUNTPOINT "$TARGET_DEVICE" | grep -q '[^[:space:]]'; then
      die "target device has mounted filesystems; unmount before install: $TARGET_DEVICE"
    fi
  fi

  DISK="$TARGET_DEVICE"
fi

log "creating GPT layout on $DISK"
run_cmd parted -s "$DISK" mklabel gpt
run_cmd parted -s "$DISK" mkpart ESP fat32 1MiB "$((ESP_SIZE_MIB + 1))MiB"
run_cmd parted -s "$DISK" set 1 esp on
run_cmd parted -s "$DISK" mkpart ROOT ext4 "$((ESP_SIZE_MIB + 1))MiB" 100%

if [[ "$DRY_RUN" -eq 0 ]]; then
  refresh_partition_table "$DISK"
fi

if [[ "$DRY_RUN" -eq 0 && "$HAS_UDEVADM" -eq 1 ]]; then
  run_cmd udevadm settle
fi

ESP_PART="$(partition_path "$DISK" 1)"
ROOT_PART="$(partition_path "$DISK" 2)"

if [[ "$DRY_RUN" -eq 0 ]]; then
  wait_for_block "$ESP_PART" || die "ESP partition node did not appear: $ESP_PART"
  wait_for_block "$ROOT_PART" || die "root partition node did not appear: $ROOT_PART"
fi

log "formatting partitions"
run_cmd "$MKFS_FAT_BIN" -F 32 -n "$EFI_LABEL" "$ESP_PART"
run_cmd mkfs.ext4 -F -L "$ROOT_LABEL" "$ROOT_PART"

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "dry-run complete"
  exit 0
fi

WORK_DIR="$(mktemp -d /tmp/walu-install.XXXXXX)"
ROOT_MNT="$WORK_DIR/root"
ESP_MNT="$WORK_DIR/esp"
mkdir -p "$ROOT_MNT" "$ESP_MNT"

run_cmd mount "$ROOT_PART" "$ROOT_MNT"
run_cmd mount "$ESP_PART" "$ESP_MNT"

ROOT_UUID="$(blkid -s UUID -o value "$ROOT_PART" 2>/dev/null || true)"

log "seeding Unix-like root filesystem"
seed_base_system "$ROOT_MNT" "$ROOT_UUID"

mkdir -p "$ROOT_MNT/boot/grub" "$ESP_MNT/EFI/BOOT" "$ESP_MNT/boot/grub"

install -m 0644 "$KERNEL_PATH" "$ROOT_MNT/boot/kernel.elf"
install -m 0644 "$KERNEL_PATH" "$ESP_MNT/boot/kernel.elf"

DISK_GRUB_CFG="$WORK_DIR/grub-disk.cfg"
prepare_disk_grub_config "$GRUB_CONFIG_PATH" "$DISK_GRUB_CFG"

install -m 0644 "$DISK_GRUB_CFG" "$ROOT_MNT/boot/grub/grub.cfg"
install -m 0644 "$DISK_GRUB_CFG" "$ESP_MNT/boot/grub/grub.cfg"

if grep -q 'unicode\.pf2' "$DISK_GRUB_CFG"; then
  if [[ -f /usr/share/grub/unicode.pf2 ]]; then
    mkdir -p "$ROOT_MNT/boot/grub/fonts" "$ESP_MNT/boot/grub/fonts"
    install -m 0644 /usr/share/grub/unicode.pf2 "$ROOT_MNT/boot/grub/fonts/unicode.pf2"
    install -m 0644 /usr/share/grub/unicode.pf2 "$ESP_MNT/boot/grub/fonts/unicode.pf2"
  else
    log "warning: grub config references unicode.pf2 but /usr/share/grub/unicode.pf2 was not found"
  fi
fi

run_cmd grub-mkstandalone \
  -O x86_64-efi \
  -o "$ESP_MNT/EFI/BOOT/BOOTX64.EFI" \
  --modules="part_gpt part_msdos fat ext2 normal multiboot2 search search_fs_file configfile all_video efi_gop efi_uga gfxterm" \
  "boot/grub/grub.cfg=$DISK_GRUB_CFG"

cat >"$ESP_MNT/startup.nsh" <<'EOF'
fs0:\EFI\BOOT\BOOTX64.EFI
EOF

run_cmd sync

run_cmd umount "$ESP_MNT"
run_cmd umount "$ROOT_MNT"
ESP_MNT=""
ROOT_MNT=""

if [[ -n "$LOOP_DEV" ]]; then
  run_cmd losetup -d "$LOOP_DEV"
  LOOP_DEV=""
fi

log "install complete: target $TARGET is UEFI bootable (removable media path)."
if [[ -n "$TARGET_IMAGE" ]]; then
  log "test with: qemu-system-x86_64 -machine q35 -drive format=raw,file=$TARGET_IMAGE -m 512M"
fi
