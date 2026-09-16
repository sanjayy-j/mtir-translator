# Architecture

**Multi-Target Intermediate Representation Translator** · Project A30 · Team 5

Written for a reader who has not seen the code. Everything below describes
what is in the repository today; what is *not* built is listed in § 12 rather
than left to inference.

---

## 1. High-level shape

```
                       ┌──────────────────┐
                       │  MiniLang source │   .mini
                       └────────┬─────────┘
                                │
              ┌─────────────────▼──────────────────┐
              │            FRONT END               │
              │  Lexer  →  Parser / AST            │   M1
              │  Semantic analysis (types, Conv)   │   M2
              └─────────────────┬──────────────────┘
                                │  typed AST
              ┌─────────────────▼──────────────────┐
              │           CIR BUILDER              │   M3a
              └─────────────────┬──────────────────┘
                                │
                     ┌──────────▼──────────┐
                     │        C I R        │   .cir  ← the contract
                     │  typed · register   │
                     │  three-address      │
                     │  explicit CFG       │
                     │  NOT SSA            │
                     └──────────┬──────────┘
                                │
                     ┌──────────▼──────────┐
                     │  VERIFIER (8 rules) │   M3b
                     └──────────┬──────────┘
                                │
                     ┌──────────▼──────────┐
                     │  OPTIMISER          │   M4
                     │  fold · copyprop    │
                     │  DCE, to fixpoint   │
                     └──────────┬──────────┘
                                │
         ┌──────────────────────┼──────────────────────┐
         │                      │                      │
┌────────▼────────┐   ┌─────────▼─────────┐   ┌────────▼────────┐
│  LLVM BACK END  │   │  WEBASSEMBLY      │   │  STACK BYTECODE │
│      M5         │   │  BACK END   M6c   │   │  BACK END   M7  │
│                 │   │                   │   │                 │
│ register →      │   │ reg→stack (M6a)   │   │ reg→stack (M6a) │
│ register        │   │ + CFG structuring │   │ + flat labels,  │
│ + trap guards   │   │   (dispatch tower)│   │   absolute jumps│
└────────┬────────┘   └─────────┬─────────┘   └────────┬────────┘
         │                      │                      │
      .ll file               .wat file              .sbc listing
         │                      │                      │
   (llvm-as, CI)          (wat2wasm, CI)      ┌────────▼────────┐
                                              │  REFERENCE VM   │
                                              │       M7        │
                                              └────────┬────────┘
                                                       │
                                                    result
```

The register-to-stack pass (M6a) sits under **two** back ends. That is why
adding the third target cost so little: only control flow and frames differ
between WebAssembly and the bytecode.

---

## 2. Module responsibilities

One static library per subsystem, so the dependency graph is enforced at link
time rather than by convention.

```
mtir_support  ──────────────────────────────►  (nothing)
mtir_cir      ──────────────────────────────►  mtir_support
mtir_frontend ──────────────────────────────►  mtir_cir
mtir_sema     ──────────────────────────────►  mtir_frontend
mtir_cirgen   ──────────────────────────────►  mtir_sema, mtir_cir
mtir_opt      ──────────────────────────────►  mtir_cir
mtir_llvm     ──────────────────────────────►  mtir_cir
mtir_wasm     ──────────────────────────────►  mtir_cir
mtir_stackvm  ──────────────────────────────►  mtir_cir, mtir_wasm
mtir_driver   ──────────────────────────────►  all of the above
```

**The invariant that matters:** `mtir_cir` depends on nothing but
`mtir_support`. No AST, no semantic analysis, no back end. That is what lets a
back end be built and tested against checked-in `.cir` files with no front end
involved — and it is enforced by the linker, not by a comment.

| Library | Owner | Contents |
|---|---|---|
| `mtir_support` | shared | `SourceLoc`, `Diagnostic` |
| `mtir_frontend` | M1 | `Lexer`, `Parser`, `AST` |
| `mtir_sema` | M2 | `TypeInfo`, `Analyse` — symbol table, type checking, `Conv` insertion |
| `mtir_cir` | M3 | `Type`, `Arith`, `Value`, `Opcode`, `Instruction`, `Function`, `Module`, `CFG`, `Verifier`, `Printer`, `Parser` |
| `mtir_cirgen` | M3 | `Builder` — typed AST → CIR |
| `mtir_opt` | M3 | `PassManager` + three passes |
| `mtir_llvm` | M3 | `TypeMap`, `Guards`, `EmitLL` |
| `mtir_wasm` | M4 | `RegToStack`, `EmitWat` |
| `mtir_stackvm` | M4 | `EmitSbc`, `VM` |
| `mtir_driver` | M1 | `Driver` — the CLI, as a callable function |

