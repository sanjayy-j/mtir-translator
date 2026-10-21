"""MiniLang abstract syntax tree.

Module M1b (data half).  Owner: Member 1.
Status: COMPLETE (Week 3).

Every node carries the source position of its first token, because the type
checker (M2) reports against AST nodes, not tokens.  The `ty` field on
expression nodes is left as None by the parser and filled in by the semantic
analyser -- the parser never guesses a type.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


# --------------------------------------------------------------------------
# Types (syntactic; the semantic analyser maps these onto CIR types)
# --------------------------------------------------------------------------
@dataclass
class TypeNode:
    name: str                       # int | long | float | bool | void
    array_len: Optional[int] = None  # int[10] -> name='int', array_len=10

    def __str__(self) -> str:
        return self.name if self.array_len is None else f"{self.name}[{self.array_len}]"


# --------------------------------------------------------------------------
# Expressions
# --------------------------------------------------------------------------
@dataclass
class Expr:
    line: int = field(default=0, kw_only=True)
    col: int = field(default=0, kw_only=True)
    ty: Optional[str] = field(default=None, kw_only=True)  # filled by M2


@dataclass
class IntLit(Expr):
    value: int = 0


@dataclass
class FloatLit(Expr):
    value: float = 0.0


@dataclass
class BoolLit(Expr):
    value: bool = False


@dataclass
class VarRef(Expr):
    name: str = ""


@dataclass
class Unary(Expr):
    op: str = ""          # - ! ~
    operand: Optional[Expr] = None


@dataclass
class Binary(Expr):
    op: str = ""          # + - * / % == != < <= > >= && || & | ^ << >>
    lhs: Optional[Expr] = None
    rhs: Optional[Expr] = None


@dataclass
class Call(Expr):
    callee: str = ""
    args: List[Expr] = field(default_factory=list)


@dataclass
class Index(Expr):
    base: Optional[Expr] = None
    index: Optional[Expr] = None


# --------------------------------------------------------------------------
# Statements
# --------------------------------------------------------------------------
@dataclass
class Stmt:
    line: int = field(default=0, kw_only=True)
    col: int = field(default=0, kw_only=True)


@dataclass
class Let(Stmt):
    name: str = ""
    type_node: Optional[TypeNode] = None
    init: Optional[Expr] = None


@dataclass
class Assign(Stmt):
    target: Optional[Expr] = None   # VarRef or Index
    value: Optional[Expr] = None


@dataclass
class ExprStmt(Stmt):
    expr: Optional[Expr] = None


@dataclass
class Block(Stmt):
    stmts: List[Stmt] = field(default_factory=list)


@dataclass
class If(Stmt):
    cond: Optional[Expr] = None
    then_blk: Optional[Block] = None
    else_blk: Optional[Stmt] = None   # Block or If (else-if chain) or None


@dataclass
class While(Stmt):
    cond: Optional[Expr] = None
    body: Optional[Block] = None


@dataclass
class For(Stmt):
    init: Optional[Stmt] = None
    cond: Optional[Expr] = None
    step: Optional[Stmt] = None
    body: Optional[Block] = None


@dataclass
class Break(Stmt):
    pass


@dataclass
class Continue(Stmt):
    pass


@dataclass
class Return(Stmt):
    value: Optional[Expr] = None


# --------------------------------------------------------------------------
# Declarations
# --------------------------------------------------------------------------
@dataclass
class Param:
    name: str
    type_node: TypeNode


@dataclass
class FnDecl:
    name: str
    params: List[Param]
    ret_type: TypeNode
    body: Block
    line: int = 0
    col: int = 0


@dataclass
class GlobalDecl:
    name: str
    type_node: TypeNode
    init: Optional[Expr] = None
    line: int = 0
    col: int = 0


@dataclass
class Program:
    decls: List[object] = field(default_factory=list)   # FnDecl | GlobalDecl


# --------------------------------------------------------------------------
# Pretty printer -- backs `driver.py --emit=ast`
# --------------------------------------------------------------------------
def dump(node: object, indent: int = 0) -> str:
    """Render an AST as an indented tree.  Used by the golden-file tests."""
    pad = "  " * indent
    out: List[str] = []

    def line(text: str) -> None:
        out.append(pad + text)

    def kid(child: object, extra: int = 1) -> None:
        if child is not None:
            out.append(dump(child, indent + extra))

    match node:
        case Program():
            line("Program")
            for d in node.decls:
                kid(d)
        case FnDecl():
            params = ", ".join(f"{p.name}: {p.type_node}" for p in node.params)
            line(f"FnDecl {node.name}({params}) -> {node.ret_type}")
            kid(node.body)
        case GlobalDecl():
            line(f"GlobalDecl {node.name}: {node.type_node}")
            kid(node.init)
        case Block():
            line("Block")
            for s in node.stmts:
                kid(s)
        case Let():
            line(f"Let {node.name}: {node.type_node}")
            kid(node.init)
        case Assign():
            line("Assign")
            kid(node.target)
            kid(node.value)
        case ExprStmt():
            line("ExprStmt")
            kid(node.expr)
        case If():
            line("If")
            kid(node.cond)
            kid(node.then_blk)
            kid(node.else_blk)
        case While():
            line("While")
            kid(node.cond)
            kid(node.body)
        case For():
            line("For")
            kid(node.init)
            kid(node.cond)
            kid(node.step)
            kid(node.body)
        case Break():
            line("Break")
        case Continue():
            line("Continue")
        case Return():
            line("Return")
            kid(node.value)
        case IntLit():
            line(f"IntLit {node.value}")
        case FloatLit():
            line(f"FloatLit {node.value}")
        case BoolLit():
            line(f"BoolLit {str(node.value).lower()}")
        case VarRef():
            line(f"VarRef {node.name}" + (f" : {node.ty}" if node.ty else ""))
        case Unary():
            line(f"Unary {node.op}")
            kid(node.operand)
        case Binary():
            line(f"Binary {node.op}")
            kid(node.lhs)
            kid(node.rhs)
        case Call():
            line(f"Call {node.callee}")
            for a in node.args:
                kid(a)
        case Index():
            line("Index")
            kid(node.base)
            kid(node.index)
        case _:  # pragma: no cover
            line(f"<unknown node {type(node).__name__}>")

    return "\n".join(out)
