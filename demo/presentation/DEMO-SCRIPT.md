# Review demo script

**Project A30 · Team 5 · BCSE307 Compiler Design**
Presenter: Member 3 — CIR core, optimiser, LLVM back end

Target length **8 minutes**, with two minutes of slack for questions mid-flow.
Every command below has been run and produces the output shown.

---

## Before the room fills

```powershell
cd C:\Users\sanja\mtir-translator

# 1. Confirm the build is current (about 30 seconds from clean)
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake --build build

# 2. Dry-run the whole demo once.  It must end "20 steps run, all as expected."
.\demo\run-demo.ps1 -Stage all
```

Then **clear the screen** and open two windows:

- a PowerShell window at the repository root
- an editor showing `docs/examples/abs.mini`

If the projector is small, widen the PowerShell window to ~100 columns first;
the CIR and `.wat` output is laid out for that.

> **If the build is broken and you cannot fix it in two minutes:** the demo
> still runs against any previously built `mtirc.exe`. If there is none, say
> so plainly and walk the faculty through `demo/expected/`, which holds real
> output from a real run — do not describe it as live.

---

## SECTION 1 — What the project is (~30 s)

**Say:**

> A compiler that targets three very different machines: LLVM, WebAssembly,
> and a stack bytecode we execute ourselves.
>
> The naive way to do that is three compilers. We built one front end and one
> intermediate representation — CIR — and three back ends behind it. So the
> hard work of understanding the program happens once.
>
> The interesting part is not emitting three files. It is that the three
> targets **disagree about what programs mean**. WebAssembly traps on division
> by zero; LLVM calls it undefined behaviour. CIR picks one answer, and each
> back end emits whatever guard code that answer costs on its target.

**Show nothing yet.** This is the only part you say without the terminal.

---

## SECTION 2 — Architecture (~1 min)

**Open:** `demo/presentation/ARCHITECTURE.md`, the first diagram.

**Say:**

> Source goes through the lexer and parser, then semantic analysis types it
> and inserts conversions. Then we lower to CIR.
>
> CIR is typed, register-based, three-address, with an explicit control-flow
> graph. Four properties, and one deliberate omission: **it is not SSA.**
>
> Not SSA because the thing SSA buys you is precise def-use chains for
> aggressive optimisation, and it costs you phi nodes — which every back end
> then has to destruct. At our scale that is a bad trade. We get the same
> effect from a much simpler rule the verifier enforces.

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage status
```

**Point at:** the ABSENT list.

**Say:**

> I want to be straight about this up front. `llvm-as`, `wat2wasm` and
> `wasmtime` are not installed on this laptop. So the LLVM and WebAssembly
> output you are about to see is generated and inspected, not assembled or
> executed. CI does that on every push. The stack target needs nothing
> external, so that one genuinely runs here.

*(This costs fifteen seconds and removes the single most likely challenge to
everything that follows.)*

---

## SECTION 3 — MiniLang → CIR (~2 min)

**Open:** `docs/examples/abs.mini`

```
fn abs(x: int) -> int {
    if (x < 0) { return -x; }
    return x;
}
```

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage front
```

**Point at:** the token stream, then the AST — `FnDecl abs(x: int) -> int`,
the `If`, the `Binary <`.

**Say:**

> Standard front end. The part worth a moment is what comes next.

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage cir
```

**Point at** these four lines and read them out:

```
entry:
  %t0 = icmp.slt i32 %x, 0
  br %t0 ? if.then.0 : if.end.0
```

**Say:**

> This is one basic block, called `entry`. Two instructions.
>
> `%t0 = icmp.slt i32 %x, 0` — that is three-address form: one operation, its
> operand types written down, one destination register. `slt` is *signed* less
> than; the signedness is in the opcode, not in the type, because CIR has one
> 32-bit integer type and the operation decides how to read the bits.
>
> `br %t0 ? if.then.0 : if.end.0` — the terminator. Every block has exactly
> one, and it is always last. That is well-formedness rule 1, and it is what
> makes the CFG explicit: the successors of a block are written in the block,
> so you never have to infer control flow by scanning for labels.

**Point at:** the round-trip line.

**Say:**

> And the text is not a debug dump. We parse it back, print it, and get the
> same bytes. That makes `.cir` a real interchange format — which is how a
> back end can be developed and tested with no front end in the picture.

---

## SECTION 4 — The verifier (~45 s)

*This is Member 3's work; it is worth showing, and faculty like it.*

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage verify
```

**Point at:**

```
@classify:entry: error[CIR03]: branch to undefined block 'nowhere'
```

**Say:**

> Eight well-formedness rules. This module parses — every line is fine, the
> types line up, every register is defined before use. What is wrong is a
> property of the *graph*: it branches to a block that does not exist.
>
> Objective O1 asks for at least eight detectable classes of malformed IR.
> These are they, and they are why a bug in the builder surfaces as `CIR03`
> from our compiler rather than as a confusing message from `llvm-as` later.

---

