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
