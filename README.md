# Multi-Target Intermediate Representation Translator

**BCSE307 — Compiler Design** · Team [Team No.] · Project [Project ID]

Design a common intermediate representation and translate it to multiple
execution targets while preserving semantics.

MiniLang source → **CIR** (typed, register-based three-address IR with an
explicit CFG) → three targets:

| Target | Model | Status |
|---|---|---|
| LLVM IR (`.ll`) | register / SSA, unstructured CFG | Weeks 6–7 |
| WebAssembly (`.wat` / `.wasm`) | structured stack machine, no `goto` | Weeks 7–8 |
| Stack bytecode (`.sbc`) + reference VM | flat stack machine, absolute jumps | Week 9 |

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

## Quick start

```bash
git clone https://github.com/<org>/mtir-translator.git
cd mtir-translator
bash scripts/setup.sh          # checks toolchain versions, runs a smoke test
python -m pytest -q            # 64 tests
```

## What works today (Review 1)

```bash
# Lexer: token stream with line:col positions
python -m src.driver --emit=tokens docs/examples/abs.mini

# Parser: full grammar, indented AST dump
python -m src.driver --emit=ast docs/examples/abs.mini

# CIR data structures + printer, on the hand-built abs() module
python -m src.driver --demo-cir

# ...and it is byte-identical to the checked-in golden file
python -m src.driver --demo-cir | diff - docs/examples/abs.cir && echo IDENTICAL
```

Stages that are not built yet exit with status 3 and name the module, its
owner and the week it is scheduled for, so the state of the project is
readable from the tool itself:

```bash
$ python -m src.driver --emit=wat docs/examples/abs.mini
error: --emit=wat is not implemented yet.
       WebAssembly back end (M6c) is owned by Member 4 and is scheduled for Weeks 7-8.
```

| Component | Module | Owner | Status |
|---|---|---|---|
| Lexer | M1a | M1 | ✅ complete |
| Parser + AST | M1b | M1 | ✅ complete |
| CIR data structures + printer | M3a/M3c | M3 | ✅ complete |
| Reg-to-stack peephole rule | M6a | M4 | ✅ implemented + tested |
| CLI driver | M9 | M1 | ✅ complete |
| Test corpus + CI | M8b | M4 | ✅ 12 programs, CI green |
| Symbol table + type checker | M2 | M2 | 🕐 Week 4 |
| CIR builder | M3a | M3 | 🕐 Week 5 |
| CIR verifier + text parser | M3b/M3c | M2/M3 | 🕐 Weeks 5–6 |
| LLVM back end | M5 | M3 | 🕐 Weeks 6–7 |
| Reg-to-stack lowering | M6a | M4 | 🕐 Weeks 6–7 |
| CFG structuring | M6b | M4 | 🕐 Week 8 |
| WebAssembly back end | M6c | M4 | 🕐 Weeks 7–8 |
| Stack bytecode + VM | M7 | M4 | 🕐 Week 9 |
| CIR interpreter + differential harness | M8 | M2/M4 | 🕐 Weeks 8–9 |
| Optimiser | M4 | M3 | 🕐 Week 10 |

## The worked example

`docs/examples/` traces one function through every representation — the same
trace as Figure 2 of the Review 1 report. Both target files have been
mechanically validated:

```bash
llvm-as  docs/examples/abs.ll  -o /tmp/abs.bc     # exit 0
wat2wasm docs/examples/abs.wat -o /tmp/abs.wasm   # exit 0, module validates
```

Note the difference between `abs.ll` and `abs.wat`: LLVM keeps the two-branch
CFG exactly as CIR expresses it, while WebAssembly must re-express it as a
structured `if/else` leaving its result on the operand stack. That is module
M6b, and it is why the two back ends are not simply two printers over the same
data.

## Repository layout

```
docs/          language spec, CIR spec, divergence table, worked examples
src/frontend/  lexer, parser, AST                                    [M1]
src/sema/      symbol table, type checker, diagnostics               [M2]
src/cir/       IR data structures, builder, verifier, printer, interp[M3]
src/opt/       constant folding, copy propagation, DCE               [M3]
src/backend/   llvm/ · wasm/ · stackvm/                              [M3, M4]
tests/         unit tests, corpus (valid/invalid/boundary), harness  [M4]
```

## Documentation

- [`docs/minilang-spec.md`](docs/minilang-spec.md) — grammar, types, diagnostics
- [`docs/cir-spec.md`](docs/cir-spec.md) — CIR design and well-formedness rules
- [`docs/divergence.md`](docs/divergence.md) — cross-target semantic divergences
- [`docs/contribution-log.md`](docs/contribution-log.md) — weekly log and meeting record
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — branch, review and commit conventions