## SECTION 5 — CIR → LLVM (~1 min)

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage llvm
```

**Point at:** the `abs` function first.

**Say:**

> `%t0 = icmp slt i32 %x, 0` became `icmp slt`. `br %t0 ? then : exit` became
> `br i1 %t0, label %then, label %exit`. Nearly one-to-one — LLVM is also a
> register machine and also lets you branch to an arbitrary label, so this
> back end is close to a printer over the CFG.

**Then point at:** the `div_edge` guard block.

**Say:**

> This is where it stops being a printer. CIR says signed division by zero
> traps, and `INT_MIN / -1` traps. LLVM says both are undefined behaviour — it
> will happily optimise around them. So the back end emits the check itself:
> two comparisons, an `or`, a branch to a trap block.
>
> That is the whole thesis of the project in one screen. The semantics live in
> CIR; the cost of realising them is paid per target.

**Say, before moving on:**

> This is generated IR. It has not been through `llvm-as` on this machine.

---

## SECTION 6 — CIR → WebAssembly (~1 min)

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage wasm
```

**Point at:** `br_table $case0 $case1 $case2` and the nested `(block $caseN)`.

**Say:**

> Same CIR, and it looks nothing like the LLVM.
>
> Two problems LLVM never posed. First, WebAssembly is a *stack* machine —
> there are no registers to name, so every CIR temporary has to become either
> a push or a local. That is our register-to-stack lowering pass, and it is
> shared with the third back end.
>
> Second, and harder: WebAssembly has **no branch to an arbitrary label**.
> Control flow is only expressible as nested blocks and loops with relative
> branch depths. So an arbitrary CFG has to be re-expressed. We use a dispatch
> tower — a `br_table` over a block index, wrapped in a loop.
>
> It is not the prettiest output. A Relooper-style analysis would recover real
> `if`/`else` nesting. But the dispatch tower is correct for **any** CFG,
> including one the optimiser has just rewritten, and we chose correctness
> first. That trade-off is documented in the header.

---

## SECTION 7 — Stack bytecode and the VM (~1 min 15 s)

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage stack
```

**Point at:** the numbered listing, especially `0003  jz 8`.

**Say:**

> Third target. Same register-to-stack pass, but here labels stay flat and a
> branch is an absolute instruction address — no structuring pass at all. The
> instruction-count difference between this and the WebAssembly is a direct
> measurement of what structuring costs.

**Point at:** `7`.

**Say:**

> And this one we can execute. That is `abs(-7)` computed by our own VM, from
> MiniLang source, through CIR, through bytecode.

**Point at:** the shift results and the trap.

**Say:**

> Which matters more than a third code generator. Because the VM runs, the
> divergence table stops being a claim and becomes an observation.
>
> `1 << 32` gives 1 — the count is taken modulo the width, exactly as CIR
> specifies. And `INT_MIN / -1` traps, exit code 4, instead of silently
> wrapping. Those are two of nine documented divergences, and they are
> *tested*, not asserted.

---

## SECTION 8 — Optimiser (~1 min)

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage opt
```

**Point at:** the two block listings side by side, then the instruction counts.

**Say:**

> Constant folding, copy propagation, dead-code elimination, run to a fixed
> point. Eleven instructions collapse into four stores; 24 down to 17.
>
> But the number is not the point. The point is the last two lines: we execute
> both versions on the VM and they print the same thing. An optimisation that
> changes observable behaviour is a bug, and because one target actually runs,
> that is a test rather than a hope.
>
> One specific thing: the folder will **not** fold `7 / 0`. Folding a trapping
> division would delete a trap the program is defined to take. Same for
> anything involving NaN.

---

## SECTION 9 — Tests (~30 s)

**Run:**

```powershell
.\demo\run-demo.ps1 -Stage tests
```

**Point at:** the live totals — do not quote a number from memory, read what
is on screen.

**Say:**

> Unit tests across every subsystem, plus `ctest`, which additionally checks
> the golden files and executes the whole corpus on the VM comparing against
> what the source says it should print.
>
> Note the two skipped entries. Those are the `llvm-as` and `wat2wasm`
> checks. They report *skipped*, never *passed*, when the tool is missing —
> so a run that validated nothing can never be mistaken for one that did.

---

## Closing (~15 s)

> To summarise: one front end, one IR with semantics we actually pinned down,
> three back ends, and one of them executes so the semantics are testable.
>
> What is not built: a CIR-level reference interpreter and the four-way
> differential harness that would compare all three targets against it. Those
> are Member 2's and Member 4's next milestones, and they are recorded in
> `docs/migration.md` rather than glossed over.

---

## Fallbacks

| If | Do |
|---|---|
| A stage errors | Do not improvise a fix. Say "that's a real failure, I'll look at it after" and run the next `-Stage`. Sections are independent. |
| Terminal output scrolls too fast | Re-run that stage with `-Pause`. |
| Asked to see source | `src/cir/Builder.cpp` for lowering, `src/backend/llvm/Guards.cpp` for the trap guards, `src/backend/stackvm/VM.cpp` for the interpreter loop. |
| Asked "does the LLVM actually run?" | "Not on this machine — no `llvm-as` or `lli` installed. CI assembles it. I can show you the CI config." |
| Asked about Python | "It was a prototype. It's been migrated and deleted; `docs/migration.md` records the parity evidence taken before we removed it." |
| Running long | Skip Section 8 (optimiser). Never skip 3, 5 or 7. |