---

## 3. CIR design

Four properties and one deliberate omission.

**Typed.** Every instruction prints exactly one type. For most that is the
result type; for a comparison it is the *operand* type, because the result is
always `i1`; for `alloca`/`gep` it is the element type.

```
%t0 = icmp.slt i32 %x, 0      ← i32 is what is compared; %t0 is i1
```

**Register-based, three-address.** One operation, named operands, one
destination:

```
%t1 = sub i32 0, %x
```

Signedness lives in the **opcode**, not the type — `sdiv`/`udiv`,
`icmp.slt`/`icmp.ult`. One 32-bit integer type; the operation decides how to
read the bits. This is the LLVM convention and it keeps the type lattice tiny.

**Explicit CFG.** Successors are written in the block that jumps to them, so
control flow never has to be inferred by scanning for labels:

```
br %t0 ? if.then.0 : if.end.0
```

**Not SSA — and this is a decision, not an omission.** SSA buys precise
def-use chains for aggressive optimisation, and costs phi nodes that every
back end then has to destruct. At this scale that is a bad trade. Instead the
verifier enforces *define-before-use along every path*, which is the property
the optimiser actually needs; and constant propagation only substitutes
registers with exactly one definition in the function, where the same
reasoning that makes SSA sound applies anyway.

**51 opcodes** in nine families: arithmetic, bitwise/shift, integer compare,
float compare, conversion, memory, call, intrinsics, terminators. A single
`constexpr` table describes all of them, with a `static_assert` pinning the
order, so opcode knowledge cannot drift across five files.

**The textual form is a real format, not a debug dump.** It round-trips:

```
printModule(parseCir(t)) == t        (byte for byte)
```

That is what makes a `.cir` file a testable artefact and what lets the back
ends be developed independently of the front end.

---

## 4. CFG design

A function is a vector of basic blocks; a block is a label plus a vector of
instructions. Edges are derived from terminators rather than stored, so they
cannot go stale.

```
   entry
     │  br %t0 ? if.then.0 : if.end.0
     ├───────────────┐
     ▼               ▼
  if.then.0      if.end.0
     │ ret           │ ret
     ▼               ▼
   (exit)          (exit)
```

Queries provided: successors, predecessors, reverse postorder, reachability.

**Reducibility.** MiniLang has no `goto`, so every CFG the builder produces
is reducible. The WebAssembly back end does not *rely* on that — see § 8 —
but it is why a Relooper would be viable future work.

---

## 5. Verification — eight well-formedness rules

Objective O1 asks for at least eight detectable classes of malformed IR.

| # | Rule | Diagnostic |
|---|---|---|
| 1 | Every block ends in exactly one terminator | `CIR01` |
| 2 | No instruction follows a terminator | `CIR02` |
| 3 | Every branch target names a block in the same function | `CIR03` |
| 4 | Every register is defined before use | `CIR04` |
| 5 | The entry block has no predecessors | `CIR05` |
| 6 | Operand types match the opcode signature | `CIR06` |
| 7 | The result of `icmp`/`fcmp` is `i1` | `CIR07` |
| 8 | A non-`void` function ends every path in `ret <ty>` | `CIR08` |

Plus two structural errors the rules assume away: a function with no blocks
(`CIR00`) and duplicate labels (`CIR09`).

Rule 4 is a forward dataflow over the CFG whose meet is **intersection**, so a
register defined in only one arm of a branch is rejected at the join.
Unreachable blocks are skipped — the property is vacuous there, and reporting
them would duplicate what DCE removes.

Verification short-circuits after a broken graph: once a branch target is
missing the dataflow is meaningless, and running it anyway produces a cascade
of derived errors instead of the one real one.

---

## 6. Optimisation

Three passes, run to a fixed point by a pass manager:

```
   ┌──────────────────┐
   │ Constant folding │──┐
   └──────────────────┘  │
   ┌──────────────────┐  │   repeat until nothing
   │ Copy propagation │──┼──  changes (capped at 8
   └──────────────────┘  │   rounds as a safety net)
   ┌──────────────────┐  │
   │ Dead-code elim.  │──┘
   └──────────────────┘
```

Measured on `tests/corpus/valid/arith.mini`: **24 → 17 instructions**, and the
program still prints `55`.

**What is deliberately *not* folded, and why.** An optimisation may not turn a
program that traps into one that does not, nor silently adopt the host's
arithmetic where CIR has defined its own:

- `sdiv`/`srem`/`udiv`/`urem` fold only when the operands prove the operation
  cannot trap — non-zero divisor, and not `INT_MIN / -1`.
