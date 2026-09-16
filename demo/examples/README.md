# Demo inputs

The demo deliberately runs over the **real repository files** rather than
copies kept here. A copy would drift: the moment someone changes the corpus
or the compiler, a duplicated demo input silently stops representing the
project, and a review is the worst possible place to discover that.

What the runner uses, and why each one is in the demo:

| File | Used for |
|---|---|
| `docs/examples/abs.mini` | the main pipeline — the worked example from the Review 1 report, traced by hand through every representation |
| `docs/examples/abs.cir` | the CIR round-trip, and the back ends with no front end involved |
| `tests/corpus/valid/arith.mini` | the optimiser: four constant expressions that fold |
| `tests/corpus/boundary/shift_edge.mini` | divergence row 3 — shift counts modulo the operand width |
| `tests/corpus/boundary/div_edge.mini` | divergence row 2 — `INT_MIN / -1` traps |

## The one file that does live here

`broken.cir` is genuinely demo-only, because nothing else in the repository
is a *structurally* malformed module that still parses.

It branches to a block called `nowhere`, which the function does not contain.
Every individual line is well formed — the registers are defined before use
and the types line up — so the parser accepts it. What it violates is a
property of the control-flow graph, which is what the verifier is for:

```
mtirc --verify --emit=cir demo/examples/broken.cir
@classify:entry: error[CIR03]: branch to undefined block 'nowhere'
```

That is well-formedness rule 3 of `docs/cir-spec.md` § 6. Catching it here
matters: without the verifier the LLVM back end would emit a branch to an
undefined label, and `llvm-as` would then reject the output with a message
about LLVM rather than about this compiler.

The `.cir` format has no comment syntax, which is why this explanation is in
this file rather than in a header inside `broken.cir`.
