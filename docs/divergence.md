# Cross-Target Semantic Divergence

Module M2/M5/M6 · Owner: Member 2 · Status: drafted Week 3–4 from the LLVM
LangRef and the WebAssembly Core Specification, **before** either back end was
written.

This table is the technical core of the project's correctness claim. Each row
is an operation where LLVM IR and WebAssembly disagree, or where at least one
leaves behaviour undefined. **CIR fixes a single meaning; each back end emits
whatever guard code realises it.** Every row is also a mandatory boundary test
in `tests/corpus/boundary/`.

| # | Operation | CIR semantics | LLVM IR realisation | WebAssembly realisation |
|---|---|---|---|---|
| 1 | Signed div/rem by zero | trap | UB — emit `icmp eq` + branch to trap block | `i32.div_s` traps natively |
| 2 | `INT_MIN / -1` | trap | UB — emit explicit two-condition check | traps natively (integer overflow) |
| 3 | Shift count ≥ width | count taken mod width (`x << 33` ≡ `x << 1`) | poison — emit `and i32 %c, 31` | native masking, no extra code |
| 4 | Signed overflow on `add`/`sub`/`mul` | two's-complement wraparound | emit **without** `nsw`/`nuw` flags | native wraparound |
| 5 | Boolean representation | `i1`, values 0/1 | native `i1` | `i32` normalised via `i32.ne 0` |
| 6 | float→int out of range or NaN | trap | UB — emit range + NaN checks | `i32.trunc_f64_s` traps; `trunc_sat` deliberately **not** used |
| 7 | Array index out of bounds | trap | explicit bounds compare + branch | explicit bounds check (linear memory only traps at page granularity — not precise enough) |
| 8 | Address-taken locals and arrays | live in addressable memory | `alloca` in entry block; `mem2reg` promotes what it can | linear memory via a shadow stack pointer global (Wasm locals have no address) |
| 9 | Operand evaluation order | strictly left to right | emitted in source order, no reassociation | push order fixed by the lowering |

## Where each row is realised today

Rows 1, 2, 3, 6 and 7 are implemented on the LLVM side in
`include/mtir/backend/llvm/Guards.h`, one function per row, with a test per row
in `tests/LLVMBackendTests.cpp`. Row 4 is realised by the *absence* of `nsw`
and `nuw` on every arithmetic instruction the back end emits, which a test
checks for explicitly. Row 5 is the type mapping in `TypeMap.h`. Row 8 is
partly realised — the CIR builder puts mutable locals and arrays in `alloca`
slots — and row 9 is a property of the builder's left-to-right emission order.
Row 7 fires where the extent is recoverable from an `alloca`; global arrays
are not lowered yet. The WebAssembly column is Member 4's and is not built.

Rows 3 and 4 also constrain the compiler's *own* arithmetic, not just what it
emits. Signed overflow, over-wide shifts and `INT_MIN / -1` are undefined
behaviour in C++ as well as in LLVM, so `include/mtir/cir/Arith.h` is the one
place CIR integer arithmetic is defined, and every fold runs through it in the
unsigned domain. A compiler that defines integer semantics must not have
undefined integer semantics itself.

The optimiser respects the same table: `ConstantFoldingPass` folds a division
only when the operands prove it cannot trap, and never folds `fdiv`, `fptosi`
or an operand that is NaN, so no pass can delete a trap that rows 1, 2 and 6
require. Each of those refusals has its own test.

Python equivalents of the same guards remain in `src/backend/llvm/guards.py`
and `src/opt/constfold.py` while the prototype is still the reference for the
unmigrated parts of the pipeline.

## The asymmetry worth noticing

WebAssembly's stricter, trap-based definitions mean the guard code is almost
always on the **LLVM** side. A translator written the other way round —
treating LLVM as the reference and WebAssembly as the port — would silently
produce programs that abort in the browser and run on to garbage results
natively. Fixing the semantics in CIR rather than in either target is what
prevents that.

## Log of observed divergences

To be populated by the differential harness from Week 9. Objective O4 requires
≥ 98% four-way agreement, with every remaining disagreement recorded here with
its cause and either a fix or a documented reason it cannot be guarded.

| Date | Program | Targets disagreeing | Cause | Resolution |
|---|---|---|---|---|
| — | — | — | — | *(none yet — harness lands Week 9)* |
