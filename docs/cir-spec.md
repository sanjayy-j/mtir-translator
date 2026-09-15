# CIR — Common Intermediate Representation, Specification v1

Module M3 · Owner: Member 3 · Status: v1 complete; § 7 lists what is built

## 1. Design decisions and why

| Decision | Reason |
|---|---|
| Register-based three-address code, not stack-based | Register → stack lowering is a linear, mechanical pass; stack → register requires reconstructing stack heights. This choice makes the LLVM path almost a direct print and confines the hard work to one well-defined pass (M6a). |
| Typed on every instruction | No back end ever infers a type. The WebAssembly emitter reads the width off the instruction rather than deducing it, and the verifier can catch front-end bugs before they reach any emitter. |
| **Not** in SSA form | Dominance frontiers and φ-placement are a project in themselves. Mutable locals become `alloca`/`load`/`store`; LLVM's `mem2reg` recovers SSA downstream, and WebAssembly locals map straight onto CIR locals. SSA is a Review 3 extension. |
| Explicit CFG, one terminator per block | The shared structure that the LLVM back end consumes directly and the WebAssembly back end must restructure. |
| Textual `.cir` interchange format | Back ends consume text, not Python objects, so they can be developed against checked-in `.cir` files without the front end being finished. |

## 2. Types

`i1` `i32` `i64` `f64` `ptr` `void`

`i1` is the result type of every comparison. It is realised as `i1` in LLVM
and as `i32` normalised to 0/1 in WebAssembly, which has no narrower type.

## 3. Instruction set (37 opcodes, 51 mnemonics)

| Category | Opcodes |
|---|---|
| Arithmetic | `add` `sub` `mul` `sdiv` `udiv` `srem` `urem` `fadd` `fsub` `fmul` `fdiv` `neg` |
| Bitwise / shift | `and` `or` `xor` `not` `shl` `ashr` `lshr` |
| Comparison | `icmp.{eq,ne,slt,sle,sgt,sge,ult,ule,ugt,uge}` · `fcmp.{oeq,one,olt,ole,ogt,oge}` |
| Conversion | `sext` `zext` `trunc` `sitofp` `fptosi` |
| Memory | `alloca` `load` `store` `gep` |
| Call | `call` |
| Terminators | `br` `br.cond` `ret` |
| Intrinsics | `print.i32` `print.f64` `trap` |

Counting `icmp.*` and `fcmp.*` as one opcode each gives 37; spelling out every
comparison predicate gives 51 distinct mnemonics. `enum class Opcode` in
`include/mtir/cir/Opcode.h` is the authoritative set, together with the single
`OpInfo` table that carries each opcode's mnemonic, arity, category, side
effects and result rule; `tests/CIRTextTests.cpp` round-trips one example of
each shape. Only `br`, `br.cond` and `ret` are terminators — `trap` is an
intrinsic, so a block containing one still needs a terminator of its own.

## 4. Textual syntax

```
global @counter : i32 = 0

func @abs(i32 %x) -> i32 {
entry:
  %t0 = icmp.slt i32 %x, 0
  br %t0 ? then : exit
then:
  %t1 = sub i32 0, %x
  ret i32 %t1
exit:
  ret i32 %x
}
```

Registers are `%name`, globals and functions are `@name`, labels are bare
identifiers followed by `:`. The printer is deterministic — no dictionary
iteration order reaches the text — so golden-file tests are stable.

Two places where the text and the data structures deliberately differ, both
resolved in one function, `src.cir.ir.result_ty`:

- a comparison prints its **operand** type (`icmp.slt i32 %x, 0`) and always
  defines an `i1`, per rule 7 below;
- `alloca` and `gep` print the **element** type and define a `ptr`.

Both terminators are spelled `br`: `br L` is unconditional, `br %c ? L1 : L2`
is conditional. The parser tells them apart by the `?`.

**Round-trip property (M3c): implemented.** `printModule(*parseCir(t).module)
== t` for every `t` the printer can produce. `tests/CIRTextTests.cpp` pins the
print direction against `docs/examples/abs.cir`, and pins the round trip over
that file, over every instruction shape, and over optimised output; `ctest`
also runs it through the `mtirc` CLI as a golden-file check.

One limit, recorded rather than worked around: an integer literal takes the
type its instruction prints, so `call i32 @f(3)` types `3` as `i32` even where
`@f` takes an `i64`. The printed text is identical either way, so the property
above still holds, and the back ends take argument types from the callee's
signature.

## 5. Control-flow lowering patterns

```
if (c) A else B          while (c) B              for (i; c; s) B
  entry: br c ? t : f      head:  br c ? b : x      entry: <i>; br head
  t: <A>; br join          b:     <B>; br head      head:  br c ? b : x
  f: <B>; br join          x:     ...               b:     <B>; br step
  join: ...                                         step:  <s>; br head
                                                    x:     ...
```

