# Project context: read this first

A complete briefing for anyone, a new Claude session included, picking up this
project cold. It is meant to be enough on its own; the files it points to are
for detail, not for orientation. Current as of **21 Oct 2026**, branch
`main` at `68cc730`.

## 1. What this is

BCSE307 Compiler Design, Team 5, Project A30: the **Multi-Target IR
Translator**. A small language, **MiniLang**, is compiled to one intermediate
representation, **CIR**, and from CIR to three targets that must all mean the
same thing:

```
.mini → lexer → parser → AST → CIR builder → type checker → CIR verifier
      → [optimiser: constfold, copyprop, DCE]
      → LLVM IR (.ll) | WebAssembly text (.wat) | stack bytecode (.sbc)
      ↘ CIR reference interpreter (the oracle every target is compared with)
```

The point of the project is that the targets **disagree** about edge cases:
LLVM leaves division by zero and over-wide shifts undefined, while Wasm traps
or masks. CIR fixes one meaning for each, and every back end emits guards to
realise it. `docs/divergence.md` is the table of those rows (1–9).

## 2. People and how to work with them

| Member | Area | Owns |
|---|---|---|
| M1 **Krishita Gupta** (the user) | Front end, spec, CLI | `src/frontend/`, `src/driver.py`, `docs/minilang-spec.md` |
| M2 Pragati Manot | Semantics | `src/sema/`, `src/cir/verifier.py`, `src/cir/interp.py`, `tests/corpus/` |
| M3 Sanjay J | CIR core, LLVM | rest of `src/cir/`, `src/opt/`, `src/backend/llvm/` |
| M4 Sparsh Khandelwal | Stack targets, testing | `src/backend/wasm/`, `src/backend/stackvm/`, `tests/harness/` |

Working rules from Krishita (keep to them):
- **Krishita makes all commits and pushes.** Never run `git commit` or
  `git push`. At a commit point, give copy-paste commands.
- **Commands with no `#` comments.** Krishita's zsh treats `#` as an argument.
- **One task at a time.** Finish it, report, and wait for "go" before the next.
  Krishita finds long multi-task runs too slow.
- Commit messages are module-prefixed: `[M1] …`, `[M2] sema: …`, `[M6b] wasm: …`.
- Work on `main` (the only branch). Ask before deleting files. Never
  force-push.
- Ask Krishita before making a language-design decision; don't invent
  semantics.

## 3. Branches and history

- **`main`** is the only branch and the working branch
  (`github.com/sanjayy-j/mtir-translator`). Sanjay handed the repo over to
  Krishita on 21 Oct 2026; Krishita has push access but not admin.
