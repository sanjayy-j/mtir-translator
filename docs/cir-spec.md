# CIR — Common Intermediate Representation, Specification v1

Module M3 · Owner: Member 3 · Status: v1 complete (Week 2)

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

## 3. Instruction set (32 opcodes)

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

**Round-trip property (M3c, Week 6):** `print_module(parse_cir(t)) == t` for
every `t` the printer can produce. `tests/test_cir_printer.py` already pins
the print direction against `docs/examples/abs.cir`.

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

## 6. Well-formedness (enforced by M3b)

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
