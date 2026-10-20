# Migrating the prototype to C++17

The project was prototyped in Python and is implemented in C++17. This
records what that migration replaced, what evidence was taken before the
prototype was removed, and what was never built in either language.

It exists because the prototype is gone from the tree: once the Python is
deleted, the only place the parity argument can live is here.

## Why the prototype existed at all

The Python tree was a behavioural reference, not a first attempt. Writing the
CIR data structures, the printer and the parser in a language with no build
step made it cheap to settle the textual format and the divergence table
before any of it had to be expressed in C++. The C++ implementation was then
written against that reference rather than against the specification alone,
which is why the parity checks below could be made byte-for-byte.

## What replaced what

| Prototype | C++ |
| --- | --- |
| `src/frontend/lexer.py` | `src/frontend/Lexer.cpp` |
| `src/frontend/ast_nodes.py` | `include/mtir/ast/AST.h`, `src/frontend/AST.cpp` |
| `src/frontend/parser.py` | `src/frontend/Parser.cpp` |
| `src/sema/typecheck.py`, `symtab.py`, `diagnostics.py` | `src/sema/Analyse.cpp`, `TypeInfo.cpp`, `src/support/Diagnostic.cpp` |
| `src/cir/ir.py` | `include/mtir/cir/{Type,Arith,Value,Opcode,Instruction,Function,Module}.h` |
| `src/cir/cfg.py` | `src/cir/CFG.cpp` |
| `src/cir/verifier.py` | `src/cir/Verifier.cpp` |
| `src/cir/printer.py` | `src/cir/Printer.cpp` |
| `src/cir/parser.py` | `src/cir/Parser.cpp` |
| `src/cir/builder.py` | `src/cir/Builder.cpp` |
| `src/opt/constfold.py`, `copyprop.py`, `dce.py` | `src/opt/*.cpp` |
| `src/backend/llvm/emit_ll.py`, `guards.py`, `typemap.py` | `src/backend/llvm/*.cpp` |
| `src/backend/wasm/reg2stack.py` | `src/backend/wasm/RegToStack.cpp` |
| `src/driver.py` | `src/tool/Driver.cpp`, `tools/mtirc/main.cpp` |
| `tests/test_*.py` | `tests/*Tests.cpp` |

Four prototype files had no behaviour to migrate, because they raised
`NotImplementedError`: `src/backend/wasm/emit_wat.py`,
`src/backend/wasm/structurer.py`, `src/backend/stackvm/emit_sbc.py` and
`src/backend/stackvm/vm.py`. The WebAssembly back end, the stack bytecode
back end and the reference VM are therefore new work in C++ rather than
migrations, and the design decisions behind them are documented in their own
headers.

## The evidence taken before deletion

Measured on MSVC 19.29.30159 at `/W4 /permissive-`, with the prototype still
in the tree, immediately before it was removed.

- **CIR parity.** `--emit=cir` output is byte-identical between the two
  implementations for every corpus program: `arith.mini`,
  `control_flow.mini`, `recursion.mini` and `docs/examples/abs.mini`.
  `arrays.mini` is rejected by both with `E004` at the same position (the
  message text differs; see below).
- **LLVM parity.** `--emit=ll` output is byte-identical for every `.cir` in
  `tests/cir/` and for `docs/examples/abs.cir`, with one exception recorded
  below.
- **Goldens.** `docs/examples/abs.cir` and `docs/examples/abs.gen.ll` are
  reproduced byte-for-byte by `mtirc`.
- **Test suites.** The prototype's suite passed 329 and skipped 7; the C++
  suite passes 368 and fails 0. Every prototype test file has a C++
  counterpart, and `tests/StackVMTests.cpp`, `tests/WasmTests.cpp`,
  `tests/SemaTests.cpp`, `tests/CIRCoreTests.cpp` and `tests/CorpusTests.cpp`
  cover ground the prototype never did.

### Two places the implementations do not agree

Both are deliberate, and both were checked rather than assumed to be
harmless.

**Guard label numbering.** For `tests/cir/div_edge.cir`, the prototype emits
`trap.div.6` paired with `cont.div.7`, and the C++ emits `trap.div.6` paired
with `cont.div.6`. The prototype allocates a fresh number per label; the C++
allocates one per guard, so a matched pair shares an index and is easier to
follow by eye. This is the only difference in the entire LLVM corpus, it is
cosmetic, and `llvm-as` accepts both. No golden file depends on it.

**Diagnostic text for E004.** The prototype said the array initialiser check
needed another member's work first; the C++ points at
`docs/decisions/0001-array-initialisers.md`, which is where the question was
actually settled. The code and the source position are the same.

## What was never built, in either language

These were planned and are still unbuilt. They are listed here because
deleting their prototype stubs would otherwise delete the only record that
they are owed.

| Module | What it is | Owner | Planned |
| --- | --- | --- | --- |
| M8a | CIR reference interpreter — executes a CIR module directly, as the oracle the back ends are compared against | Member 2 | Week 8 |
| M8b | Four-way differential harness — one program through the interpreter, `lli`, `wasmtime` and the stack VM, requiring agreement | Member 4 | Week 9 |
| M8b | Random MiniLang program generator, for a fuzzing layer over the fixed corpus | Member 4 | Week 11 |

The stack VM in `src/backend/stackvm/VM.cpp` is **not** the M8a interpreter.
It executes bytecode, not CIR, so it is one of the things the harness would
compare rather than the oracle it would compare them against. It does,
however, make `docs/divergence.md` rows 1 to 7 executable today, which is the
part of the differential objective that could be reached without the
interpreter.

## What still cannot be checked here

The development machine used for this migration has MSVC but no CMake, no
`llvm-as`, no `lli`, no `wat2wasm` and no `wasmtime`. So:

- `CMakeLists.txt` is maintained but has never been configured locally; the
  results above come from a direct `cl.exe` build.
- The generated LLVM IR and WebAssembly have been checked structurally and by
  eye, never assembled. CI installs LLVM and WABT and runs
  `cmake/LlvmAsCheck.cmake` and `cmake/Wat2WasmCheck.cmake` over generated
  artefacts; those tests register as *skipped*, never as passed, when the
  tool is absent, so a run that validated nothing cannot be mistaken for one
  that did.
- Only the stack VM target can be executed locally, which is why the corpus
  behaviour checks in `cmake/RunCorpusCheck.cmake` go through it.
