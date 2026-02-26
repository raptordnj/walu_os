# WaluOS Userland Scaffolding

This directory contains host-buildable scaffolding for milestone services:

- `walud`: unit-file parser/validator skeleton.
- `authd`: password policy and shadow-state helper.
- `storaged`: Linux-like disk operation helper with safety gates.

## `storaged` command set
- `lsblk` (device listing, `--json` optional)
- `blkid` (filesystem signature listing)
- `probe --device <path>` (device, fs, mount, UUID/label details)
- `mount --device <path> --target <dir>` with safe default options
- `umount --target <dir|device>`
- `fsck --device <path>` (read-only check by default)
- `format --device <path> [--fstype ext4|vfat|xfs] [--label <name>]`

Safety defaults:
- Unknown removable media mounts with `ro,nosuid,nodev,noexec,relatime`.
- Read-write on untrusted removable media requires `--force`.
- Destructive commands (`format`, forced `fsck`) require:
  - `--force`
  - `--confirm <exact-device>`
  - `--yes`
- Every command supports `--dry-run` where meaningful.

## Build
```bash
cd userland
make
```

## Test
```bash
cd userland
make test
```

## Notes
- These tools are implementation scaffolds and do not yet replace full distro-grade services.
- `storaged` executes host tools (`lsblk`, `blkid`, `mount`, `umount`, `fsck`, `mkfs.*`) through explicit command invocation with policy checks.
- `authd` supports `policy-check --stdin` to avoid putting passwords in shell history.
