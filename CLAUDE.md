# CLAUDE.md

BCSE307 Compiler Design, Team 5 / Project A30: **Multi-Target IR Translator**
(MiniLang → CIR → LLVM IR / WebAssembly / stack bytecode, all checked against
the CIR reference interpreter). Branch `python-combined` is the **Python**
implementation the team uses.

The full briefing (architecture, file map, semantics, decisions, gotchas,
history) is in PROJECT_CONTEXT.md, imported here so every session loads it:

@PROJECT_CONTEXT.md

Current state and open items: `status.md`.

## Rules (from Krishita, the user, M1)

- **Never run `git commit` or `git push`.** Krishita commits and pushes. At a
  commit point, give copy-paste commands with **no `#` comments** (Krishita's
  zsh rejects them).
- **One task at a time**: finish, report briefly, then wait for "go".
- Never touch `main` or force-push. Ask before deleting files.
- Don't edit or build the C++ tree (`include/`, `tools/`, `src/**/*.cpp`,
  `tests/*.cpp`, `CMakeLists.txt`, `cmake/`) unless Krishita asks.
- Don't decide language semantics yourself; ask Krishita (spec owner).
- `tests/corpus/valid/arrays.mini` keeps its `= 0` on purpose (the E004
  example). Don't "fix" it.
- Python ≥ 3.10: use `./venv/bin/python` (macOS `python3` is 3.9).
- Before saying work is done: `python -m pytest -q` (553 pass, 0 skipped),
  plus `python -m tests.harness.differential --random 500` at `--opt 0` and
  `--opt 1` for back-end or optimiser changes.
