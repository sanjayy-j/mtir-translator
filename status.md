# Status — python-combined

Last updated: 20 Oct 2026. Branch `python-combined`, pushed to origin.

Each subtask is fixed and committed separately, in order, because later ones
depend on earlier ones (the interpreter is the oracle the back ends are judged
against, so it goes first).

Legend: ☐ open · ◐ in progress · ☑ done · ⛔ blocked on a team decision

## Baseline (before any fixes)

- `pytest`: all pass, 7 skipped (`llvm-as` not installed).
- `--run` on the corpus matches the CLAUDE.md table.
- Nothing checks that the LLVM, Wasm or bytecode output *runs* correctly;
  `--emit` only proves that each back end prints something.

## Subtasks

### 1. ☑ Interpreter (oracle) correctness — `src/cir/interp.py` (M2)
- [x] `sext`/`zext`/`trunc`/`sitofp`/`fptosi` are unimplemented: any program
      with `let f: float = i;` crashes with "unsupported CIR op".
- [x] `lshr` behaves like `ashr` (sign bit is shifted in).
- [x] `udiv`/`urem` are computed as signed.
- [x] Unsigned compares mask to 32 bits even for `i64`.
- [x] `sdiv`/`srem` go through float division (wrong for large `i64`).
- [x] `not` on `i1` gives -2 instead of 0.
- [x] Out-of-bounds array index: a negative index silently wraps (Python list),
      a large one crashes. divergence.md row 7 says trap.
- [x] `fptosi` out of range / NaN must trap (row 6).
- [x] `print_float` prints `107.0`; the LLVM back end prints `107.000000`
      (`%f`). Pick one format for every target (the LLVM one already exists).
- [x] Add unit tests (there are none for the interpreter).

### 2. ☑ WebAssembly back end correctness — `src/backend/wasm/emit_wat.py` (M4)
- [x] Locals live at fixed addresses per function, so recursion clobbers them.
      Needs the shadow-stack pointer from divergence.md row 8.
- [x] Non-void functions end with an empty stack after the dispatch loop
      → invalid module.
- [x] Conversions are silently dropped (no code emitted).
- [x] `neg` on `i64` uses `i32.const 0`; `not` on `i1` xors with -1.
- [x] No array bounds check (row 7); no `fptosi` range check (row 6).
- [x] `main` is not exported, so nothing can run the module.
- [x] Validate and run the output for real (via the `wasmtime` Python package).

### 3. ☑ Bytecode back end + VM agree — `src/backend/stackvm/` (M4, Review 2 defect #1)
- [x] `emit_sbc.py` emits Wasm mnemonics; `vm.py` expects the documented
      `PUSHI`/`LOADL`/`JZ` format (docs/examples/abs.sbc.txt). Make the emitter
      produce the documented format.
- [x] `br.cond` only emits the true edge, so the false edge falls through to
      whichever block happens to be next.
- [x] The VM has no parameters, memory ops, division, conversions, `i64`
      wrapping, floats or traps. Extend it to cover every CIR op.
- [x] Add tests that run the corpus on the VM.

