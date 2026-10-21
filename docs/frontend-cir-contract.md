# Front end → CIR integration contract

**Owners:** M1 (AST), M2 (semantic analysis), M3 (CIR builder).
**Status:** proposed by M3 — needs M1 and M2 sign-off before Phase C starts.

Nothing in this document is implemented. It exists so that M1 and M2 can build
against a fixed interface instead of M3 guessing at one, and so that M3 can
start the builder the day both sides land.

```
ast::Program ──► sema::analyse ──► sema::TypeInfo ──► cir::build ──► cir::Module
    [M1]              [M2]             [M2]              [M3]
```

## 1. The entry point

```cpp
// include/mtir/cir/Builder.h                                        [M3]
namespace mtir::cir {

struct BuildResult {
  std::optional<Module>   module;
  support::Diagnostics    diagnostics;
  bool ok() const { return module.has_value(); }
};

BuildResult build(const ast::Program &program, const sema::TypeInfo &types);

} // namespace mtir::cir
```

Two parameters, not one fused `sema::TypedProgram`. The AST belongs to M1 and
the analysis belongs to M2; fusing them would make one member's type own the
other's. The builder takes both by `const&` and mutates neither.

**Precondition:** `build` is only called on a program M2 has already accepted.
It is not a second line of defence against ill-typed input, and it does not
re-report E001–E012. If M2 produced errors, the caller stops before the
builder.

## 2. `ast::Conv` — the node M2 inserts and M3 lowers

`minilang-spec.md` §4 already assigns conversion insertion to the type
checker: *"`int → long` and `int → float` are inserted automatically by the
type checker as explicit conversion nodes"*. That sentence requires a node
that the parser never produces, so M1 must define it even though M1 never
constructs one.

```cpp
// include/mtir/ast/AST.h                                            [M1]
/// An explicit type conversion.  The parser NEVER produces one of these;
/// semantic analysis inserts them where minilang-spec.md section 4 requires
/// an automatic widening.  Making the conversion a node rather than an
/// implicit rule is what keeps the CIR builder from having to re-derive
/// types.
class Conv final : public Expr {
public:
  const Expr &operand() const;   ///< the expression being converted
  // `from` is types.typeOf(operand()); `to` is types.typeOf(*this).
  // The node does not store either, so the two can never disagree with
  // TypeInfo.
};
```

**What `Conv` represents.** Exactly one widening, applied to exactly one
operand. Not a cast expression — MiniLang has no cast syntax — and not a
narrowing: §4 says narrowing "must be written out", and there is no syntax to
write it out, so a narrowing conversion is E005 and never reaches the builder.

**Source type / destination type.** Deliberately *not* stored on the node.
`from = types.typeOf(conv.operand())` and `to = types.typeOf(conv)`. Storing
them would create a second source of truth that could drift from `TypeInfo`.
If M1 prefers to store them, they must be the same values `TypeInfo` reports,
and M3 will read `TypeInfo` regardless.

**Wrapped expression.** One operand, always non-null. A `Conv` with no operand
is malformed AST, not a builder error.

**Source location.** `Conv` carries the location of the expression it wraps,
so a diagnostic that points at a conversion points at real source text rather
than at a node the programmer never wrote.

**Which conversions may appear.** Only the two of §4:

| from | to | CIR opcode M3 emits |
|---|---|---|
| `int` (`i32`) | `long` (`i64`) | `sext` |
| `int` (`i32`) | `float` (`f64`) | `sitofp` |

**How M3 lowers it.** `build` visits `Conv` by lowering the operand to a
value, then emitting one CIR instruction with a fresh temporary:

```
%t = sext i64 <operand>        # for int -> long
%t = sitofp f64 <operand>      # for int -> float
```

There is one shortcut, and it is the only one: when the operand lowers to an
integer *literal*, the builder folds the conversion into the constant rather
than emitting an instruction, so `let n: long = 1;` produces `1` typed `i64`
and not `sext i64 1`. This matches what the Python prototype does today and
keeps `--opt=0` output readable.

