# Skill: waluos-kernel-io-terminal

## Use When
- Implementing keyboard, TTY, ANSI parsing, UTF-8 handling, console rendering, or shell input behavior.

## Inputs
- `kernel/src/core/keyboard.c`
- `kernel/src/core/console.c`
- `kernel/src/core/shell.c`
- `docs/interfaces/input_tty.md`
- `docs/WALUOS_SYSTEM_BLUEPRINT.md` (sections 2, 7, 17)

## Workflow
1. Keep IRQ handling minimal; move complex parsing outside interrupt path.
2. Maintain clear stages: scancode -> keycode -> keysym/unicode -> TTY bytes.
3. Preserve ASCII behavior while extending modifier/layout handling.
4. Gate ANSI/Unicode complexity by milestone; do not over-claim unsupported behavior.
5. Add/update docs with exact support matrix after code changes.

## Validation
- Build target if toolchain exists: `make iso`.
- If cross-compiler missing, perform host syntax check for touched C files.
- Verify no regressions in shell command entry and backspace behavior.
