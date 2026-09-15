# Multi-Target Intermediate Representation Translator

**BCSE307 — Compiler Design** · Team [Team No.] · Project [Project ID]

Design a common intermediate representation and translate it to multiple
execution targets while preserving semantics.

Implemented in **C++17**, built with CMake. A Python prototype of the middle
end also lives in the tree; it is the behavioural reference for the parts not
yet migrated and is deleted subsystem by subsystem as each C++ counterpart
reaches test parity. See [Migration status](#migration-status).

MiniLang source → **CIR** (typed, register-based three-address IR with an
explicit CFG) → three targets:

| Target | Model | Status |
|---|---|---|
| LLVM IR (`.ll`) | register / SSA, unstructured CFG | **built in C++** |
| WebAssembly (`.wat` / `.wasm`) | structured stack machine, no `goto` | not started in C++ |
| Stack bytecode (`.sbc`) + reference VM | flat stack machine, absolute jumps | not started in C++ |

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

> **Verification status, stated precisely.** The C++ sources compile clean and
> all **164 tests pass** under MSVC 19.29 at `/W4 /permissive-`, driven
> directly by `cl`. The CMake build itself is **UNVERIFIED — CMake is not
> installed on the machine used so far**, and the code has **not been built
> with GCC or Clang**. `llvm-as` has **not been run locally**, so no generated
> LLVM IR has been checked by a real assembler outside CI. Running
> `cmake`/`ctest` once on a Linux machine is the top outstanding task.
>
> The installed GCC is MinGW.org 6.3.0, whose libstdc++ has no `<variant>`,
> `<optional>` or `<string_view>`, so it cannot build this project at all.

```bash
git clone https://github.com/sanjayy-j/mtir-translator.git
cd mtir-translator
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## What works today

```bash
# CIR round-trip: parse the checked-in module and print it back unchanged
build/mtirc --emit=cir docs/examples/abs.cir | diff - docs/examples/abs.cir && echo IDENTICAL

# LLVM IR from CIR, with the divergence guards
build/mtirc --emit=ll docs/examples/abs.cir

# constant folding, copy propagation and dead-code elimination
build/mtirc --emit=cir --opt=1 docs/examples/abs.cir

# CIR well-formedness checking: the eight rules of docs/cir-spec.md section 6
build/mtirc --emit=cir --verify docs/examples/abs.cir
```

`mtirc` does not read `.mini` yet: the C++ front end is migration phase C.
Until it lands, MiniLang compilation runs through the Python prototype.

Stages that are not migrated yet exit with status 3 and name the module, its
owner and the migration phase they are scheduled for, so the state of the
project is readable from the tool itself:

```bash
$ build/mtirc --emit=wat docs/examples/abs.cir
error: --emit=wat is not implemented yet.
       WebAssembly back end (M6c) is owned by Member 4 and is scheduled for migration phase H.
       Implemented today: --emit=cir, --emit=ll.
```

## Migration status

The project was prototyped in Python and is being migrated to C++17. Nothing
in this table is a projection — it is what the two trees contain today.

| Component | C++ | Python prototype |
|---|---|---|
| CIR types, values, opcodes, instructions | ✅ `include/mtir/cir/` | still present |
| CFG construction and queries | ✅ `CFG.h` | still present |
| CIR verifier, 8 well-formedness rules | ✅ `Verifier.h` | never implemented |
| CIR printer | ✅ byte-identical to `abs.cir` | still present |
| CIR parser, round-trip | ✅ `Parser.h` | still present |
| Optimiser: fold / copyprop / DCE | ✅ `include/mtir/opt/` | still present |
| LLVM back end + trap guards | ✅ byte-identical to the Python output | still present |
| CLI | ✅ `mtirc`, `.cir` input | `python -m src.driver`, `.mini` and `.cir` |
| Lexer, parser, AST | ❌ not started | ✅ working |
| CIR builder, AST → CIR | ❌ blocked on the C++ AST | ✅ working |
| Symbol table, type checker | ❌ not started | ❌ never implemented |
| Reg-to-stack, WebAssembly, stack VM | ❌ not started | prototype / stubs |
| Differential harness | ❌ not started | ❌ never implemented |

The Python tree is retained deliberately: it is the behavioural reference for
everything not yet migrated, and it is removed subsystem by subsystem as each
C++ counterpart reaches test parity. It is **not** the implementation.

```bash
python -m pytest -q     # the reference suite: 329 pass, 7 skip without llvm-as
```

Ownership and the C++ status of each module:

| Component | Module | Owner | C++ status |
|---|---|---|---|
| CIR data model | M3a | M3 | ✅ complete |
| CFG + queries | M3a | M3 | ✅ complete |
| CIR verifier, rules 1–8 | M3b | M3 (M2 to review 6–8) | ✅ complete |
| CIR printer | M3c | M3 | ✅ complete |
| CIR text parser | M3c | M3 | ✅ complete |
| Optimiser | M4 | M3 | ✅ three passes, to a fixed point |
| LLVM back end + guards | M5 | M3 | ✅ every opcode in cir-spec § 3 |
| CLI (`mtirc`) | M9 | M1 | ✅ `.cir` input; `.mini` needs phase C |
| CMake build + CTest | M8b | M4 | ✅ libraries, CLI, tests |
| Lexer, parser, AST | M1 | M1 | ❌ migration phase C |
| CIR builder (AST → CIR) | M3a | M3 | ❌ blocked on the C++ AST |
| Symbol table + type checker | M2 | M2 | ❌ migration phase D |
| Reg-to-stack, CFG structuring | M6a/M6b | M4 | ❌ migration phase H |
| WebAssembly back end | M6c | M4 | ❌ migration phase H |
| Stack bytecode + VM | M7 | M4 | ❌ migration phase H |
| CIR interpreter + differential harness | M8 | M2/M4 | ❌ migration phase I |

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

The C++ and Python back ends emit **byte-identical** text for this input, which
is how the migration is checked: `abs.gen.ll` is a golden file for both.

Note the difference between `abs.ll` and `abs.wat`: LLVM keeps the two-branch
CFG exactly as CIR expresses it, while WebAssembly must re-express it as a
structured `if/else` leaving its result on the operand stack. That is module
M6b, and it is why the two back ends are not simply two printers over the same
data.

## Repository layout

```text
CMakeLists.txt      C++17 build: libraries, mtirc, CTest
cmake/              golden-file and llvm-as check scripts
include/mtir/
  support/          SourceLoc, Diagnostic                          [shared]
  cir/              Type, Arith, Value, Opcode, Instruction,
                    Function, Module, CFG, Verifier,
                    Printer, Parser                                [M3]
  opt/              Pass, PassManager, ConstFold/CopyProp/DCE      [M3]
  backend/llvm/     TypeMap, Emitter, Guards, EmitLL               [M3]
src/                the .cpp files, mirroring include/mtir/
tools/mtirc/        the command-line driver                        [M1]
tests/              C++ unit tests and the golden fixtures         [M3, M4]
tests/cir/          CIR lowered from the corpus by the Python reference,
                    so the C++ side has real programs to test against
                    before the C++ front end exists                    [M3]
docs/               language spec, CIR spec, divergence table, examples

src/**/*.py         the Python prototype -- reference only, removed
tests/test_*.py     subsystem by subsystem as C++ reaches parity
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
  interface the CIR builder will be written against
- [`docs/decisions/`](docs/decisions/) — open questions needing a team decision
- [`tests/cir/README.md`](tests/cir/README.md) — the generated CIR fixtures and why they exist
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch, review and commit conventions
