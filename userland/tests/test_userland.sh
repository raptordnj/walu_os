#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
BIN_DIR="$ROOT_DIR/userland/build/bin"

pass() {
  printf "PASS: %s\n" "$1"
}

fail() {
  printf "FAIL: %s\n" "$1" >&2
  exit 1
}

expect_success() {
  name="$1"
  shift
  if "$@"; then
    pass "$name"
  else
    fail "$name"
  fi
}

expect_failure() {
  name="$1"
  shift
  if "$@"; then
    fail "$name"
  else
    pass "$name"
  fi
}

expect_success "walud validate sample unit" \
  "$BIN_DIR/walud" validate "$ROOT_DIR/docs/examples/walu/units/sshd.service"

TMP_UNIT="$(mktemp)"
cat > "$TMP_UNIT" <<'UNIT'
[Unit]
Description=Broken Unit

[Service]
ExecStart=bin/not-absolute
UNIT
expect_failure "walud reject relative ExecStart" "$BIN_DIR/walud" validate "$TMP_UNIT"
rm -f "$TMP_UNIT"

expect_success "authd policy check stdin strong password" \
  sh -c "printf '%s\n' 'StrongPass!123' | '$BIN_DIR/authd' policy-check --stdin"

expect_failure "authd policy rejects short password" \
  "$BIN_DIR/authd" policy-check short

expect_failure "storaged rejects non-block char device" \
  "$BIN_DIR/storaged" format --device /dev/null --dry-run

expect_failure "authd verify fails for missing user in sample shadow" \
  sh -c "printf '%s\n' 'StrongPass!123' | '$BIN_DIR/authd' verify --user no_such_user --shadow '$ROOT_DIR/docs/examples/etc/shadow' --password-stdin"

expect_success "storaged probe works" \
  "$BIN_DIR/storaged" probe --device /dev/null

FIRST_BLOCK_DEV="$(find /dev -maxdepth 1 -type b 2>/dev/null | head -n 1 || true)"
if [ -n "$FIRST_BLOCK_DEV" ]; then
  expect_success "storaged dry-run valid block device" \
    "$BIN_DIR/storaged" format --device "$FIRST_BLOCK_DEV" --dry-run

  expect_failure "storaged requires destructive confirmations" \
    "$BIN_DIR/storaged" format --device "$FIRST_BLOCK_DEV"
else
  pass "storaged block-device tests skipped (no block devices in environment)"
fi

pass "all userland safety tests"
