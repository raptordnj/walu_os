#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_BIN="/tmp/walu_kernel_host_tests"

# Host tests should use libc memory primitives to avoid freestanding/builtin
# optimization recursion that can occur with kernel string.c at -O2.
gcc -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Ikernel/include \
  kernel/tests/test_tty_pty.c kernel/src/core/tty.c kernel/src/core/pty.c \
  -o "$OUT_BIN"

"$OUT_BIN"