If a `Conv` appears with any other from/to pair, the builder reports an
internal error and refuses to build — it does not guess.

## 3. What M3 needs from M1 (the AST)

**REQUIRED.**

1. **Const traversal.** Either an `ast::ConstVisitor` with a `visit` overload
   per node, or a `NodeKind` tag plus `isa`/`cast` helpers. M1 chooses; M3
   only needs to dispatch on node kind and reach children. The builder never
   mutates a node, so no non-const accessor is required.

2. **The node set.** 24 kinds, being the 23 the Python AST already has plus
   `Conv`:

   `Program · FnDecl · GlobalDecl · Param · TypeNode · Block · Let · Assign ·
   ExprStmt · If · While · For · Break · Continue · Return · IntLit ·
   FloatLit · BoolLit · VarRef · Unary · Binary · Call · Index · Conv`

3. **The accessors the builder reads.** These are exactly the fields the
   Python builder touches, so the list is derived rather than wished for:

   | Node | Accessors |
   |---|---|
   | `Program` | `decls()` |
   | `FnDecl` | `name() params() returnType() body()` |
   | `GlobalDecl` | `name() type() init()` |
   | `Param` | `name() type()` |
   | `TypeNode` | `name() arrayLen()` |
   | `Block` | `statements()` |
   | `Let` | `name() type() init()` |
   | `Assign` | `target() value()` |
   | `ExprStmt` | `expr()` |
   | `If` | `cond() thenBlock() elseBlock()` |
   | `While` | `cond() body()` |
   | `For` | `init() cond() step() body()` |
   | `Return` | `value()` |
   | `IntLit` / `FloatLit` / `BoolLit` | `value()` |
   | `VarRef` | `name()` |
   | `Unary` | `op() operand()` |
   | `Binary` | `op() lhs() rhs()` |
   | `Call` | `callee() args()` |
   | `Index` | `base() index()` |
   | `Conv` | `operand()` |

   Optional children (`Let::init`, `Return::value`, `If::elseBlock`, all three
   `For` clauses) are nullable or `std::optional`; M1 chooses which.

4. **Source locations.** `SourceLoc` on every `Stmt` and `Expr`, using
   `mtir::support::SourceLoc`. The builder attaches them to diagnostics it
   raises about lowering, and M2 needs them anyway.

**OPTIONAL.** A pretty-printer (`ast::dump`) — useful for `--emit=ast`, which
is M1's own CLI stage, and not something the builder calls.

**NOT REQUIRED.** Parent pointers, mutable accessors, node identity/IDs, or
any visitor that can rewrite the tree. M2 rewrites the AST to insert `Conv`
before the builder ever sees it.

## 4. What M3 needs from M2 (`TypeInfo`)

Deliberately four members. The Python builder is larger than this because M2
did not exist and it had to re-derive types itself; **none of that logic
should be ported.**

```cpp
// include/mtir/sema/TypeInfo.h                                      [M2]
namespace mtir::sema {

enum class SymbolKind { Parameter, Local, Global, Function };

struct Symbol {
  SymbolKind kind;
  std::string name;
  cir::Ty type;                    ///< element type for an array
  std::optional<int> arrayLength;  ///< set iff the declaration was T[n]
};

class TypeInfo {
public:
  /// (1) The CIR type of any expression, after conversion insertion.
  cir::Ty typeOf(const ast::Expr &e) const;

  /// (2) The declaration a name refers to.  Never null for a program M2
  ///     accepted -- an unresolved name is E001 and never reaches the builder.
  const Symbol *resolve(const ast::VarRef &ref) const;

  /// (3) The callee's signature.  Return type plus parameter types, in order.
  struct Signature { cir::Ty returnType; std::vector<cir::Ty> parameters; };
  const Signature *signatureOf(const ast::Call &call) const;

  /// (4) Whether this parameter is ever assigned in its function body.
  bool isAssigned(const ast::Param &param) const;
};

} // namespace mtir::sema
```

