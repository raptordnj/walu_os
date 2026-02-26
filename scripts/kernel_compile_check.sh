#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

TMP_DIR="$(mktemp -d /tmp/walu-kernel-compile-check.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

for f in kernel/src/core/*.c kernel/src/arch/x86_64/idt.c kernel/src/lib/string.c; do
  gcc -std=gnu11 -Wall -Wextra -Ikernel/include \
    -ffreestanding -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
    -c "$f" -o "$TMP_DIR/$(basename "$f").o"
done

echo "kernel compile check passed"
