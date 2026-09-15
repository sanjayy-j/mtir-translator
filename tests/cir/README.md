# Generated CIR fixtures

These `.cir` files are **generated artefacts**, not hand-written sources. Each
one is the CIR that the Python reference implementation lowers from the
MiniLang program of the same name:

| Fixture | Source program |
|---|---|
| `arith.cir` | `tests/corpus/valid/arith.mini` |
| `control_flow.cir` | `tests/corpus/valid/control_flow.mini` |
| `recursion.cir` | `tests/corpus/valid/recursion.mini` |
| `div_edge.cir` | `tests/corpus/boundary/div_edge.mini` |
| `nesting.cir` | `tests/corpus/boundary/nesting.mini` |
| `shift_edge.cir` | `tests/corpus/boundary/shift_edge.mini` |

## Why they exist

The C++ CIR core, optimiser and LLVM back end are migrated; the C++ front end
is not. Without these fixtures the C++ tests can only exercise hand-written
snippets, which are short, regular, and chosen by the same person who wrote
the code under test. These give the C++ side **real programs** — loops,
nested branches, recursion, mutual recursion, globals, and the three
`docs/divergence.md` boundary cases — before the C++ front end exists.

They are a migration scaffold. Once the C++ front end and CIR builder land,
these tests should be re-pointed at CIR built by the C++ builder, and this
directory removed along with the Python prototype.

**Having these does not mean the front end is migrated.** It is not.

## Regenerating

```bash
python -m src.driver --emit=cir tests/corpus/valid/arith.mini > tests/cir/arith.cir
# ...and so on for each row of the table above
```

Output is deterministic: the same input produces byte-identical CIR on every
run, which is what makes them usable as checked-in fixtures.

Note that the `.cir` format has no comment syntax, so the provenance of each
file is recorded here rather than in a header inside it.

## What the C++ tests do with them

`tests/CorpusTests.cpp` runs each fixture through parse → verify →
print → re-parse → optimise → re-verify → LLVM emission, and checks that the
optimiser neither breaks well-formedness nor removes a trap.

## Not included

`tests/corpus/valid/arrays.mini` has no fixture. The Python reference refuses
to lower it because `let xs: int[8] = 0;` has no defined semantics in
`docs/minilang-spec.md`. See `docs/decisions/0001-array-initialisers.md`.
