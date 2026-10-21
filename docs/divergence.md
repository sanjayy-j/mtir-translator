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

### In the C++ implementation (`origin/main`)

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

### In the Python implementation

| Row | CIR interpreter (`src/cir/interp.py`, the oracle) | LLVM (`src/backend/llvm/`) | WebAssembly (`src/backend/wasm/emit_wat.py`) | Bytecode VM (`src/backend/stackvm/`) |
|---|---|---|---|---|
| 1, 2 | trap on a zero divisor and on `INT_MIN / -1` (also for `srem`) | `guards.guard_div` | `div_s`/`div_u`/`rem_u` trap natively; `rem_s` returns 0 for `INT_MIN % -1`, so an explicit guard is emitted unless the divisor is a constant other than -1 | shares the interpreter's arithmetic |
| 3 | count taken mod width | `guards.mask_shift_count` | native masking | shared |
| 4 | results wrapped by `wrap_int`; **constant operands are wrapped too** | no `nsw`/`nuw` | native | shared |
| 5 | `i1` is 0/1 | native `i1` | `i32` holding 0/1; `i1` constants are emitted as `value & 1` | shared |
| 6 | trap outside (INT_MIN-1, INT_MAX+1) or NaN, same bounds as the LLVM guard | `guards.guard_fptosi` | `trunc_f64_s` traps natively | shared |
| 7 | trap on any out-of-range index | `guards.guard_bounds` (allocas with a known extent) | explicit `i32.ge_u` check before every `gep` into an alloca | trap in `GEP` |
| 8 | per-call slots | `alloca` | shadow stack pointer `$.sp`, one frame per call | per-call slots |
| 9 | left to right | source order | push order | push order |

`print_float` prints C's `%f` format (`107.000000`) on every target.
The bytecode VM reuses the interpreter's arithmetic helpers, so its
differential value is in the lowering (stack discipline, jumps, frames and
memory), not in re-deriving this table.

## The asymmetry worth noticing

WebAssembly's stricter, trap-based definitions mean the guard code is almost
always on the **LLVM** side. A translator written the other way round —
treating LLVM as the reference and WebAssembly as the port — would silently
produce programs that abort in the browser and run on to garbage results
natively. Fixing the semantics in CIR rather than in either target is what
prevents that.

## Log of observed divergences

Produced by `tests/harness/differential.py` (fixed corpus + programs from
`tests/harness/gen_random.py`). Objective O4 requires ≥ 98% four-way
agreement, with every remaining disagreement recorded here with its cause and
either a fix or a documented reason it cannot be guarded.

**Current result (20 Oct 2026): 507/507 programs agree on interp, LLVM, Wasm
and VM, at both `--opt 0` and `--opt 1`** (the corpus minus
`arrays.mini`, which is rejected by design, plus 500 random seeds). With `--opt 1` the harness also checks the optimised
program against the unoptimised one.

| Date | Program | Targets disagreeing | Cause | Resolution |
|---|---|---|---|---|
| 20 Oct 2026 | `div_edge.mini` | LLVM (output missing) | stdout is buffered when piped; `llvm.trap` killed the process before the `3` printed before the trap was written | `fflush(NULL)` emitted before every `llvm.trap` |
| 20 Oct 2026 | random, bool arrays | LLVM (clang rejected IR) | array length printed with the element type: `alloca i1, i32 true` | count always printed as `i32` |
| 20 Oct 2026 | random seed 122 | interp vs all three back ends | an out-of-range constant (`4294967296` as an `int`) was not wrapped by the interpreter; every back end wraps it to 0 and traps on dividing by it | interpreter wraps constant operands (row 4) |
| 20 Oct 2026 | random seeds 122, 475 | `--opt 1` vs `--opt 0` | the constant folder used unwrapped operands, so it folded a division by an i32 `4294967296`; folded `true` was -1 | folder wraps operands first; `i1` results stay 0/1 |
| 20 Oct 2026 | Wasm `true` globals | Wasm | `wrap_int(1, i1) == -1`, so `true` was stored as -1 and `!flag` was non-zero | `i1` constants emitted as 0/1 (row 5) |

### Not guarded, by design

- **Reading an uninitialised local or array element** is undefined behaviour
  (minilang-spec.md §5, decided 20 Oct 2026). LLVM and Wasm read stale stack
  memory; the interpreter reads 0. The generator never produces it.

### Needs a spec decision

- **An int literal out of range for `int`.** Accepted, and wrapped (so
  `4294967296` is 0 in an int context), consistently on every target now.
  Whether it should be a compile-time error is open.
