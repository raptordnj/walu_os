# Skill: waluos-validation-pipeline

## Use When
- Adding or modifying build/test/CI automation.
- Updating fault-tolerance behavior in scripts under `scripts/`.
- Changing packaging, smoke tests, or local/CI validation targets.

## Inputs
- `Makefile`
- `scripts/*.sh`
- `.github/workflows/ci.yml`
- `README.md`
- `AGENTS.md`

## Workflow
1. Keep script safety defaults strict (`set -euo pipefail`, no destructive implicit behavior).
2. Validate hard dependencies early with clear error messages.
3. Ensure each script is callable directly and through a `make` target.
4. Keep CI stages incremental:
   - host compile/unit checks
   - userland checks
   - boot smoke + packaging
5. On failures, print actionable diagnostics (tail relevant logs).
6. Document every new entrypoint in `README.md` and `AGENTS.md`.

## Validation
- `make test-kernel`
- `make test-userland`
- `make boot-smoke`
- `make package-artifacts`
- Optional full run: `make ci`
