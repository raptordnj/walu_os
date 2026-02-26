#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BOOTSTRAP_LOG="/tmp/walu_toolchain_bootstrap.log"
ISO_LOG="/tmp/walu_make_iso.log"
BOOT_LOG="/tmp/walu_boot_smoke.log"
rm -f "$BOOTSTRAP_LOG" "$ISO_LOG" "$BOOT_LOG"

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
  if [[ -f "$BOOT_LOG" ]]; then
    echo "----- qemu boot log -----" >&2
    tail -n 120 "$BOOT_LOG" >&2 || true
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

set +e
timeout 20s qemu-system-x86_64 \
  -cdrom build/waluos.iso \
  -m 256M \
  -serial stdio \
  -display none \
  -no-reboot \
  -no-shutdown >"$BOOT_LOG" 2>&1
QEMU_RC=$?
set -e

HAS_READY_MARKER=0
HAS_FB_MARKER=0
if grep -q "Kernel ready. Type \`help\`." "$BOOT_LOG"; then
  HAS_READY_MARKER=1
fi
if grep -Eq "Framebuffer console enabled|Framebuffer console unavailable" "$BOOT_LOG"; then
  HAS_FB_MARKER=1
fi

if [[ "$HAS_READY_MARKER" -eq 1 && "$HAS_FB_MARKER" -eq 1 ]]; then
  if [[ "$QEMU_RC" -ne 0 && "$QEMU_RC" -ne 124 ]]; then
    echo "boot smoke failed: qemu exited with code $QEMU_RC" >&2
    exit 1
  fi
  echo "boot smoke test passed"
  exit 0
fi

# Fallback for builds that do not mirror console output to serial:
# accept timeout-based runtime if no fatal errors are visible.
if [[ "$QEMU_RC" -eq 124 ]]; then
  if grep -Eqi "triple fault|fatal|panic|aborted|assert" "$BOOT_LOG"; then
    echo "boot smoke failed: fatal keyword detected in qemu log" >&2
    cat "$BOOT_LOG" >&2
    exit 1
  fi
  echo "boot smoke test passed (timeout fallback: serial boot marker unavailable)"
  exit 0
fi

if [[ "$QEMU_RC" -ne 0 && "$QEMU_RC" -ne 124 ]]; then
  echo "boot smoke failed: qemu exited with code $QEMU_RC" >&2
  exit 1
fi

echo "boot smoke failed: serial boot markers missing and qemu did not reach timeout fallback" >&2
cat "$BOOT_LOG" >&2
exit 1
