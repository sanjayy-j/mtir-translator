# Status: python-combined

**As of 20 Oct 2026** · branch `python-combined` (pushed) · last commit `c80fac8`

## At a glance

| | |
|---|---|
| Test suite | **553 tests, all pass, 0 skipped** (`python -m pytest -q`, ~45 s) |
| Four-way agreement (interp / LLVM / Wasm / VM) | **507/507** at `--opt 0` and `--opt 1` (corpus + 500 random programs; target ≥ 98%) |
| CI on GitHub | **all 3 jobs green** (C++, Python, hand-written examples) |
| Planned work since Review 2 | **10/10 done** |
| Waiting on the team | confirm decision 0001 · whether to merge into `main` · one spec question |

## What works

- **Front end**: lexer, parser, AST; type checker with all 12 diagnostics
  (E001–E012), int→long/float widening, shadowing.
- **CIR**: builder, printer/parser (exact round trip), verifier, optimiser
  (constant folding, copy propagation, DCE), reference interpreter.
- **LLVM back end**: guards for every divergence row; output compiles and runs
  through `clang`.
- **WebAssembly back end**: valid, runnable modules with structured control
  flow (`block`/`loop`/`if`) and a shadow stack, so recursion is safe.
- **Bytecode back end + VM**: the documented `PUSHI`/`LOADL`/`JZ` format;
  matches `docs/examples/abs.sbc.txt` op for op.
- **Testing**: four-way differential harness, random program generator, and
  an optimiser check (optimised vs unoptimised).

## Done since Review 2 (20 Oct 2026)

| # | Task | Result |
|---|---|---|
| 1 | Interpreter (the oracle) correctness | conversions, unsigned ops, `lshr`, `!` on bool, exact i64 division, bounds and fptosi traps, `%f` floats |
| 2 | Wasm back end correctness | was invalid for every non-void function; now validates and runs |
| 3 | Bytecode emitter ↔ VM (Review 2 defect #1) | they agree; `br.cond` false edge fixed |
| 4 | Differential harness | `tests/harness/differential.py` |
| 5 | Random program generator | `tests/harness/gen_random.py`; 507/507 agreement |
| 6 | Wasm structuring | `structurer.py` (Ramsey's algorithm); dispatch loop kept as fallback |
| 7 | Symbol table + diagnostics | `src/sema/`; E001–E012 |
| 8 | 7 skipped LLVM tests | now verified with clang; 0 skipped |
| 9 | Array initialisers (decision 0001) | option A: `let xs: int[8] = 0;` is E004 |
| 10 | Docs | README, CONTRIBUTING, CLAUDE.md, cir-spec, divergence log |

### Bugs found by the harness and fixed (all have regression tests)

1. LLVM: a `bool` array's length printed as `alloca i1, i32 true`; clang
   rejected it.
2. LLVM: output printed before a trap was lost when stdout was a pipe. Now
   `fflush(NULL)` runs before `llvm.trap`.
3. Interpreter: an out-of-range constant (`4294967296` as an `int`) wasn't
   wrapped to 0 like on every target.
4. Constant folder: same bug (it folded a division by that "0" instead of
   trapping); folded `true` came out as -1.
5. Builder: `global g: int = -5;` was rejected as "not a literal".
6. Wasm: `true` globals were stored as -1; `rem_s` doesn't trap on
   `INT_MIN % -1` without a guard.

### CI fixes

- The C++ job was red for two reasons. (a) Dropping `= 0` from `arrays.mini`
  broke 2 C++ tests that use it as the rejected example, so that edit was
  reverted. (b) 3 C++ lexer tests had been failing on `main` since 16 Sept:
  `tests/LexerTests.cpp` read a destroyed temporary inside `CHECK_EQ`. Fixed
  with a by-value helper. The lexer was fine. **`main` has the same bug.**

## Open items

### Needs the team
- **Confirm decision 0001** (array declarations take no initialiser; reading
  anything unassigned is undefined). Decided by Krishita as M1, the spec owner.
- **Merge `python-combined` into `main`?** `main` is Sanjay's C++ rewrite.
- **Tell Sanjay** about the `LexerTests.cpp` fix (commit `5b1f572`) so `main`
  can take it.

### Spec question (M1)
- An int literal that doesn't fit in `int`, e.g. `x / 4294967296` with
  `x: int`, is accepted and silently becomes 0 (now consistently on every
  target). Should it be a compile error?

### Known limitations (not bugs)
- Global arrays are not lowered (only scalar globals).
- `fptosi` is only reachable from `.cir` input (MiniLang has no cast syntax).
- The bytecode VM reuses the interpreter's arithmetic, so it checks the
  lowering, not the arithmetic.

## Commit history on this branch

```
c80fac8 docs: describe python-combined as the Python implementation
5b1f572 [M1a] lexer tests: stop reading a destroyed temporary in CHECK_EQ
86a6c46 revert arrays.mini edit: keep it as the E004 example the C++ tests use
04657b9 [M1] spec: forbid array initialisers, uninitialised reads undefined
ea699ea / 2322e60  fix: bugs found by differential fuzzing
afa307b / 44e979c  [M2] sema: symbol table, coded diagnostics E001-E012
0aaab71 / 2a02596  docs: CLAUDE.md, status.md, divergence log
b926297 [M6b] wasm: dominator-tree structurer replaces the dispatch loop
eb37638 [M5] tests: verify generated IR with clang when llvm-as is missing
6425bf2 [M8b] harness: four-way differential harness and random program generator
f98f66c [M7] stackvm: make the bytecode emitter and the VM agree
a75dcd3 [M6c] wasm: emit valid, runnable modules with a shadow stack
2d715cd [M2] interp: implement conversions, unsigned ops, bounds and fptosi traps
f83794c feat: restore the Python implementation with the Review 2 modules
```

(Pairs like `ea699ea / 2322e60` are the same commit message used twice: the
second commit carried follow-up changes.)
