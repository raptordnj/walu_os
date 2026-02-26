# Skill: waluos-doc-sync

## Use When
- Code behavior changed and docs/config examples must be synchronized.
- New interfaces are added under `docs/interfaces/`.

## Workflow
1. Update `README.md` feature/status notes and project layout pointers.
2. Update `docs/WALUOS_SYSTEM_BLUEPRINT.md` sections impacted by the change.
3. Ensure referenced files exist under:
   - `docs/examples/`
   - `docs/interfaces/`
4. Run a quick reference check:
   - list section headers
   - list doc files
   - grep for dead references

## Completion Criteria
- No stale references in blueprint appendices.
- README reflects current capabilities and limitations.
- Example files are concrete, not placeholders.