**Why each one is required, and nothing else is:**

1. **`typeOf`** replaces the prototype's `type_of`, `unify`, and most of
   `coerce` in one accessor. It is what lets the builder pick `add` vs `fadd`,
   `icmp.slt` vs `fcmp.olt`, and the right width, without owning any typing
   rules.

2. **`resolve`** tells the builder whether a name is a parameter in a
   register, a local in an `alloca`, a global, or a function — which is the
   whole of variable lowering. `arrayLength` is what the `alloca` needs, and
   what the LLVM back end later recovers for the row-7 bounds check.

3. **`signatureOf`** types the arguments and the result of a `call`. The
   builder must not infer these from the argument expressions: a literal `3`
   passed to an `i64` parameter has to be emitted as `i64 3`.

4. **`isAssigned`** is the one flow fact the builder needs. A parameter that
   is never assigned stays in its incoming register instead of getting an
   `alloca`, which is why `abs` lowers to the three blocks of
   `docs/examples/abs.cir` rather than an alloca-laden variant. M2 already
   computes assignment information for its own checks, so exposing it costs
   nothing; if M2 would rather not, M3 can compute it with a read-only visitor
   over the function body, and this member drops out.

## 5. What M3 must NOT ask M2 for

Listed explicitly, because an over-large `TypeInfo` is the easiest way for
this interface to rot:

- **A constant evaluator.** Folding is `src/opt/ConstantFolding.cpp`'s job,
  after lowering.
- **A diagnostic-emission API.** M2 *returns* `support::Diagnostics`. The
  builder never asks M2 to report something on its behalf.
- **A walkable symbol table or scope chain.** The builder wants resolved
  answers (`resolve`, `signatureOf`), not a structure to traverse. Scope
  handling is entirely M2's, and the builder maintains its own register/slot
  map, which is a different thing.
- **Coercion helpers.** By the time the builder runs, conversions are `Conv`
  nodes. There is nothing left to coerce.
- **"Does this function return on every path?"** That is E009 and M2 reports
  it. The builder assumes it holds.
- **Anything about CIR.** `TypeInfo` refers to `cir::Ty` because that is the
  type vocabulary `minilang-spec.md` §3 already maps MiniLang onto, but M2
  must not build, inspect or verify CIR. That is the layering `mtir_cir`
  enforces at link time.

## 6. Diagnostics

One shared type, `mtir::support::Diagnostic`, already in the tree.

| Phase | Codes | Location field used |
|---|---|---|
| Lexer / parser (M1) | syntax errors | `file`, `loc` |
| Semantic analysis (M2) | E001–E012 | `file`, `loc` |
| CIR builder (M3) | internal lowering failures only | `file`, `loc` |
| CIR verifier (M3) | CIR00–CIR09 | `function`, `block`, `instrIndex` |
| LLVM back end (M3) | LLVM01 | `function`, `block` |

The builder's own diagnostics should be rare by construction: on a program M2
accepted, the only things it can report are constructs it does not yet lower
(today: global arrays). Those are marked as such, not dressed up as user
errors.

## 7. What is blocked on this

M3 cannot write `src/cir/Builder.cpp` until §3 and §4 exist as headers. Every
other M3 component — CIR core, CFG, verifier, printer, parser, optimiser, LLVM
back end — is already migrated and needs none of this; they are driven from
`.cir` text, which is why `tests/cir/` exists.

## 8. Open questions for M1 and M2

1. **Visitor or `NodeKind` tag?** M3 works with either. M1 decides.
2. **Does `Conv` store `from`/`to`, or are they read from `TypeInfo`?** M3
   prefers `TypeInfo` as the single source of truth; either is workable.
3. **Does M2 expose `isAssigned`, or does M3 compute it?** Either; §4 item 4.
4. **`let xs: int[8] = 0;`** — see `docs/decisions/0001-array-initialisers.md`.
   This one genuinely blocks M2, not just M3.
