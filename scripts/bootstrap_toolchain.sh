#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$ROOT_DIR/toolchain/bin"
mkdir -p "$BIN_DIR"

require_tool() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required host tool: $name" >&2
    exit 1
  fi
}

has_tool() {
  local name="$1"
  command -v "$name" >/dev/null 2>&1 || [[ -x "$BIN_DIR/$name" ]]
}

link_or_wrap() {
  local target_name="$1"
  local host_name="$2"
  local out="$BIN_DIR/$target_name"
  local resolved=""
  if command -v "$target_name" >/dev/null 2>&1; then
    resolved="$(command -v "$target_name")"
    ln -sf "$resolved" "$out"
    return
  fi
  require_tool "$host_name"
  resolved="$(command -v "$host_name")"
  cat > "$out" <<EOF_WRAP
#!/usr/bin/env bash
exec "$resolved" "\$@"
EOF_WRAP
  chmod +x "$out"
}

link_or_wrap x86_64-elf-gcc gcc
link_or_wrap x86_64-elf-ld ld
link_or_wrap x86_64-elf-ar ar
link_or_wrap x86_64-elf-objcopy objcopy

for tool in grub-mkrescue xorriso mformat qemu-system-x86_64 timeout; do
  if ! has_tool "$tool"; then
    echo "warning: optional host tool not found: $tool" >&2
  fi
done

echo "toolchain prepared at $BIN_DIR"
echo "export PATH=\"$BIN_DIR:\$PATH\""