`break` branches to the exit block, `continue` to the step block (`for`) or
the head block (`while`). Because these are the only control constructs in
MiniLang, every CFG the builder produces is reducible — which is what makes
the WebAssembly fallback path in `structurer.py` viable.

## 6. Well-formedness

1. Every basic block ends in exactly one terminator.
2. No instruction follows a terminator.
3. Every branch target names an existing block in the same function.
4. Every register is defined before it is used.
5. The entry block has no predecessors.
6. Operand types match the opcode signature.
7. The result of `icmp` / `fcmp` is `i1`.
8. A non-`void` function ends every path in `ret <ty>`.

Objective O1 requires at least eight detectable classes of malformed IR; these
are they.

All eight are implemented in `include/mtir/cir/Verifier.h` and reported as
diagnostics `CIR01`–`CIR08`, with a test per rule in `tests/CFGTests.cpp`.
Two further structural errors the rules assume away are also detected: a
function with no blocks (`CIR00`) and duplicate block labels (`CIR09`).

Rule 4 is a forward dataflow over the CFG whose meet is **intersection**, so a
register defined in only one arm of a branch is rejected at the join.
Unreachable blocks are skipped: no path reaches them, so the property is
vacuous there and reporting them would duplicate what dead-code elimination
removes. The lattice element is a bitset over the function's registers,
iterated in reverse postorder.

`verifyFunction` short-circuits after a broken graph: once a branch target is
missing, the dataflow is meaningless, and running it anyway produces a cascade
of derived errors instead of the one real one.

**Ownership note.** This document previously assigned rules 1–5 to the CFG
layer (M3) and rules 6–8 to the verifier (M2). During the C++ migration M3
implemented all eight in one place, because the LLVM back end cannot safely
emit from IR whose operand types have not been checked. M2 should review and
take ownership of the rule 6–8 section of `src/cir/Verifier.cpp`. The *source*
diagnostics E001–E012 of `minilang-spec.md` section 7 remain entirely M2's and
are not touched here.

## 7. What is built

The implementation language is C++17. A Python prototype of the same design
is still in the tree as a behavioural reference and is removed subsystem by
subsystem as each C++ counterpart reaches test parity.

| Piece | C++ | Python prototype |
|---|---|---|
| Types, arithmetic semantics | `include/mtir/cir/Type.h`, `Arith.h` | `src/cir/ir.py` |
| Values, opcodes, instructions | `Value.h`, `Opcode.h`, `Instruction.h` | `src/cir/ir.py` |
| Blocks, functions, modules | `Function.h`, `Module.h` | `src/cir/ir.py` |
| CFG queries | `CFG.h` | `src/cir/cfg.py` |
| Verifier, rules 1–8 | `Verifier.h` | rules 1–5 only, in `cfg.py` |
| Printer (Module → text) | `Printer.h` | `src/cir/printer.py` |
| Parser (text → Module) | `Parser.h` | `src/cir/parser.py` |
| Builder (AST → Module) | not started — needs the C++ AST | `src/cir/builder.py` |
| Reference interpreter | not started | never implemented |

Two C++ design points worth recording, because both are places where a
mechanical translation of the prototype would have been wrong:

- **`Arith.h` is the single definition of CIR integer arithmetic.** Signed
  overflow, over-wide shifts and `INT_MIN / -1` are all undefined behaviour in
  C++ — the same undefinedness `divergence.md` rows 1–4 exist to close on
  LLVM. Every fold therefore runs in the unsigned domain and converts back. A
  compiler that defines integer semantics must not have undefined integer
  semantics itself.
- **The terminator stays as the last element of a block's instruction vector**,
  rather than a separate field that would make "exactly one terminator" true
  by construction. Rules 1 and 2 of section 6 would become unrepresentable
  under that design, and an unrepresentable rule cannot be demonstrated as
  detected — which would take the count in section 6 from eight to six.

The builder lowers functions, parameters, scalar globals, `let`, assignment,
`if`/`else`, `while`, `for`, `break`, `continue`, `return`, calls including
recursion and mutual recursion, `print_int`/`print_float`, short-circuit `&&`
and `||`, fixed-length local arrays, and the automatic `int → long` and
`int → float` widenings. It does **not** yet lower array initialisers or
global arrays, and it emits no array bounds checks — those are realised per
target, see `divergence.md` row 7.

Mutable locals become `alloca` + `load`/`store` as § 1 requires. The one
exception is a parameter the function never assigns: it has no address, so it
stays in its incoming register. That is why `--emit=cir docs/examples/abs.mini`
produces the shape drawn in `docs/examples/abs.cir` rather than an
alloca-laden variant of it.

Since the type checker (M2) does not exist yet, the builder derives types from
declarations alone and refuses what it cannot type, raising `BuildError` with
the `Exxx` code from `minilang-spec.md` § 7 that M2 will own. It reports one
error and stops. It is not a diagnostics engine and is not meant to become
one.
