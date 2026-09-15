# Decision needed: what does `let xs: int[8] = 0;` mean?

**Status:** OPEN — needs a team decision before M2 writes the type checker.
**Raised by:** Member 3, during the C++ migration audit.
**Affects:** M1 (grammar/spec), M2 (type checker), M3 (CIR builder), M4 (corpus).

## The construct

`tests/corpus/valid/arrays.mini` line 5:

```
let xs: int[8] = 0;
```

## Evidence

**It is syntactically legal.** `docs/minilang-spec.md` §2:

```
letStmt ::= 'let' IDENT ':' type [ '=' expr ] ';'
type    ::= ('int' | 'long' | 'float' | 'bool' | 'void') [ '[' INT_LIT ']' ]
```

The optional initialiser is not restricted by the array suffix, so the parser
accepts it — and does, today.

**It has no defined semantics.** Searching the whole specification:

- §3 maps `T[n]` to "`ptr` + element type, fixed length, known at compile time".
- §4 (Conversion rules) permits only `int → long` and `int → float`
  automatically; everything else "must be written out".
- §7 lists E004 "Type mismatch in an assignment" and E012 "Indexing a
  non-array, or a non-integer index".

**No sentence anywhere states what initialising an array from a scalar does.**
Neither "fill every element" nor "this is an error" is written down.

**The corpus does not settle it either.** `tests/corpus/valid/` is consumed by
exactly one test, `test_valid_corpus_parses`, which asserts only that the file
*parses*. So `valid/` currently means **syntactically valid**, not
semantically valid, and the file is correctly placed for the assertion that is
actually made about it. The file has been unchanged since the initial scaffold
commit `3027aef`, i.e. it predates every implementation.

**Current behaviour.** The Python reference refuses to lower it:

```
tests/corpus/valid/arrays.mini:5:5: error[E004]: an array declaration cannot
take an initialiser (array initialisers need M2's E004 check first)
```

That refusal was a placeholder chosen so the builder would not invent
semantics — it is *not* a decision, and it is why `arrays.mini` is the one
corpus program with no fixture in `tests/cir/`.

## Why it has to be settled now

The moment M2's type checker runs over the corpus, it must either accept this
line or reject it. If it rejects it, a file in `valid/` fails semantic
analysis, and the corpus needs a third bucket — `invalid/` cannot hold it,
because `invalid/` is tested for *parse* rejection and this parses fine.

## Options

### A. Forbid an initialiser on an array declaration

- `minilang-spec.md` §5 gains one sentence: an array declaration takes no
  initialiser; array elements start undefined and must be assigned.
- M2 reports E004 on `let xs: int[8] = 0;`.
- `arrays.mini` drops `= 0` — a one-token edit that keeps the file exercising
  arrays, globals and float arithmetic exactly as intended.
- M3's builder keeps its current behaviour and needs no new lowering.
- Corpus needs a `sema-invalid/` bucket only if the team wants a test that
  *this specific construct* is rejected.

**Cost:** one spec sentence, one token in one corpus file.

### B. Define the semantics and implement them

- `minilang-spec.md` gains a rule, most plausibly: `let a: T[n] = e;`
  evaluates `e` once and assigns it to every element.
- M2 type-checks `e` against the element type, not the array type.
- M3 lowers it to an `alloca` plus a fill — either `n` stores for a small
  constant `n`, or a loop. The builder currently has no loop-emitting path
  outside `while`/`for`, so this is real new work.
- `arrays.mini` stays as it is.

**Cost:** a spec rule, M2 work, and a new lowering pattern in M3.

### C. Leave it undefined and move the file

- `arrays.mini` moves out of `valid/`, and the construct stays unspecified.

**Cost:** lowest now, but it leaves a hole in the language that will be found
again — by a differential test, or by an examiner reading the grammar.

## Recommendation

**Option A.** It invents no semantics, costs one sentence and one token, keeps
the corpus program doing the job it was written for, and matches what both
implementations already do. Option B is defensible but buys a feature nothing
in the corpus actually needs — `arrays.mini` never reads `xs` before assigning
it, so the initialiser has no observable effect in the one program that uses
it.

**Whoever owns the decision:** M1 owns `minilang-spec.md`; M2 owns the type
checker and `tests/corpus/`. M3 will follow whatever is written down.

## What has *not* been done

`tests/corpus/valid/arrays.mini` has **not** been modified. No semantics have
been invented in either implementation. This note records the finding and the
options only.
