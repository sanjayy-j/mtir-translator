# Contribution Log

Updated weekly. Every entry must name verifiable evidence — a commit, a file,
or a command whose output can be reproduced. Per the review guidelines, an
individual mark can be reduced where a claimed contribution has no evidence.

| Week | Member | Task | Status | Evidence |
|---|---|---|---|---|
| 1 | All | Kickoff; scope agreed; roles assigned | Complete | Meeting record, repo initialised |
| 1 | Member 1 | MiniLang EBNF grammar and type rules v1 | Complete | `docs/minilang-spec.md` |
| 1 | Member 2 | Toolchain survey; setup script verified on 4 machines | Complete | `scripts/setup.sh` |
| 2 | Member 3 | CIR types, opcode set, textual syntax v1 | Complete | `docs/cir-spec.md` |
| 2 | Member 4 | Register-to-stack lowering design; CI workflow | Complete | `src/backend/wasm/reg2stack.py`, `.github/workflows/ci.yml` |
| 2–3 | Member 1 | Lexer with position tracking | Complete | `src/frontend/lexer.py`, 11 tests |
| 3 | Member 2 | Tool survey and gap analysis | Complete | Review 1 report §4.2 |
| 3 | Member 3 | Hand-worked `abs` example: CIR, LLVM IR, `.wat`, bytecode | Complete | `docs/examples/`, validated by `llvm-as` / `wat2wasm` |
| 3 | Member 1 | Recursive-descent parser and AST | Complete | `src/frontend/parser.py`, 25 tests |
| 3 | Member 3 | CIR data structures and printer | Complete | `src/cir/`, output byte-identical to `docs/examples/abs.cir` |
| 4 | Member 2 | Divergence table from both specifications | Complete | `docs/divergence.md` |
| 4 | Member 4 | Test corpus (12 programs), peephole rule, CI green | Complete | `tests/`, 64 tests passing |
| 4 | All | Review 1 report and presentation | Complete | Review 1 submission |

## Meeting record

| Week | Attendees | Agenda | Decisions |
|---|---|---|---|
| 1 | All 4 | Scope, source language, target selection | MiniLang fixed; LLVM IR + WebAssembly mandatory, stack bytecode third; roles assigned |
| 2 | All 4 | CIR design review | Register-based, typed, non-SSA; textual `.cir` adopted as the inter-module contract |
| 2 | M2, M4 | Toolchain and CI | Versions pinned; GitHub Actions; setup script must pass before any merge |
| 3 | All 4 | Module interfaces and ownership | Interfaces frozen; backup owners M1↔M2, M3↔M4; one review + green CI per PR |
| 3 | M2, M3 | LLVM vs WebAssembly semantics | CIR defines semantics authoritatively; guard code per target; every divergence becomes a boundary test |
| 4 | All 4 | Review 1 preparation | Report sections allocated; presentation split per guidelines §3 |
