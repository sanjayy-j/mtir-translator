# Checked-in CIR fixtures

These `.cir` files are **checked-in artefacts**, not hand-written sources.
Each one is the CIR lowered from the MiniLang program of the same name:

| Fixture | Source program |
|---|---|
| `arith.cir` | `tests/corpus/valid/arith.mini` |
| `control_flow.cir` | `tests/corpus/valid/control_flow.mini` |
| `recursion.cir` | `tests/corpus/valid/recursion.mini` |
| `div_edge.cir` | `tests/corpus/boundary/div_edge.mini` |
| `nesting.cir` | `tests/corpus/boundary/nesting.mini` |
| `shift_edge.cir` | `tests/corpus/boundary/shift_edge.mini` |

## Why they exist

They were introduced while the C++ front end did not yet exist, so that the
C++ CIR core, optimiser and back ends could be tested against **real
programs** — loops, nested branches, recursion, globals, and the three
`docs/divergence.md` boundary cases — rather than against hand-written
snippets chosen by the same person who wrote the code under test.

The front end exists now, and the fixtures have outlived that original
purpose. They are kept for a better one: a back end can be developed and
tested with no front end in the picture at all, which is precisely the
property the textual `.cir` format was introduced to provide
(`docs/cir-spec.md` § 1). Deleting them would quietly make every back-end
test depend on the front end being correct.

They also pin the front end: `tests/CorpusTests.cpp` checks that lowering the
`.mini` source still produces the CIR recorded here, so a change in the
builder cannot slip through unnoticed.

## Regenerating

```bash
build/mtirc --emit=cir tests/corpus/valid/arith.mini > tests/cir/arith.cir
# ...and so on for each row of the table above
```

Output is deterministic: the same input produces byte-identical CIR on every
run, which is what makes them usable as checked-in fixtures.

Note that the `.cir` format has no comment syntax, so the provenance of each
file is recorded here rather than in a header inside it.

## What the tests do with them

`tests/CorpusTests.cpp` runs each fixture through parse → verify → print →
re-parse → optimise → re-verify → LLVM emission, and checks that the
optimiser neither breaks well-formedness nor removes a trap.

`tests/WasmTests.cpp` lowers each to WebAssembly and checks the result is
structurally sound. `tests/StackVMTests.cpp` **runs** each on the reference
stack VM and compares what it printed against what the MiniLang source says
it should print — twice, unoptimised and at `-O1`, requiring the same answer
both times.

## Not included

`tests/corpus/valid/arrays.mini` has no fixture: `let xs: int[8] = 0;` has no
defined semantics in `docs/minilang-spec.md`, so the compiler refuses to lower
it. See `docs/decisions/0001-array-initialisers.md`.