- `fdiv` is never folded: CIR does not fix what float division by zero means.
- Nothing folds when an operand is NaN.
- Every integer result goes through `wrapInt`, every shift count through
  `maskShift`, so folding cannot disagree with § 7.

Copy propagation targets arithmetic with an identity operand (`add %x, 0`,
`mul %x, 1`, …), which is what actually produces copies in three-address code;
there is no move instruction to chase.

---

## 7. Cross-target divergence — the central problem

The three targets genuinely disagree about what programs mean. CIR fixes one
answer; each back end pays whatever that costs on its target.

| # | Situation | CIR says | LLVM needs | WebAssembly needs |
|---|---|---|---|---|
| 1 | Signed div/rem by zero | trap | explicit compare + branch to trap | nothing — traps natively |
| 2 | `INT_MIN / -1` | trap | explicit two-condition check | nothing — traps natively |
| 3 | Shift count ≥ width | count mod width | `and i32 %c, 31` — else poison | nothing — native |
| 4 | Signed overflow | wraps | omit `nsw`/`nuw` | nothing — native |
| 5 | Booleans | `i1`, 0/1 | native `i1` | `i32` normalised |
| 6 | float→int out of range / NaN | trap | range + NaN checks | native trap |
| 7 | Array index out of bounds | trap | bounds compare + branch | bounds check |
| 8 | Address-taken locals | addressable memory | `alloca` in entry block | shadow stack in linear memory |
| 9 | Operand evaluation order | strictly left to right | emit in source order | push order |

**Note the asymmetry.** WebAssembly's stricter, trap-based definitions mean
the guard code is almost always on the *LLVM* side. A translator built the
other way round — LLVM first, WebAssembly bolted on — would have discovered
this late and painfully.

Rows 1–7 are **executed** by the stack VM, so they are observations rather
than claims.

---

## 8. LLVM back end (M5)

Closest to a printer over the CFG, because LLVM is also a register machine and
also permits a branch to any label.

```
  %t0 = icmp.slt i32 %x, 0          →   %t0 = icmp slt i32 %x, 0
  br %t0 ? then : exit              →   br i1 %t0, label %then, label %exit
```

Type mapping is direct: `i1→i1`, `i32→i32`, `i64→i64`, `f64→double`,
`ptr→ptr`.

Where it stops being a printer is the guards. `sdiv` becomes:

```
  %g.1 = icmp eq i32 %b, 0
  %g.2 = icmp eq i32 %a, -2147483648
  %g.3 = icmp eq i32 %b, -1
  %g.4 = and i1 %g.2, %g.3
  %g.5 = or i1 %g.1, %g.4
  br i1 %g.5, label %trap.div.6, label %cont.div.6
trap.div.6:
  call void @llvm.trap()
  unreachable
cont.div.6:
  %t0 = sdiv i32 %a, %b
```

The emitter **refuses rather than guesses**: a conversion whose result type is
wrong is reported as a diagnostic instead of emitted as text `llvm-as` would
reject.

---

## 9. WebAssembly back end (M6c)

Two problems LLVM never posed.

**Register → stack.** No registers to name. A three-address instruction
assumes its operands are addressable at any time; a stack machine can only
reach the top. Each value is either consumed immediately or parked in a local.
Parking everything is always correct but wasteful, so a peephole removes a
`local.set`/`local.get` pair when the local is read exactly once in the block
and is not live out. Measured on `abs`: **14 → 10 instructions, 28.6%**.

**Control flow.** WebAssembly has *no* branch to an arbitrary label — only
nested `block`/`loop`/`if` with relative depths. An arbitrary CFG must be
re-expressed. We use a dispatch tower:

```
(local $__block i32)
(block $exit
  (loop $dispatch
    (block $case2
      (block $case1
        (block $case0
          local.get $__block
          br_table $case0 $case1 $case2))
        ;; CIR block 0 — sets $__block, br $dispatch, or returns
      )
      ;; CIR block 1
    )
    ;; CIR block 2
  ))
```

Falling out of `(block $caseN)` lands exactly at CIR block *N*.

**This is deliberately not the prettiest output.** A Relooper would recover
real `if`/`loop` nesting for the reducible CFGs MiniLang produces. The
dispatch tower is correct for **any** CFG, reducible or not — including one
the optimiser has just rewritten — and correctness was chosen first. The
trade-off is recorded in `include/mtir/backend/wasm/EmitWat.h`.

Address-taken locals live in a shadow stack in linear memory, behind a mutable
`$__sp` global, with a frame opened in the prologue and restored before every
return.

---

## 10. Stack bytecode back end (M7)

The cheap third target: it reuses the entire register-to-stack pass and
differs only in control flow and frames.

