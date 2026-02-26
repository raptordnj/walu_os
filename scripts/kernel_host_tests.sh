#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

OUT_BIN="/tmp/walu_kernel_host_tests"
KBD_OUT_BIN="/tmp/walu_keyboard_host_tests"
STORAGE_OUT_BIN="/tmp/walu_storage_host_tests"
FS_OUT_BIN="/tmp/walu_fs_host_tests"

# Host tests should use libc memory primitives to avoid freestanding/builtin
# optimization recursion that can occur with kernel string.c at -O2.
gcc -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Ikernel/include \
  kernel/tests/test_tty_pty.c kernel/src/core/tty.c kernel/src/core/pty.c \
  -o "$OUT_BIN"

"$OUT_BIN"

gcc -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Ikernel/include \
  kernel/tests/test_keyboard.c kernel/src/core/keyboard.c \
  -o "$KBD_OUT_BIN"

"$KBD_OUT_BIN"

gcc -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Ikernel/include \
  kernel/tests/test_storage.c kernel/src/core/storage.c kernel/src/lib/string.c \
  -o "$STORAGE_OUT_BIN"

"$STORAGE_OUT_BIN"

gcc -std=gnu11 -Wall -Wextra -O2 -fno-builtin -Ikernel/include \
  kernel/tests/test_fs.c kernel/src/core/fs.c kernel/src/lib/string.c \
  -o "$FS_OUT_BIN"

"$FS_OUT_BIN"
