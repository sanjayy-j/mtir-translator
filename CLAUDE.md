# CLAUDE.md

BCSE307 Compiler Design project, Team 5 / Project A30: **Multi-Target IR Translator**.
MiniLang source → AST → checked AST → CIR (typed, non-SSA, register-based) → LLVM IR / WebAssembly (WAT) / stack bytecode.
The CIR reference interpreter (`src/cir/interp.py`) is the semantic oracle that every back end must agree with.

## The team uses Python, not C++

The team agreed to use the **Python** implementation. Ignore the C++ tree (`include/`, `tools/`, `src/**/*.cpp`, `CMakeLists.txt`, `cmake/`, `tests/*.cpp`). It is still present only because this branch starts from a commit that had both. Don't edit it or build it, and don't delete it unless Krishita asks.

## Branch state (as of 20 Oct 2026)

- Working branch: **`python-combined`** (local only, **not pushed**).
  - Base: `da14d31`, the last upstream commit that still had the full Python tree.
  - Commit on top: `f83794c`, which restores the Review 2 modules.
- `origin/main` is a C++17-only rewrite by Sanjay (commits 15–16 Sept, ending with `5de2ff2` "retire the Python prototype"). It is a legitimate, documented rewrite, not tampering. Its reasons are in `docs/migration.md`.
- Where the code in this branch came from:
  - **From the repo's Python history (Sanjay):** `cir/builder.py`, `cir/parser.py`, `cir/cfg.py`, `cir/ir.py`, `opt/*`, `backend/llvm/*` (including div/shift guards), and the frontend.
  - **From Krishita's unpushed Review 2 working copy:** `sema/typecheck.py`, `cir/verifier.py`, `cir/interp.py`, `backend/wasm/emit_wat.py`, `backend/stackvm/emit_sbc.py`, `backend/stackvm/vm.py`.
  - **Changed in the merge:** `src/driver.py` was updated to wire `--run`, `--emit=wat` and `--emit=sbc`. The builder runs before the type checker so that coded diagnostics (E001…) win.

**Do not push, force-push, or touch `main` without explicit confirmation.** The plan is to push `python-combined` as a new branch once the team agrees.

## Setup and commands

You need Python ≥ 3.10, because the code uses `match`. The system `python3` is 3.9 and will fail.

```bash
python3.12 -m venv venv && source venv/bin/activate && pip install pytest
python -m pytest -q                                  # all pass (7 skipped)
python -m src.driver --emit=tokens|ast|cir|ll|wat|sbc <file>.mini
python -m src.driver --run <file>.mini               # interpreter; exit 4 on trap
python -m src.driver --opt=1 --emit=cir <file>.mini  # constfold + copyprop + DCE
python -m src.driver --emit=ll <file>.cir            # .cir input skips the front end
python -m src.driver --demo-cir | --demo-stack
```

Expected `--run` output:

| Program | Output |
|---|---|
| `arith` | 55 |
| `control_flow` | 16 |
| `recursion` | 3628800, 1 |
| `nesting` | 3 |
| `shift_edge` | 1, -2147483648, 1, 2 |
| `div_edge` | 3, then a trap (exit 4) |
| `docs/examples/abs` | 7 |

## Known open items

- `tests/corpus/valid/arrays.mini` is rejected with **E004**. The builder refuses `let xs: int[8] = 0;` pending a team decision in `docs/decisions/0001-array-initialisers.md`. Don't choose a semantics unilaterally.
- `vm.py` understands its own opcodes (PUSHI/LOADL/JZ…), but `emit_sbc.py` emits Wasm-style mnemonics, so running the bytecode on the VM does not work yet. This was defect #1 in the Review 2 deck.
- The Wasm back end uses a program-counter dispatch loop. Relooper structuring (`backend/wasm/structurer.py`) is still a stub.
- Not built yet: `tests/harness/differential.py` (four-way agreement harness, target ≥98% over a 100-program corpus) and `tests/harness/gen_random.py`.
- `sema/symtab.py` and `sema/diagnostics.py` are stubs; `typecheck.py` uses a dict environment.

## Team and ownership (see CONTRIBUTING.md)

| Member | Area | Owns |
|---|---|---|
| M1 Krishita Gupta | Front end | `src/frontend/`, `src/driver.py` |
| M2 Pragati Manot | Semantics | `src/sema/`, `cir/verifier.py`, `cir/interp.py` |
| M3 Sanjay J | CIR and LLVM | `src/cir/` (builder, printer, parser), `src/opt/`, `src/backend/llvm/` |
| M4 Sparsh Khandelwal | Stack targets and testing | `src/backend/wasm/`, `src/backend/stackvm/`, `tests/harness/` |

Commit messages are prefixed by module, e.g. `[M1a] lexer: …`.
