# AGENTS.md

## Purpose
Repository guidance for CLI coding agents working on WaluOS.

## Current Project State
- Kernel prototype in C with a small Rust static library.
- Boot path: GRUB -> Multiboot2 -> x86_64 long mode.
- Core subsystems: console, PMM/VMM, IDT/PIC/PIT, keyboard IRQ, tiny shell.
- Architecture/spec source of truth: `docs/WALUOS_SYSTEM_BLUEPRINT.md`.

## Primary References
- System blueprint: `docs/WALUOS_SYSTEM_BLUEPRINT.md`
- Interface sketches: `docs/interfaces/`
- Example system configs: `docs/examples/`

## Build and Run
- Build ISO: `make iso`
- Run (GUI): `make run`
- Run (headless serial): `make run-headless`
- Kernel host checks: `make test-kernel`
- Userland checks: `make test-userland`
- Boot smoke (QEMU, toolchain wrapper aware): `make boot-smoke`
- Artifact packaging: `make package-artifacts`

## Implementation Priorities
1. Keep kernel changes small and testable.
2. Preserve freestanding constraints (`-ffreestanding`, no host libc assumptions).
3. Keep docs and code in sync when behavior changes.
4. For terminal/input work, follow `docs/interfaces/input_tty.md`.
5. Keep scripts fault-tolerant: dependency checks, explicit error output, and non-destructive defaults.

## Safety and Operations
- Do not introduce destructive disk-operation defaults in tooling or examples.
- Any destructive storage flow must require explicit confirmation and `--force`.
- Prefer additive changes over sweeping rewrites unless explicitly requested.

## Documentation Update Rule
When adding or changing behavior:
- Update `README.md` (feature status / layout pointers).
- Update `docs/WALUOS_SYSTEM_BLUEPRINT.md` if architecture or scope changes.
- Update related files in `docs/interfaces/` and `docs/examples/`.
