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
back end. It is specified in `docs/cir-spec.md` §§ 3–4 and changes only by
team agreement; `tests/test_cir_parser.py` pins the round-trip property that
makes it a contract rather than a debug dump.

## Before you push

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
python -m pytest -q     # while the Python reference is still in the tree
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

Areas each member commits into. Paths marked *(not created yet)* are reserved
by this table, not present in the tree — they appear when that member starts
their C++ migration phase.

| Member | Paths |
|---|---|
| 1 | `include/mtir/ast/` *(not created yet)*, `src/frontend/`, `tools/mtirc/`, `docs/minilang-spec.md`, `README.md` |
| 2 | `include/mtir/sema/` *(not created yet)*, `src/sema/`, the rule 6–8 section of `src/cir/Verifier.cpp`, `docs/divergence.md`, `tests/corpus/` |
| 3 | `include/mtir/cir/`, `include/mtir/opt/`, `include/mtir/backend/llvm/` and their `src/` counterparts, `docs/cir-spec.md` |
| 4 | `include/mtir/backend/wasm/` and `include/mtir/backend/stackvm/` *(neither created yet)* and their `src/` counterparts, `CMakeLists.txt`, `cmake/`, `.github/workflows/` |

`include/mtir/support/` is shared and changes by agreement, because every
subsystem depends on `Diagnostic` and none of them owns it.

The Python tree under `src/**/*.py` and `tests/test_*.py` is the behavioural
reference for the parts of the pipeline not yet migrated to C++. Its ownership
is the same as the C++ counterpart above, and each file is removed once its
C++ replacement reaches test parity.
