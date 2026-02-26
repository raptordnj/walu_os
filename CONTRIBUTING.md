# Contributing

## Development Flow

1. Create a branch from `main`.
2. Make focused changes.
3. Run local checks before opening a PR:

```bash
make test
make boot-smoke
make package-artifacts
```

4. Open a pull request.
5. Wait for required checks to pass.

## Coding Expectations

- Keep changes small and testable.
- Preserve freestanding kernel constraints.
- Keep destructive operations opt-in and explicit.
- Update docs whenever behavior changes.

## Commit Guidelines

- Use clear, imperative commit messages.
- Separate unrelated changes into separate commits.

## Pull Request Checklist

- [ ] tests updated or added when behavior changes
- [ ] docs updated (`README.md`, `docs/`, interfaces/examples as needed)
- [ ] no unsafe defaults introduced
