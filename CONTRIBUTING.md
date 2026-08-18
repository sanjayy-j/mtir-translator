# Contributing

## Branching

- `main` is protected and must always be demonstrable. Never commit to it directly.
- Feature branches: `<member>/<module>-<short-description>`, e.g. `m4/wasm-reg2stack`.

## Pull requests

Every PR needs **one review from the backup owner** and **a green CI run**
before merge. Backup owners: M1 ↔ M2, M3 ↔ M4. This exists so no module has a
single point of failure and so each member can answer viva questions about a
second area.

## Commits

Prefix with the module you are touching so that per-member contribution is
visible in `git log`:

```
[M1a] lexer: handle hex literals without digits
[M6a] reg2stack: peephole for single-use locals
[docs] cir-spec: pin the round-trip property
```

Do not squash a week of work into one commit — the contribution log and the
individual mark both depend on granular, attributable history.

## Interfaces

The textual `.cir` format is the contract between the middle end and every
back end. It was frozen at the end of Week 5 and changes only by team
agreement, recorded in `docs/contribution-log.md`.

## Before you push

```bash
python -m pytest -q
```

Every bug found during development becomes a permanent entry in
`tests/corpus/` so it cannot be reintroduced.

## First-week commit plan

The scaffold lands as one initial commit. From there, **each member commits
their own work under their own git identity** — do not create commits on
another member's behalf, and do not rewrite history to manufacture
attribution. `git log --author` is evidence at every review, and a fabricated
history is worse than a thin one.

Set your identity once per machine:

```bash
git config user.name  "Your Name"
git config user.email "your.regno@university.edu"
```

Areas each member commits into from Week 4 onward:

| Member | Paths |
|---|---|
| 1 | `src/frontend/`, `src/driver.py`, `docs/minilang-spec.md`, `README.md` |
| 2 | `src/sema/`, `src/cir/verifier.py`, `src/cir/interp.py`, `docs/divergence.md`, `tests/corpus/` |
| 3 | `src/cir/` (builder, printer, parser), `src/opt/`, `src/backend/llvm/`, `docs/cir-spec.md` |
| 4 | `src/backend/wasm/`, `src/backend/stackvm/`, `tests/harness/`, `.github/workflows/` |