- History: `main` was first a C++17-only rewrite by Sanjay (15–16 Sept,
  ending `5de2ff2` "retire the Python prototype"). The Python implementation
  was developed on `python-combined` (based on `da14d31`; `f83794c` restored
  the Review 2 modules; then the 20 Oct 2026 fix-up, see `status.md`) and
  merged into `main` on 21 Oct 2026 (PR #1, then PR #4). `python-combined`
  and the `fix/` branches were then deleted.
- The **C++ tree** (`include/`, `tools/`, `src/**/*.cpp`, `tests/*.cpp`,
  `CMakeLists.txt`, `cmake/`) is still on `main`, kept from Sanjay's port.
  It is **not developed**: don't edit or delete it unless
  Krishita asks. CI still builds and tests it, and it passes. The one edit made
  to it, at Krishita's request, is the `firstKind()` fix in
  `tests/LexerTests.cpp` (§9).

Code provenance: builder, parser, cfg, ir, opt, llvm and the front end come
from Sanjay's Python history. `typecheck.py`, `verifier.py`, `interp.py`, the
Wasm emitter and the stack VM started as Krishita's Review 2 working copy;
most were rewritten on 20 Oct.

## 4. Setup and everyday commands

Python ≥ 3.10 is required (`match`); macOS `python3` is 3.9, so use
`python3.12`. The venv is `./venv` (git-ignored).

```bash
python3.12 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python -m pytest -q
python -m tests.harness.differential --random 500
python -m tests.harness.differential --random 500 --opt 1
python -m src.driver --run tests/corpus/valid/arith.mini
python -m src.driver --emit=tokens|ast|cir|ll|wat|sbc FILE.mini
python -m src.driver --opt=1 --emit=cir FILE.mini
python -m src.driver --emit=ll docs/examples/abs.cir
python -m tests.harness.gen_random 42
```

- `pytest`: **560 tests, all pass, none skipped**, about 45 s (the
  differential tests compile with clang).
- Tools: `clang` (Apple, from Xcode) runs and verifies LLVM IR, since there's
  no `lli` or `llvm-as` locally; the `wasmtime` pip package assembles and runs
  WAT. Tests that need a missing tool **skip**, not fail.
- C++ (only if touched): `cmake -S . -B <dir> && cmake --build <dir> &&
  ctest --test-dir <dir>`. Build outside the repo (e.g. the scratchpad).
- CI (`.github/workflows/ci.yml`) has 5 jobs (C++ build+ctest, Python
  pytest, validate `docs/examples/abs.ll`/`.wat`, and the 500-seed
  differential at `--opt 0` and `--opt 1`). **All green** as of `68cc730`.
  Check with `gh run list --branch main`.

`--run` prints the program's output followed by `main`'s return value.
Expected:

| Program | Output |
|---|---|
| `valid/arith` | 55 |
| `valid/control_flow` | 16 |
| `valid/recursion` | 3628800, 1 |
| `boundary/nesting` | 3 |
| `boundary/shift_edge` | 1, -2147483648, 1, 2 |
| `boundary/div_edge` | 3, then trap (exit 4) |
| `valid/arrays` | **rejected, E004, by design** (§7) |
| `docs/examples/abs` | 7 |

## 5. File map (Python)

| File | What it is |
|---|---|
| `src/driver.py` | CLI. Order: parse → **build → typecheck** (builder first so its coded errors win) → verify → optimise → run/emit. Exit 1 on error, 4 on trap. |
| `src/frontend/{lexer,parser,ast_nodes}.py` | Front end. `Expr.ty` is filled by the type checker. |
| `src/sema/typecheck.py` | Type checker, E001–E012 (`TypeError_` has `.code`). Widening int→long and int→float in operands, assignments, arguments and returns; shadowing legal. |
| `src/sema/symtab.py`, `diagnostics.py` | Chained-scope symbol table; `Diagnostic` and the code list. |
| `src/cir/ir.py` | CIR data types. `wrap_int(v, ty)`: **note `wrap_int(1, I1) == -1`** (pinned by a test; see §8). |
| `src/cir/builder.py` | AST → CIR. Locals are `alloca`+load/store; unassigned params stay in registers. |
| `src/cir/{printer,parser}.py` | `.cir` text, round-trip exact. |
| `src/cir/{cfg,verifier}.py` | Well-formedness checks. |
| `src/cir/interp.py` | **The oracle.** `run(mod) -> (value, stdout, trapped)`. Also exports `fmt_float` and the arithmetic helpers the VM reuses. |
| `src/opt/{constfold,copyprop,dce}.py` | `--opt=1`. The folder never folds something that would trap. |
| `src/backend/llvm/{emit_ll,guards,typemap}.py` | LLVM IR with guards for rows 1, 2, 3, 6, 7. |
| `src/backend/wasm/reg2stack.py` | Register → stack lowering plus peephole; used by the bytecode emitter. |
| `src/backend/wasm/structurer.py` | CFG → nested `block`/`loop`/`if` (Ramsey, "Beyond Relooper"). Raises `Irreducible` → emitter falls back to a dispatch loop. |
| `src/backend/wasm/emit_wat.py` | WAT. Shadow stack `$.sp` for allocas; `emit(mod, structured=True)`. |
| `src/backend/stackvm/emit_sbc.py` | Bytecode in the documented format of `docs/examples/abs.sbc.txt` (`PUSHI`/`LOADL`/`JZ`…, `.params`, `.global`, block labels `L_<label>`). |
| `src/backend/stackvm/vm.py` | Runs that bytecode; same triple as the interpreter. The opcode list is in its docstring. |
| `tests/harness/runners.py` | One runner per target: `run_interp`, `run_llvm` (clang), `run_wasm` (wasmtime), `run_vm`. |
| `tests/harness/differential.py` | Four-way compare. CLI + `run_all()`. With `--opt 1` it also compares against the unoptimised program (`interp.O0`). |
| `tests/harness/gen_random.py` | `generate(seed)`: deterministic, well-typed, terminating MiniLang; never reads uninitialised memory. |
| `tests/programs.py` | Shared test programs (`CORPUS`, `SOURCES`) for the back-end tests. |
| `tests/test_*.py` | One per module, plus `test_differential.py` (corpus + 40 seeds at O0/O1). |
| `tests/corpus/{valid,invalid,boundary}/` | `valid/` means *syntactically* valid (`arrays.mini` parses but is rejected). |
| `tests/cir/*.cir` | Golden CIR for the corpus. |
| `docs/minilang-spec.md` | The language. Authoritative. |
| `docs/cir-spec.md`, `docs/frontend-cir-contract.md` | The IR, and the AST→CIR contract. |
| `docs/divergence.md` | Rows 1–9; a per-target realisation table; the **observed-divergence log**. |
| `docs/decisions/0001-array-initialisers.md` | Decided (§7). |
| `status.md` | Current state, what was fixed, open items. |

## 6. Semantics every target implements (and the non-obvious bits)

- **Integers** wrap (two's complement). **Shift counts** are taken mod width.
  **Division**: `/` and `%` trap on a zero divisor and on `INT_MIN / -1`, and
  so does `INT_MIN % -1`. Division truncates toward zero.
- **`i1`** is 0 or 1 everywhere (row 5). Watch out: `wrap_int(1, I1)` returns
  -1, so code that produces `i1` constants must use `v & 1` (the interpreter,
  Wasm emitter and constant folder each do this locally).
- **Constants are wrapped to their type before use**, on every target: an i32
  constant `4294967296` *is* 0. (The interpreter and constant folder used to
  miss this.)
- **fptosi** traps on NaN or outside (MIN-1, MAX+1); MiniLang has no cast
  syntax, so only `.cir` input reaches it. **Array index** out of range traps
  (row 7).
- **`print_float`** prints C's `%f` (`107.000000`) on every target
  (`interp.fmt_float`).
- **Output before a trap is observable.** LLVM emits `fflush(NULL)` before
  `llvm.trap`, because a piped stdout would otherwise lose it.
- **Return value**: compared exactly, except the LLVM target, which only sees
  the process exit status, so it is compared **mod 256**. After a trap the
  return value is ignored.
- **Wasm specifics**: `rem_s` does *not* trap on `INT_MIN % -1` natively, so a
  guard is emitted (skipped for a constant divisor other than -1). Allocas live
  on a shadow stack (`$.sp`, top of 16 pages, one frame per call), so
  recursion is safe. Internal names start with `.` (`$.pc`, `$.fp`, `$.sp`),
  which MiniLang identifiers can't.
- **Undefined by spec** (decided 20 Oct): reading a local or array element
  before it is assigned. Targets genuinely differ (LLVM and Wasm return stale
  memory, the interpreter returns 0), and the fuzzer never generates it.

## 7. Decisions and open questions

- **Decision 0001 (DECIDED 20 Oct 2026, option A, by Krishita as M1;
  confirmed by the team, Sanjay, 21 Oct):** an array declaration takes no initialiser:
  `let xs: int[8] = 0;` is **E004** (spec §5). `tests/corpus/valid/arrays.mini`
  **keeps its `= 0` on purpose**: it is the rejected example the C++ tests
  (`SemaTests.cpp`, `DriverTests.cpp`) and `test_typecheck.py` check.
  Removing the `= 0` was tried and broke C++ CI; don't "fix" it.
- **Open:** should an int literal that doesn't fit `int` (e.g. `4294967296` in
  an int context, which silently wraps to 0) be a compile error? Spec owner M1.
- **Settled 21 Oct 2026:** `python-combined` was merged into `main` (PR #1,
  `7ab2f6b`), so the Python implementation is the main line; the C++ tree
  stays and CI on `main` is green. `main` has the `LexerTests.cpp` fix (PR #2).
  Sanjay handed the repo over to Krishita.

## 8. Gotchas for whoever edits next

- Changing **`wrap_int`'s `i1` behaviour** breaks `tests/test_opt.py`
  (`(1, Ty.I1, -1)` is pinned). Normalise `i1` locally instead.
- A literal is typed `int` by the builder until context widens it, so
  `5 >> p` with `p: long` is **E005** (the shift count narrows), and an
  all-literal expression is computed in i32. The generator avoids
  `literal >> long`.
- The **builder runs before the type checker**. Both report E-codes; the
  builder's win when both would fire.
- `reg2stack.peephole` drops `local.set x; local.get x` pairs, so the stack VM
  depends on values staying on its operand stack between instructions.
- The **VM reuses the interpreter's arithmetic**, so it checks the lowering
  (stack, jumps, frames, memory), not the arithmetic itself.
- Wasm `ELEM_SIZE` is 8 bytes for every type.

## 9. What was done on 20 Oct 2026 (summary; details in `status.md`)

1. Interpreter rewritten: conversions, unsigned ops, `lshr`, `not` on `i1`,
   exact i64 division, bounds/fptosi traps, `%f` floats, constant wrapping.
2. Wasm back end: now validates and runs (it was invalid for every non-void
   function); shadow stack; conversions; i64; `rem_s` guard; `true` globals.
3. Bytecode emitter and VM agree (Review 2 defect #1); `br.cond` false edge fixed.
4. Differential harness (four-way, plus an optimiser check at `--opt 1`).
5. Random program generator. **507/507 agree** (corpus minus `arrays.mini`,
   plus 500 seeds) at O0 and O1.
6. Wasm structurer (real `block`/`loop`/`if`); dispatch loop kept as fallback.
7. Symbol table, diagnostics, type checker with E001–E012 and widening.
8. LLVM tests verify IR with clang when `llvm-as` is missing (0 skipped).
9. Decision 0001 (option A) in spec and checker.
10. Docs: README, CONTRIBUTING, cir-spec §7, divergence.md log.

Bugs the fuzzer found and fixed: LLVM `alloca i1, i32 true`; LLVM lost output
before a trap; interpreter and constant folder didn't wrap constants; folded
`true` was -1; builder rejected `global g: int = -5;`.

C++ CI fix: `tests/LexerTests.cpp` did `CHECK_EQ(kindsOf("42")[0], …)`, which
read a destroyed temporary (`CHECK_EQ` binds `const auto &`), so it failed with
garbage values. A by-value `firstKind()` helper fixes it. The lexer itself was
fine. (`main` had the same bug; Sanjay's PR #2 fixed it there.)

## 10. If you are asked to do something next

- Run `python -m pytest -q` first; it should be all green.
- For a back-end or optimiser change, also run the harness at `--opt 0` and
  `--opt 1`. Any `DIFF` line is a bug; `interp.O0` disagreeing means the
  optimiser changed behaviour.
- Add a regression test for every bug found (`tests/test_*.py` or
  `tests/programs.py`), and log cross-target bugs in `docs/divergence.md`.
- Update `status.md` when a piece of work is done, then give Krishita the
  commit commands.
