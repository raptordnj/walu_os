# WaluOS Userland Scaffolding

This directory contains host-buildable scaffolding for milestone services:

- `walud`: unit-file parser/validator skeleton.
- `authd`: password policy and shadow-state helper.
- `storaged`: safe storage operation gate with mandatory `--force` + confirmation model.

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
- `storaged` intentionally does not execute `mkfs` directly yet; it enforces safety policy and approval flow first.
- `authd` supports `policy-check --stdin` to avoid putting passwords in shell history.