### 4. ☑ Differential harness — `tests/harness/differential.py` (M4)
- [x] Run each program four ways: interpreter, LLVM (via `clang`, since `lli`
      isn't installed), Wasm (`wasmtime`), bytecode VM. Compare
      (stdout, return value, trapped).
- [x] CLI + pytest entry point; skip a target cleanly if its tool is missing.
- [x] With `--opt 1`, also compare against the *unoptimised* program, so an
      optimiser bug can't hide behind four targets agreeing on wrong CIR.

### 5. ☑ Random program generator — `tests/harness/gen_random.py` (M4)
- [x] Generate terminating, well-typed MiniLang programs from a seed.
- [x] Fixed corpus + 500 random programs: **507/507 agree four ways**, at
      both `--opt 0` and `--opt 1` (Objective O4 target was ≥ 98%).
- [ ] Log the divergences that were found and fixed in docs/divergence.md
      (moved to subtask 10).

### 6. ☑ Wasm structuring — `src/backend/wasm/structurer.py` (M4)
- [x] Dominator-tree structurer (Ramsey, "Beyond Relooper") for reducible
      CFGs; the emitter now produces real `block`/`loop`/`if`.
- [x] Program-counter dispatch loop kept as the fallback (used automatically
      for irreducible CFGs; `emit(mod, structured=False)` forces it).

### 7. ☑ Symbol table + diagnostics — `src/sema/` (M2)
- [x] Implement `SymbolTable` (chained scopes) and `Diagnostic`.
- [x] `typecheck.py` uses them and reports coded errors E001–E012.
- [x] Fixed: `long + int` / `float * int` were rejected (spec §4 says they
      widen), and shadowing an outer name was rejected (spec §5 allows it).

### 8. ☑ The 7 skipped LLVM tests
- [x] They now use `llvm-as` if it's installed, otherwise Apple `clang -x ir`.
      All 66 LLVM tests run; 0 skipped.

### 9. ☑ `arrays.mini` rejected with E004
- Decided 20 Oct 2026 by Krishita (M1): **option A**, so array declarations
  take no initialiser (E004), and reading anything unassigned is undefined.
  Spec §5, type checker and decision 0001 updated.
- `arrays.mini` keeps its `= 0` and stays the rejected example. Dropping it
  broke two C++ tests in CI, so that edit was reverted (Krishita's choice).

### 10. ☑ Docs
- [x] CLAUDE.md: branch state, setup (`wasmtime`), harness command, known
      open items.
- [x] docs/divergence.md: per-row table for the Python back ends, and the
      observed-divergence log (5 entries) plus the two unguarded cases.

## Log

- 20 Oct 2026: baseline taken, status.md created.
- 20 Oct 2026: **1 done** (`2d715cd`). Interpreter rewritten around one trap
  exception; 26 new tests in `tests/test_interp.py`.
- 20 Oct 2026: **2 done** (`a75dcd3`). The old WAT failed validation for every
  non-void function. Two more bugs turned up and were fixed while testing:
  `true` globals were stored as -1, and `rem_s` does not trap on
  `INT_MIN % -1` in Wasm. Added `wasmtime` to requirements.txt (tests skip
  without it). New: `tests/harness/runners.py`, `tests/test_wasm_backend.py`.
- 20 Oct 2026: **3 done** (`f98f66c`). Emitted bytecode for abs now matches
  `docs/examples/abs.sbc.txt` op for op. New: `tests/test_stackvm.py`.
- 20 Oct 2026: **4, 5, 7 done** (not committed yet: Krishita commits from now
  on). The harness and fuzzer found five real bugs, all fixed with tests:
  1. LLVM: a `bool` array's length printed as `alloca i1, i32 true`, which
     clang rejects.
  2. LLVM: output printed before a trap was lost when stdout is a pipe
     (stdio buffer never flushed). Now `fflush(NULL)` runs before
     `llvm.trap`.
  3. Interpreter: an out-of-range constant (`4294967296` used as an `int`)
     wasn't wrapped, while every back end wraps it to 0.
  4. Constant folder: same bug, so `x / 4294967296` folded instead of
     trapping; also folded `true` came out as -1.
  5. Builder: `global g: int = -5;` was rejected as "not a literal".
- **Question for the team (M1 owns the spec):** an int literal that doesn't
  fit in `int`, e.g. `4294967296` in `x / 4294967296` where `x: int`, is
  accepted and silently becomes 0. It is consistent across targets now, but
  the spec doesn't say whether it should be an error.
- Note: reading an uninitialised local or array element is undefined in
  practice (LLVM and Wasm see stale stack memory, the interpreter sees 0).
  The spec doesn't say; the decision 0001 draft says "elements start
  undefined". The fuzzer avoids it.
- 20 Oct 2026: **8 done** (not committed). `tests/test_llvm_backend.py` only.
- 20 Oct 2026: **6 done** (not committed). New `structurer.py`, plus
  `tests/test_structurer.py`; `emit_wat.py` uses it by default. The fuzzing
  harness still gives 507/507 at `--opt 0` and `--opt 1` with structured Wasm.
  Also: the `rem_s` guard is no longer emitted for a constant divisor.

## Remaining

- All 10 subtasks are done. Team to confirm decision 0001.
- `python-combined` pushed to origin on 20 Oct 2026.
- 20 Oct 2026: **10 done** (not committed): CLAUDE.md, docs/divergence.md.
- 20 Oct 2026: CI on `python-combined` was red on the C++ job, for two reasons:
  1. The `arrays.mini` edit broke 2 C++ tests that use it as the rejected
     example, so the edit was reverted (Krishita's choice).
  2. 3 C++ lexer tests had been failing on `main` since 16 Sept. The lexer
     was fine; `tests/LexerTests.cpp` did `CHECK_EQ(kindsOf("42")[0], …)`,
     and `CHECK_EQ` binds `const auto &` to a reference into a temporary
     vector, so it read freed memory (0 on a Mac, 157 on CI). Fixed with a
     by-value `firstKind()` helper. This edits the C++ tree, at Krishita's
     request. All 4 C++ test suites pass locally.
- 20 Oct 2026: CI is green on `python-combined` (all 3 jobs). Docs updated
  to match the branch: README (Python is the implementation; team names,
  setup, usage, harness), CONTRIBUTING (pre-push checks, Python ownership),
  cir-spec §7 table, tests/cir/README, the builder docstring (it claimed to
  read `Expr.ty` via a `_ty_of` that doesn't exist), and the CI job name (no
  longer "retired").
