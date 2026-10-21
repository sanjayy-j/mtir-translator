# Multi-Target Intermediate Representation Translator

**BCSE307 — Compiler Design** · Team 5 · Project A30

Design a common intermediate representation and translate it to multiple
execution targets while preserving semantics.

The **Python** implementation is the one the team agreed to use. It is complete end to end: MiniLang source → AST → checked AST
→ **CIR** (typed, register-based three-address IR with an explicit CFG) →
three targets, all checked against a reference interpreter.

| Target | Model | Status |
|---|---|---|
| LLVM IR (`.ll`) | register / SSA, unstructured CFG | ✅ runs natively via `clang` |
| WebAssembly (`.wat`) | structured stack machine, no `goto` | ✅ structured `block`/`loop`/`if`, runs under `wasmtime` |
| Stack bytecode (`.sbc`) + reference VM | flat stack machine, absolute jumps | ✅ runs on the VM |

The interesting problem is not emitting three files. It is that the targets
**disagree about what programs mean**: WebAssembly masks over-wide shift
counts and traps on division by zero, while LLVM leaves both undefined. CIR
fixes a single semantics, and each back end emits the guard code that realises
it. See [`docs/divergence.md`](docs/divergence.md).

**Correctness evidence:** the differential harness runs every program on all
four execution paths (interpreter, LLVM, Wasm, VM) and compares
(stdout, return value, trapped). The corpus plus 500 randomly generated
programs agree four ways at both `--opt 0` and `--opt 1`: 507/507, against an
Objective O4 target of ≥ 98%.

## Team

| Member | Role | Owns |
|---|---|---|
| M1 Krishita Gupta | Lead / Front end | Language spec, lexer, parser, AST, CLI (`src/frontend/`, `src/driver.py`) |
| M2 Pragati Manot | Semantics | Symbol table, type checker, CIR verifier, reference interpreter (`src/sema/`, `cir/verifier.py`, `cir/interp.py`) |
| M3 Sanjay J | CIR core / LLVM | CIR builder, printer/parser, optimiser, LLVM back end (`src/cir/`, `src/opt/`, `src/backend/llvm/`) |
| M4 Sparsh Khandelwal | Stack targets / Testing | Reg-to-stack, CFG structuring, Wasm + bytecode back ends, harness (`src/backend/wasm/`, `src/backend/stackvm/`, `tests/harness/`) |

## Setup

Requires Python ≥ 3.10 (the code uses `match`). On macOS the system `python3`
is 3.9, so use `python3.12` or similar.

```bash
python3.12 -m venv venv && source venv/bin/activate
pip install -r requirements.txt          # pytest, wasmtime
python -m pytest -q                      # everything passes, nothing skipped
```

`clang` (Xcode on macOS) runs the LLVM output and verifies the generated IR;
`llvm-as` is used instead if it is installed. Tests that need a missing tool
are skipped, not failed.

## Usage

```bash
python -m src.driver --emit=tokens|ast|cir|ll|wat|sbc prog.mini
python -m src.driver --run prog.mini                 # interpreter; exit 4 on a trap
python -m src.driver --opt=1 --emit=cir prog.mini    # constfold + copyprop + DCE
python -m src.driver --emit=ll docs/examples/abs.cir # .cir input skips the front end

python -m tests.harness.differential                 # four-way check of the corpus
python -m tests.harness.differential --random 500 --opt 1
python -m tests.harness.gen_random 42                # print one generated program
```

`--run` prints the program's output followed by `main`'s return value:

| Program | Output |
|---|---|
| `valid/arith` | 55 |
| `valid/control_flow` | 16 |
| `valid/recursion` | 3628800, 1 |
| `boundary/nesting` | 3 |
| `boundary/shift_edge` | 1, -2147483648, 1, 2 |
| `boundary/div_edge` | 3, then a trap (exit 4) |
| `valid/arrays` | rejected with E004 by design (decision 0001) |
| `docs/examples/abs` | 7 |

## The worked example

`docs/examples/` traces one function, `abs`, through every representation
(Figure 2 of the Review 1 report): `.mini` → `.cir` → `.ll` / `.wat` /
`.sbc.txt`. `abs.ll` and `abs.wat` are hand-written references that CI
validates with `llvm-as`, `wat2wasm` and `wasm-validate`; `abs.gen.ll` is the
LLVM back end's golden output. The bytecode emitter reproduces
`abs.sbc.txt` op for op.

## Repository layout

```text
src/frontend/       lexer, parser, AST                              [M1]
src/sema/           symbol table, diagnostics, type checker         [M2]
src/cir/            IR, builder, printer, parser, CFG, verifier,
                    reference interpreter                           [M2, M3]
src/opt/            constant folding, copy propagation, DCE         [M3]
src/backend/llvm/   LLVM IR emitter and trap guards                 [M3]
src/backend/wasm/   reg-to-stack lowering, CFG structurer, WAT      [M4]
src/backend/stackvm/  bytecode emitter and reference VM             [M4]
src/driver.py       command-line driver                             [M1]
tests/test_*.py     unit tests, one file per module
tests/harness/      differential harness, random program generator  [M4]
tests/corpus/       valid / invalid / boundary MiniLang programs
docs/               language spec, CIR spec, divergence table,
                    decisions, examples
status.md           what was fixed and built since Review 2

include/ src/**/*.cpp tools/ tests/*.cpp CMakeLists.txt
                    the C++17 port (Sanjay's rewrite), kept but
                    not developed (CI still builds it)
```

## Branches

`main` is the only branch. It holds the Python implementation, which the team
agreed to use, and keeps the C++17 port alongside it (see
`docs/migration.md`). The Python work was developed on `python-combined` and
merged into `main` on 21 Oct 2026.

## Documentation

- [`status.md`](status.md): work since Review 2, bugs found, open questions
- [`docs/minilang-spec.md`](docs/minilang-spec.md): grammar, types, diagnostics E001–E012
- [`docs/cir-spec.md`](docs/cir-spec.md): CIR design and well-formedness rules
- [`docs/divergence.md`](docs/divergence.md): cross-target semantic divergences and the observed-divergence log
- [`docs/frontend-cir-contract.md`](docs/frontend-cir-contract.md): the M1 → M2 → M3 interface
- [`docs/decisions/`](docs/decisions/): team decisions (0001: array initialisers, decided)
- [`CONTRIBUTING.md`](CONTRIBUTING.md): branch, review and commit conventions