```
  .func abs(i32) -> i32
    .frame 0
    .local 0 $x
    0000  local.get 0  ;; $x
    0001  i32.const 0
    0002  i32.lt_s
    0003  jz 8              ← absolute address, no structuring needed
    0004  i32.const 0
    0005  local.get 0  ;; $x
    0006  i32.sub
    0007  return
    0008  local.get 0  ;; $x
    0009  return
  .end
```

Labels stay flat and a branch is an absolute index. **The instruction-count
difference against the WebAssembly output is a direct measurement of what
structuring costs** — which is the comparison the project exists to make.

Frames are simpler here than in WebAssembly: the VM owns the call stack, so a
function declares `.frame <bytes>` and the VM allocates and releases it. Same
layout, simpler mechanism.

---

## 11. The reference VM (M7)

A fetch-decode-execute loop over the bytecode. Mnemonics are decoded to an
enum once before execution, so the inner loop is a switch rather than string
comparison.

```
  ┌─────────┐   ┌─────────┐   ┌──────────┐   ┌────────────┐
  │  fetch  │──►│ decode  │──►│ execute  │──►│  operand   │
  │   pc    │   │  once   │   │          │   │   stack    │
  └─────────┘   └─────────┘   └────┬─────┘   └────────────┘
       ▲                           │
       └───────────────────────────┘
                                   │  linear memory (64 KiB)
                                   │  call frames grow down
                                   ▼
                              trap / result / output
```

**This matters more than a third code generator.** `llvm-as`, `lli`,
`wat2wasm` and `wasmtime` are absent from the development machine, so the
other two targets can be inspected but never run. The VM is where § 7 stops
being a table and becomes a set of observations: a trap on division by zero
is either seen or it is not.

Integer arithmetic is computed in the **unsigned domain** and converted back,
because signed overflow is undefined in C++17 — the implementation must not
rely on the very behaviour it is trying to define.

A step limit and a call-depth limit turn a non-terminating program into a
reported trap rather than a hung test run.

---

## 12. Testing

```
   368 unit tests  ──────────────  every subsystem
        │
        ├── ctest ── golden_cir_roundtrip    printModule(parseCir(t)) == t
        ├── ctest ── golden_llvm             abs.gen.ll byte-identical
        ├── ctest ── llvm_as (SKIPPED here)  real assembler, in CI
        ├── ctest ── wat2wasm (SKIPPED here) real assembler, in CI
        └── ctest ── run_corpus_on_the_vm    executed, output compared
```

Largest suites: `stack` 35, `sema` 25, `verifier` 25, `builder` 25, `driver`
24, `wasm` 23, `parser` 23, `vm` 22, `llvm` 19 (+15 divergence).

Two properties worth naming:

- **The external-tool checks report *skipped*, never *passed*, when the tool
  is missing.** A run that validated nothing cannot be mistaken for one that
  did.
- **The corpus is executed before and after optimisation** and must produce
  identical output — the strongest correctness statement the project can make
  on a machine with no LLVM or WebAssembly runtime.

---

## 13. What is not built

Listed explicitly so it is not inferred from silence. Details, with owners, in
`docs/migration.md`.

| Module | What it would be | Owner |
|---|---|---|
| M8a | CIR reference interpreter — executes CIR directly, as the oracle the back ends are compared against | Member 2 |
| M8b | Four-way differential harness — one program through the interpreter, `lli`, `wasmtime` and the VM, requiring agreement | Member 4 |
| M8b | Random MiniLang program generator, for fuzzing over the fixed corpus | Member 4 |

The stack VM is **not** the M8a interpreter: it executes bytecode, not CIR, so
it is one of the things the harness would compare rather than the oracle it
would compare them against.

Also future work: a Relooper-style structuring pass for WebAssembly (§ 9).

Array bounds checking (row 7) *is* implemented. The LLVM back end records the
extent of each `alloca` with a constant count and emits a guard on every `gep`
off it, using an unsigned compare so that a negative index is caught by the
same test:

```
  %xs = alloca i32, i32 4
  %g.1 = icmp uge i32 %i, 4
  br i1 %g.1, label %trap.bounds.2, label %cont.bounds.2
```

---

## 14. Adding a fourth target

The measure of whether the architecture works.

1. Write an emitter against `mtir_cir` only.
2. Reuse `RegToStack` if the target is a stack machine; skip it if not.
3. Consult `docs/divergence.md` and emit whatever guards the target lacks.
4. Register `--emit=<stage>` in `src/tool/Driver.cpp`.
5. Add tests against the checked-in `.cir` fixtures — **no front end needed**.

Nothing in the front end, semantic analysis, CIR, verifier or optimiser
changes. That is the return on having an IR at all.
