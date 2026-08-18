# MiniLang — Language Specification v1

Module M1 · Owner: Member 1 · Status: v1 complete (Week 1–2)

This document is authoritative. Where an implementation and this document
disagree, the document wins until the team agrees to change it.

## 1. Lexical structure

| Class | Members |
|---|---|
| Keywords | `fn` `let` `global` `if` `else` `while` `for` `break` `continue` `return` `true` `false` `int` `long` `float` `bool` `void` |
| Identifiers | `[A-Za-z_][A-Za-z0-9_]*`, not a keyword |
| Integer literals | decimal `42`, hexadecimal `0xFF`, `_` permitted as a separator |
| Float literals | `1.5`, `1e9`, `2.5e-3` — a `.` is part of the number only when followed by a digit |
| Operators | `+ - * / % = == != < <= > >= && \|\| ! & \| ^ ~ << >> ->` |
| Punctuation | `( ) { } [ ] , ; :` |
| Comments | `// to end of line`, `/* block */` (not nested) |

Longest match wins: `<<` lexes as one token, never as two `<`.

## 2. Grammar (EBNF)

```
program     ::= { fnDecl | globalDecl }
fnDecl      ::= 'fn' IDENT '(' [ params ] ')' [ '->' type ] block
params      ::= param { ',' param }
param       ::= IDENT ':' type
globalDecl  ::= 'global' IDENT ':' type [ '=' expr ] ';'
type        ::= ('int' | 'long' | 'float' | 'bool' | 'void') [ '[' INT_LIT ']' ]

block       ::= '{' { stmt } '}'
stmt        ::= letStmt | ifStmt | whileStmt | forStmt | block
              | 'break' ';' | 'continue' ';' | 'return' [ expr ] ';'
              | simpleStmt ';'
letStmt     ::= 'let' IDENT ':' type [ '=' expr ] ';'
simpleStmt  ::= expr [ '=' expr ]                  (* assignment or expr stmt *)
ifStmt      ::= 'if' '(' expr ')' block [ 'else' ( ifStmt | block ) ]
whileStmt   ::= 'while' '(' expr ')' block
forStmt     ::= 'for' '(' [ simpleStmt ] ';' [ expr ] ';' [ simpleStmt ] ')' block

expr        ::= orExpr
orExpr      ::= andExpr   { '||' andExpr }
andExpr     ::= bitOrExpr { '&&' bitOrExpr }
bitOrExpr   ::= bitXor    { '|'  bitXor }
bitXor      ::= bitAnd    { '^'  bitAnd }
bitAnd      ::= eqExpr    { '&'  eqExpr }
eqExpr      ::= relExpr   { ('==' | '!=') relExpr }
relExpr     ::= shiftExpr { ('<' | '<=' | '>' | '>=') shiftExpr }
shiftExpr   ::= addExpr   { ('<<' | '>>') addExpr }
addExpr     ::= mulExpr   { ('+' | '-') mulExpr }
mulExpr     ::= unary     { ('*' | '/' | '%') unary }
unary       ::= ('-' | '!' | '~') unary | postfix
postfix     ::= primary { '[' expr ']' }
primary     ::= INT_LIT | FLOAT_LIT | 'true' | 'false'
              | IDENT [ '(' [ expr { ',' expr } ] ')' ]
              | '(' expr ')'
```

All binary operators are left-associative. The implementation collapses the
`orExpr … mulExpr` cascade into a single precedence-climbing routine driven by
the `BINARY_PREC` table in `src/frontend/parser.py`; the cascade above is the
normative definition.

Every branch body is a braced block, so there is no dangling-else ambiguity
and `else if` chains need no special rule.

## 3. Types

| MiniLang | CIR type | Notes |
|---|---|---|
| `int` | `i32` | two's complement, wraps on overflow |
| `long` | `i64` | two's complement, wraps on overflow |
| `float` | `f64` | IEEE-754 double |
| `bool` | `i1` | values 0 and 1 only |
| `T[n]` | `ptr` + element type | fixed length, known at compile time |
| `void` | `void` | return type only |

## 4. Conversion rules

No implicit narrowing. `int → long` and `int → float` are inserted
automatically by the type checker as explicit conversion nodes; everything
else must be written out. The parser never infers a type — `Expr.ty` is
`None` until M2 fills it in.

## 5. Statement semantics

- `break` / `continue` are legal only inside `while` or `for`.
- A function with a non-`void` return type must return on every path.
- `for` clauses are all optional; `for (;;)` is an infinite loop.
- Declarations are scoped to the enclosing block; shadowing an outer name is
  permitted, re-declaring in the same scope is not.

## 6. Arithmetic semantics

Deferred to [`divergence.md`](divergence.md), which is authoritative for every
operation where LLVM IR and WebAssembly disagree. In summary: integer overflow
wraps, shift counts are taken modulo the operand width, division by zero and
`INT_MIN / -1` trap, out-of-range float→int conversion traps, and array
indices are bounds-checked.

## 7. Diagnostics (M2 must detect all twelve)

| Code | Condition |
|---|---|
| E001 | Undeclared identifier |
| E002 | Duplicate declaration in the same scope |
| E003 | Type mismatch in a binary operation |
| E004 | Type mismatch in an assignment |
| E005 | Narrowing conversion without an explicit cast |
| E006 | Call to an undeclared function |
| E007 | Wrong number of arguments |
| E008 | Argument type mismatch |
| E009 | Missing return on some path of a non-void function |
| E010 | `return` with a value in a `void` function |
| E011 | `break` or `continue` outside a loop |
| E012 | Indexing a non-array, or a non-integer index |

## 8. Built-ins

`print_int(int)` and `print_float(float)` — the entire observable-output
surface, and therefore what the differential harness compares.
