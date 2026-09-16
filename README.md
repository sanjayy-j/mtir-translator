# Multi-Target Intermediate Representation Translator

**BCSE307 — Compiler Design** · Team [Team No.] · Project [Project ID]

Design a common intermediate representation and translate it to multiple
execution targets while preserving semantics.

Implemented in **C++17**, built with CMake, with no third-party
dependencies. The project was prototyped in Python; that prototype has been
migrated in full and removed. What it was, and what evidence was taken before
it was deleted, is in [`docs/migration.md`](docs/migration.md).

MiniLang source → **CIR** (typed, register-based three-address IR with an
explicit CFG) → three targets:

| Target | Model | Status |
|---|---|---|
| LLVM IR (`.ll`) | register / SSA, unstructured CFG | built |
| WebAssembly (`.wat`) | structured stack machine, no `goto` | built |
| Stack bytecode (`.sbc`) + reference VM | flat stack machine, absolute jumps | built, and **executable** |

The interesting problem is not emitting three files — it is that the targets
**disagree about what programs mean**. WebAssembly masks over-wide shift
counts and traps on division by zero; LLVM leaves both undefined. CIR fixes a
single semantics and each back end emits the guard code that realises it. See
[`docs/divergence.md`](docs/divergence.md).

## Team

| Member | Role | Owns |
|---|---|---|
| [Name] | Lead / Front end | Language spec, lexer, parser, AST, CLI (M1, M9) |
| [Name] | Requirements / Semantics | Symbol table, type checker, CIR verifier, reference interpreter (M2, M3b, M8a) |
| [Name] | CIR core / LLVM | CIR design, builder, printer/parser, optimiser, LLVM back end (M3, M4, M5) |
| [Name] | Stack targets / Testing | Reg-to-stack, CFG structuring, Wasm + bytecode back ends, harness, CI (M6, M7, M8b) |

## Building

Requirements: a C++17 compiler and CMake 3.16 or newer. `llvm-as` is optional;
it validates the generated LLVM IR, and without it that one test reports
SKIPPED rather than passing.

> **Verification status, stated precisely.** The sources compile clean and all
> **368 tests pass** under MSVC 19.29 at `/W4 /permissive-`, driven directly
> by `cl`. The CMake build itself is **UNVERIFIED — CMake is not installed on
> the machine used so far**, and the code has **not been built with GCC or
> Clang**. Neither `llvm-as` nor `wat2wasm` has been run locally, so no
> generated LLVM IR or WebAssembly has been checked by a real assembler
> outside CI. Running `cmake`/`ctest` once on a Linux machine is the top
> outstanding task.
>
> The installed GCC is MinGW.org 6.3.0, whose libstdc++ has no `<variant>`,
> `<optional>` or `<string_view>`, so it cannot build this project at all.
>
> The stack bytecode target is the exception: it needs no external tool, so
> its behaviour *is* checked locally — see "What works today" below.

```bash
git clone https://github.com/sanjayy-j/mtir-translator.git
cd mtir-translator
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## What works today

The whole pipeline, from MiniLang source to all three targets, with no
external tool involved:

```bash
# MiniLang -> tokens, AST, CIR
build/mtirc --emit=tokens tests/corpus/valid/recursion.mini
build/mtirc --emit=ast    tests/corpus/valid/recursion.mini
build/mtirc --emit=cir    tests/corpus/valid/recursion.mini

# CIR -> each of the three targets
build/mtirc --emit=ll  docs/examples/abs.cir     # LLVM IR, with the divergence guards
build/mtirc --emit=wat docs/examples/abs.cir     # WebAssembly text
build/mtirc --emit=sbc docs/examples/abs.cir     # stack bytecode, addressed listing

# run it on the reference stack VM
build/mtirc --run tests/corpus/valid/recursion.mini
# 3628800
# 1

# a trap is a defined outcome, and exits 4
build/mtirc --run tests/cir/div_edge.cir
# 3
# trap: signed division overflow

# CIR round-trip: parse the checked-in module and print it back unchanged
build/mtirc --emit=cir docs/examples/abs.cir | diff - docs/examples/abs.cir && echo IDENTICAL

# constant folding, copy propagation and dead-code elimination
build/mtirc --emit=cir --opt=1 docs/examples/abs.cir

