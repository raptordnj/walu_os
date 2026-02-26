# Skill: waluos-architecture

## Use When
- Updating OS architecture scope, subsystem boundaries, or milestone definitions.
- Adding or changing security, storage, or compatibility strategy.

## Inputs
- `docs/WALUOS_SYSTEM_BLUEPRINT.md`
- `docs/interfaces/*`
- `docs/examples/*`

## Workflow
1. Read the affected numbered sections in `docs/WALUOS_SYSTEM_BLUEPRINT.md`.
2. Apply edits with explicit feasibility notes (MVP vs later phases).
3. Keep security defaults conservative and explicit.
4. Sync any referenced examples/interfaces under `docs/examples/` and `docs/interfaces/`.
5. Update `README.md` only if feature status or layout pointers changed.

## Output Checklist
- Numbered blueprint sections remain present and ordered.
- Any new config/API reference has a concrete file in `docs/`.
- Trade-offs and deferred items are stated clearly.
