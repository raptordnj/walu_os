#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BOOTSTRAP_LOG="/tmp/walu_toolchain_bootstrap.log"
ISO_LOG="/tmp/walu_make_iso.log"
UEFI_BOOT_LOG="/tmp/walu_boot_smoke_uefi.log"
rm -f "$BOOTSTRAP_LOG" "$ISO_LOG" "$UEFI_BOOT_LOG"

require_tool() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required host tool: $name" >&2
    exit 1
  fi
}

dump_logs_on_error() {
  local rc="$1"
  if [[ "$rc" -eq 0 ]]; then
    return
  fi

  echo "boot smoke failed (exit=$rc)" >&2
  if [[ -f "$BOOTSTRAP_LOG" ]]; then
    echo "----- bootstrap log -----" >&2
    tail -n 120 "$BOOTSTRAP_LOG" >&2 || true
  fi
  if [[ -f "$ISO_LOG" ]]; then
    echo "----- make iso log -----" >&2
    tail -n 120 "$ISO_LOG" >&2 || true
  fi
  if [[ -f "$UEFI_BOOT_LOG" ]]; then
    echo "----- qemu uefi boot log -----" >&2
    tail -n 160 "$UEFI_BOOT_LOG" >&2 || true
  fi
}
trap 'dump_logs_on_error $?' EXIT

./scripts/bootstrap_toolchain.sh >"$BOOTSTRAP_LOG" 2>&1
export PATH="$ROOT_DIR/toolchain/bin:$PATH"

require_tool grub-mkrescue
require_tool xorriso
require_tool mformat
require_tool qemu-system-x86_64
require_tool timeout

make iso >"$ISO_LOG" 2>&1

find_uefi_firmware() {
  local code=""
  local vars=""

  for p in \
    /usr/share/OVMF/OVMF_CODE_4M.fd \
    /usr/share/OVMF/OVMF_CODE.fd; do
    if [[ -f "$p" ]]; then
      code="$p"
      break
    fi
  done

  for p in \
    /usr/share/OVMF/OVMF_VARS_4M.fd \
    /usr/share/OVMF/OVMF_VARS.fd; do
    if [[ -f "$p" ]]; then
      vars="$p"
      break
    fi
  done

  if [[ -z "$code" || -z "$vars" ]]; then
    echo "missing required UEFI firmware files (install package: ovmf)" >&2
    return 1
  fi

  printf '%s\n%s\n' "$code" "$vars"
}

mapfile -t firmware_paths < <(find_uefi_firmware)
UEFI_CODE="${firmware_paths[0]}"
UEFI_VARS_TEMPLATE="${firmware_paths[1]}"
UEFI_VARS_RUNTIME="/tmp/walu_ovmf_vars.fd"
cp -f "$UEFI_VARS_TEMPLATE" "$UEFI_VARS_RUNTIME"

set +e
timeout 25s qemu-system-x86_64 \
  -machine q35 \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file="$UEFI_CODE" \
  -drive if=pflash,format=raw,file="$UEFI_VARS_RUNTIME" \
  -cdrom build/waluos.iso \
  -serial stdio \
  -display none \
  -no-reboot \
  -no-shutdown >"$UEFI_BOOT_LOG" 2>&1
QEMU_RC=$?
set -e

if [[ "$QEMU_RC" -ne 0 && "$QEMU_RC" -ne 124 ]]; then
  echo "boot smoke failed: qemu exited with code $QEMU_RC" >&2
  exit 1
fi

if grep -Eq "unsupported tag:|you need to load the kernel first|Failed to boot both default and fallback entries|Invalid multiboot2 magic" "$UEFI_BOOT_LOG"; then
  echo "boot smoke failed: bootloader/kernel handoff error detected" >&2
  exit 1
fi

if ! grep -q "Kernel ready. Type \`help\`." "$UEFI_BOOT_LOG"; then
  echo "boot smoke failed: kernel ready marker not found in UEFI run" >&2
  exit 1
fi

if ! grep -q "Multiboot2 handoff OK" "$UEFI_BOOT_LOG"; then
  echo "boot smoke failed: multiboot2 handoff marker missing in UEFI run" >&2
  exit 1
fi

echo "boot smoke test passed (UEFI)"