# CIR well-formedness checking: the eight rules of docs/cir-spec.md section 6
build/mtirc --emit=cir --verify docs/examples/abs.cir
```

A back end takes `.cir` as readily as `.mini`, which is the point of having a
textual IR: each one can be developed and tested with no front end involved.

### What is actually executed, and what is only inspected

This distinction is worth being blunt about, because two of the three targets
cannot be run on the development machine at all.

| Target | Checked how |
|---|---|
| Stack bytecode | **Executed.** Every corpus program runs on the VM and its output is compared against what the source says it should print (`cmake/RunCorpusCheck.cmake`). |
| LLVM IR | Structurally, plus `llvm-as` in CI (`cmake/LlvmAsCheck.cmake`). Never executed. |
| WebAssembly | Structurally, plus `wat2wasm` and `wasm-validate` in CI (`cmake/Wat2WasmCheck.cmake`). Never executed. |

Both external-tool checks register as **skipped**, never as passed, when the
tool is absent, so a run that validated nothing cannot be mistaken for one
that did.

Because the VM runs, `docs/divergence.md` rows 1 to 7 are *observed* rather
than asserted: division by zero traps, `INT_MIN / -1` traps, `1 << 32` is `1`,
signed overflow wraps, a comparison yields 0 or 1, converting NaN traps, and a
load outside linear memory traps. `tests/StackVMTests.cpp` names the row each
test covers.

## Implementation status

| Component | Module | Owner | Status |
|---|---|---|---|
| Lexer, parser, AST | M1 | M1 | complete |
| Symbol table + type checker | M2 | M2 | complete |
| CIR data model, CFG + queries | M3a | M3 | complete |
| CIR verifier, rules 1–8 | M3b | M3 | complete |
| CIR printer and text parser | M3c | M3 | complete |
| CIR builder (AST → CIR) | M3a | M3 | complete |
| Optimiser: fold / copyprop / DCE | M4 | M3 | complete, to a fixed point |
| LLVM back end + trap guards | M5 | M3 | complete |
| Register-to-stack lowering | M6a | M4 | complete |
| CFG structuring for WebAssembly | M6b | M4 | dispatch tower; a Relooper is future work |
| WebAssembly back end | M6c | M4 | complete |
| Stack bytecode back end + VM | M7 | M4 | complete |
| CLI (`mtirc`) | M9 | M1 | complete |
| CMake build + CTest | M8b | M4 | complete, but never configured locally |
| CIR reference interpreter | M8a | M2 | **not built** |
| Four-way differential harness | M8b | M4 | **not built** |
| Random program generator | M8b | M4 | **not built** |

The three unbuilt modules were never built in either language; they are
described, with their owners, in [`docs/migration.md`](docs/migration.md).

The WebAssembly back end uses a dispatch tower rather than a Relooper-style
structural analysis. That is correct for **any** CFG, including one the
optimiser has rewritten, and it is deliberately not the prettiest possible
output; `include/mtir/backend/wasm/EmitWat.h` explains the trade-off.

## The worked example

`docs/examples/` traces one function through every representation — the same
trace as Figure 2 of the Review 1 report. The two hand-written target files
are checked by the `validate-examples` CI job, which installs LLVM and WABT
and runs:

```bash
llvm-as  docs/examples/abs.ll  -o /tmp/abs.bc
wat2wasm docs/examples/abs.wat -o /tmp/abs.wasm
wasm-validate /tmp/abs.wasm
```

Neither tool is installed on the machine the C++ migration has been done on,
so those results come from CI rather than from a local run.

`abs.ll` is the hand-written reference. `abs.gen.ll` is what the LLVM back end
emits from `abs.cir`, and its `@abs` matches the hand-written one line for
line. Regenerate it with:

```bash
build/mtirc --emit=ll docs/examples/abs.cir > docs/examples/abs.gen.ll
```

`abs.gen.ll` was byte-identical between the C++ and the Python back ends
throughout the migration, which is how the migration was checked; see
[`docs/migration.md`](docs/migration.md) for the parity evidence and for the
one place the two implementations deliberately disagreed.

Note the difference between `abs.ll` and `abs.wat`: LLVM keeps the two-branch
CFG exactly as CIR expresses it, while WebAssembly must re-express it as
structured regions. That is module M6b, and it is why the two back ends are
not simply two printers over the same data. `abs.wat` is the hand-written
structured form a Relooper would produce; what the back end emits today is a
dispatch tower, which is correct for any CFG but less pretty.

## Repository layout

```text
CMakeLists.txt      C++17 build: libraries, mtirc, CTest
cmake/              golden-file, llvm-as, wat2wasm and corpus-run checks
include/mtir/
  support/          SourceLoc, Diagnostic                          [shared]
  cir/              Type, Arith, Value, Opcode, Instruction,
                    Function, Module, CFG, Verifier,
                    Printer, Parser                                [M3]
  opt/              Pass, PassManager, ConstFold/CopyProp/DCE      [M3]
  ast/, frontend/   Token, AST, Lexer, Parser                      [M1]
  sema/             TypeInfo, Analyse                              [M2]
  backend/llvm/     TypeMap, Emitter, Guards, EmitLL               [M3]
  backend/wasm/     RegToStack, EmitWat                            [M4]
  backend/stackvm/  EmitSbc, VM                                    [M4]
  tool/             Driver -- the CLI, as a callable function      [M1]
src/                the .cpp files, mirroring include/mtir/
tools/mtirc/        the process entry point, a wrapper over Driver [M1]
tests/              C++ unit tests and the golden fixtures
tests/cir/          checked-in CIR programs, so every back end can be
                    tested with no front end involved                  [M3]
tests/corpus/       MiniLang sources: valid/ and invalid/              [M1]
docs/               language spec, CIR spec, divergence table,
                    migration record, worked example
```

The library targets enforce the dependency graph at link time. The invariant
that matters is that `mtir_cir` depends on nothing but `mtir_support` — no
AST, no semantic analysis, no back end — which is what lets a back end be
built and tested against checked-in `.cir` files with no front end involved.

## Documentation

- [`docs/minilang-spec.md`](docs/minilang-spec.md) — grammar, types, diagnostics
- [`docs/cir-spec.md`](docs/cir-spec.md) — CIR design and well-formedness rules
- [`docs/divergence.md`](docs/divergence.md) — cross-target semantic divergences
- [`docs/frontend-cir-contract.md`](docs/frontend-cir-contract.md) — the M1 → M2 → M3
  interface the CIR builder is written against
- [`docs/migration.md`](docs/migration.md) — what the Python prototype was,
  what replaced it, and the parity evidence taken before it was deleted
- [`docs/decisions/`](docs/decisions/) — decision records
- [`docs/decisions/`](docs/decisions/) — open questions needing a team decision
- [`tests/cir/README.md`](tests/cir/README.md) — the generated CIR fixtures and why they exist
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch, review and commit conventions
