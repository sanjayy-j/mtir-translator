"""AST -> CIR lowering.

Module M3a.  Owner: Member 3.
Status: IMPLEMENTED for the MiniLang subset listed below; see "Not yet built".

What this pass does
-------------------
One ``Function`` per ``FnDecl``.  Expressions are flattened to three-address
form with a fresh-register allocator, statements are split into basic blocks,
and every block is closed with exactly one terminator.  The control-flow
shapes are the ones fixed in docs/cir-spec.md section 5.

Storage model (docs/cir-spec.md, "Not in SSA form")
--------------------------------------------------
Mutable locals live in memory: an ``alloca`` in the entry block plus
``load``/``store`` at each use.  That is what keeps the IR out of SSA without
needing phi nodes, and LLVM's ``mem2reg`` recovers SSA downstream.

One exception, and it is deliberate: **a parameter that the function never
assigns stays in its incoming register.**  It has no address, nothing can
observe it in memory, and the saving is a store and a load per use.  This is
also why the builder reproduces the shape of the hand-drawn abs() module in
docs/examples/abs.cir rather than an alloca-laden variant of it.

Typing, and the interface to M2
-------------------------------
The type checker (M2, ``src/sema/typecheck.py``) is not built yet, so
``Expr.ty`` is ``None`` on every node the parser produces.  Rather than
duplicate M2, this builder derives types only from what the *declarations*
already state -- parameter types, ``let`` types, global types and function
return types -- and propagates them bottom-up.  It reports nothing: when it
cannot type an expression it raises ``BuildError`` naming the diagnostic code
from docs/minilang-spec.md section 7 that M2 will own.  When M2 lands and
annotates the AST, this file reads ``Expr.ty`` in preference to its own
derivation (see ``_ty_of``) and the fallback simply stops firing.

Not yet built (Review 1 scope, honestly stated)
-----------------------------------------------
  * array ``let`` with an initialiser -- needs M2's E004 first;
  * global arrays -- only scalar globals are lowered;
  * array bounds checks -- realised per target, see docs/divergence.md row 7.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional

from ..frontend import ast_nodes as ast
from .ir import (BasicBlock, ConstFloat, ConstInt, Function, Global, GlobalRef,
                 Instr, Module, Param, Reg, Ty, Value, wrap_int)


class BuildError(Exception):
    """The builder cannot lower this AST.

    Carries the MiniLang diagnostic code (docs/minilang-spec.md section 7)
    that the type checker will report properly once M2 exists, so the message
    stays useful rather than becoming an internal assertion.
    """

    def __init__(self, code: str, message: str, node: object = None) -> None:
        line = getattr(node, "line", 0)
        col = getattr(node, "col", 0)
        where = f"{line}:{col}: " if line else ""
        super().__init__(f"{where}error[{code}]: {message}")
        self.code = code


# --------------------------------------------------------------------------
# Types
# --------------------------------------------------------------------------
_TYPE_NAMES = {"int": Ty.I32, "long": Ty.I64, "float": Ty.F64,
               "bool": Ty.I1, "void": Ty.VOID}

_INT_TYPES = (Ty.I1, Ty.I32, Ty.I64)

# int -> long and int -> float are the only conversions MiniLang inserts
# automatically (docs/minilang-spec.md section 4).  Everything else is an
# error the type checker reports as E005.
_WIDEN = {(Ty.I32, Ty.I64): "sext", (Ty.I32, Ty.F64): "sitofp",
          (Ty.I1, Ty.I32): "zext", (Ty.I1, Ty.I64): "zext"}

_SIGNED_CMP = {"==": "eq", "!=": "ne", "<": "slt", "<=": "sle",
               ">": "sgt", ">=": "sge"}
_FLOAT_CMP = {"==": "oeq", "!=": "one", "<": "olt", "<=": "ole",
              ">": "ogt", ">=": "oge"}
_INT_BINOP = {"+": "add", "-": "sub", "*": "mul", "/": "sdiv", "%": "srem",
              "&": "and", "|": "or", "^": "xor", "<<": "shl", ">>": "ashr"}
_FLOAT_BINOP = {"+": "fadd", "-": "fsub", "*": "fmul", "/": "fdiv"}

BUILTINS = {"print_int": (Ty.VOID, Ty.I32, "print.i32"),
            "print_float": (Ty.VOID, Ty.F64, "print.f64")}


def ty_of_node(node: Optional[ast.TypeNode]) -> Ty:
    """Map a syntactic type onto a CIR type.  An array decays to ``ptr``."""
    if node is None:
        return Ty.VOID
    if node.array_len is not None:
        return Ty.PTR
    if node.name not in _TYPE_NAMES:
        raise BuildError("E003", f"unknown type {node.name!r}")
    return _TYPE_NAMES[node.name]


def elem_ty_of_node(node: ast.TypeNode) -> Ty:
    """The element type of an array declaration, or the type itself."""
    if node.name not in _TYPE_NAMES:
        raise BuildError("E003", f"unknown type {node.name!r}")
    return _TYPE_NAMES[node.name]


def zero_of(ty: Ty) -> Value:
    return ConstFloat(0.0) if ty is Ty.F64 else ConstInt(0, ty)


# --------------------------------------------------------------------------
# Symbols
# --------------------------------------------------------------------------
@dataclass
class _Var:
    """A name in scope.

    kind is 'reg' for an unassigned parameter, 'slot' for an alloca'd local or
    parameter, or 'global' for a module-level variable.  ``ty`` is the element
    type for arrays, whose ``addr`` is the base pointer.
    """
    ty: Ty
    kind: str
    addr: Optional[Value] = None     # slot / global / array base
    reg: Optional[Reg] = None        # 'reg' kind only
    array_len: Optional[int] = None


class _FnBuilder:
    def __init__(self, decl: ast.FnDecl, sigs: Dict[str, tuple],
                 globals_: Dict[str, _Var]) -> None:
        self.decl = decl
        self.sigs = sigs
        self.ret_ty = ty_of_node(decl.ret_type)
        self.fn = Function(decl.name, [], self.ret_ty, [])
        self.scopes: List[Dict[str, _Var]] = [dict(globals_)]
        self.blocks: List[BasicBlock] = []
        self.cur: Optional[BasicBlock] = None
        self.entry: Optional[BasicBlock] = None
        self.n_tmp = 0
        self.n_label = 0
        self.taken: set[str] = set()
        # (continue target, break target) for the innermost enclosing loop.
        self.loops: List[tuple[str, str]] = []

    # -- naming ------------------------------------------------------------
    def tmp(self, ty: Ty) -> Reg:
        name = f"t{self.n_tmp}"
        self.n_tmp += 1
        self.taken.add(name)
        return Reg(name, ty)

    def unique(self, base: str) -> str:
        """A register name derived from a source name, uniquified on collision.

        Shadowing is legal in MiniLang but CIR register names are per-function,
        so an inner ``x`` becomes ``x.1``.
        """
        name, n = base, 1
        while name in self.taken:
            name, n = f"{base}.{n}", n + 1
        self.taken.add(name)
        return name

    def label(self, base: str) -> str:
        name = f"{base}.{self.n_label}"
        self.n_label += 1
        return name

    # -- blocks ------------------------------------------------------------
    def open_block(self, label: str) -> BasicBlock:
        block = BasicBlock(label)
        self.blocks.append(block)
        self.cur = block
        return block

    def ensure_open(self) -> BasicBlock:
        """The block to emit into, opening an unreachable one if needed.

        Emission continues after a ``return`` so that the rest of the function
        still lowers; the block that results has no predecessors and dead-code
        elimination removes it.
        """
        if self.cur is None:
            self.open_block(self.label("dead"))
        return self.cur

    def emit(self, instr: Instr) -> Optional[Reg]:
        self.ensure_open().instrs.append(instr)
        return instr.dest

    def terminate(self, instr: Instr) -> None:
        self.ensure_open().instrs.append(instr)
        self.cur = None

    def br(self, target: str) -> None:
        self.terminate(Instr("br", Ty.VOID, None, [], labels=[target]))

    def br_cond(self, cond: Value, t: str, f: str) -> None:
        self.terminate(Instr("br.cond", Ty.I1, None, [cond], labels=[t, f]))

    # -- scopes ------------------------------------------------------------
    def push(self) -> None:
        self.scopes.append({})

    def pop(self) -> None:
        self.scopes.pop()

    def declare(self, name: str, var: _Var) -> None:
        self.scopes[-1][name] = var

    def lookup(self, name: str, node: object) -> _Var:
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        raise BuildError("E001", f"undeclared identifier {name!r}", node)

    # -- entry-block allocas ----------------------------------------------
    def alloca(self, name: str, ty: Ty, count: Optional[int] = None) -> Reg:
        """Add an alloca to the entry block.

        Allocas are kept at the top of the entry block so the slot exists on
        every path, which is also what LLVM's mem2reg expects to find.
        """
        reg = Reg(self.unique(name), Ty.PTR)
        args: List[Value] = [ConstInt(count)] if count is not None else []
        self.entry.instrs.insert(self.n_alloca, Instr("alloca", ty, reg, args))
        self.n_alloca += 1
        return reg

    # ======================================================================
    # Functions
    # ======================================================================
    def build(self) -> Function:
        assigned = _assigned_names(self.decl.body)
        self.entry = self.open_block("entry")
        self.n_alloca = 0
        self.push()

        for p in self.decl.params:
            ty = ty_of_node(p.type_node)
            self.taken.add(p.name)
            self.fn.params.append(Param(p.name, ty))
            if p.name in assigned:
                # The parameter is written to, so it needs an address.
                slot = self.alloca(f"{p.name}.addr", ty)
                self.emit(Instr("store", ty, None, [Reg(p.name, ty), slot]))
                self.declare(p.name, _Var(ty, "slot", addr=slot))
            else:
                self.declare(p.name, _Var(ty, "reg", reg=Reg(p.name, ty)))

        self.stmt(self.decl.body)
        self.close_function()
        self.pop()
        self.fn.blocks = self.blocks
        return self.fn

    def close_function(self) -> None:
        """Terminate a block left open by falling off the end of the function."""
        if self.cur is None:
            return
        if self.ret_ty is Ty.VOID:
            self.terminate(Instr("ret", Ty.VOID, None, []))
            return
        if not _reaches(self.blocks, self.cur.label):
            # Unreachable tail, e.g. the join of an if whose arms both return.
            # No semantics are invented: control never arrives here.
            self.terminate(Instr("ret", self.ret_ty, None, [zero_of(self.ret_ty)]))
            return
        raise BuildError(
            "E009", f"function {self.decl.name!r} does not return on every path",
            self.decl)

    # ======================================================================
    # Statements
    # ======================================================================
    def stmt(self, node: ast.Stmt) -> None:
        match node:
            case ast.Block():
                self.push()
                for s in node.stmts:
                    self.stmt(s)
                self.pop()
            case ast.Let():
                self.let(node)
            case ast.Assign():
                self.assign(node)
            case ast.ExprStmt():
                self.expr(node.expr)
            case ast.Return():
                self.ret(node)
            case ast.If():
                self.if_stmt(node)
            case ast.While():
                self.while_stmt(node)
            case ast.For():
                self.for_stmt(node)
            case ast.Break():
                if not self.loops:
                    raise BuildError("E011", "'break' outside a loop", node)
                self.br(self.loops[-1][1])
            case ast.Continue():
                if not self.loops:
                    raise BuildError("E011", "'continue' outside a loop", node)
                self.br(self.loops[-1][0])
            case _:
                raise BuildError("E003", f"cannot lower {type(node).__name__}", node)

    def let(self, node: ast.Let) -> None:
        tn = node.type_node
        if tn.array_len is not None:
            if node.init is not None:
                raise BuildError(
                    "E004", "an array declaration cannot take an initialiser "
                            "(array initialisers need M2's E004 check first)", node)
            elem = elem_ty_of_node(tn)
            base = self.alloca(node.name, elem, tn.array_len)
            self.declare(node.name,
                         _Var(elem, "slot", addr=base, array_len=tn.array_len))
            return

        ty = ty_of_node(tn)
        slot = self.alloca(f"{node.name}.addr", ty)
        self.declare(node.name, _Var(ty, "slot", addr=slot))
        if node.init is not None:
            value = self.coerce(self.expr(node.init), ty, node)
            self.emit(Instr("store", ty, None, [value, slot]))

    def assign(self, node: ast.Assign) -> None:
        addr, ty = self.address_of(node.target)
        value = self.coerce(self.expr(node.value), ty, node)
        self.emit(Instr("store", ty, None, [value, addr]))

    def ret(self, node: ast.Return) -> None:
        if node.value is None:
            if self.ret_ty is not Ty.VOID:
                raise BuildError("E009", "'return' without a value in a "
                                         "non-void function", node)
            self.terminate(Instr("ret", Ty.VOID, None, []))
            return
        if self.ret_ty is Ty.VOID:
            raise BuildError("E010", "'return' with a value in a void function", node)
        value = self.coerce(self.expr(node.value), self.ret_ty, node)
        self.terminate(Instr("ret", self.ret_ty, None, [value]))

    def if_stmt(self, node: ast.If) -> None:
        """docs/cir-spec.md section 5:  br c ? then : else ... join.

        The join block is created lazily: if both arms end in a ``return``
        there is no edge into it and none is emitted, which is why abs() comes
        out as three blocks rather than four.
        """
        n = self.n_label
        self.n_label += 1
        then_l, end_l = f"if.then.{n}", f"if.end.{n}"
        else_l = f"if.else.{n}" if node.else_blk is not None else end_l

        cond = self.as_bool(self.expr(node.cond), node)
        self.br_cond(cond, then_l, else_l)

        reaches_end = False
        self.open_block(then_l)
        self.stmt(node.then_blk)
        if self.cur is not None:
            self.br(end_l)
            reaches_end = True

        if node.else_blk is not None:
            self.open_block(else_l)
            self.stmt(node.else_blk)
            if self.cur is not None:
                self.br(end_l)
                reaches_end = True
        else:
            reaches_end = True     # the false edge of br.cond goes to end

        if reaches_end:
            self.open_block(end_l)
        else:
            self.cur = None

    def while_stmt(self, node: ast.While) -> None:
        n = self.n_label
        self.n_label += 1
        head, body, end = f"while.head.{n}", f"while.body.{n}", f"while.end.{n}"

        self.br(head)
        self.open_block(head)
        self.br_cond(self.as_bool(self.expr(node.cond), node), body, end)

        self.open_block(body)
        self.loops.append((head, end))       # continue -> head, break -> end
        self.stmt(node.body)
        self.loops.pop()
        if self.cur is not None:
            self.br(head)

        self.open_block(end)

    def for_stmt(self, node: ast.For) -> None:
        n = self.n_label
        self.n_label += 1
        head, body = f"for.head.{n}", f"for.body.{n}"
        step, end = f"for.step.{n}", f"for.end.{n}"

        self.push()                           # the init clause scopes to the loop
        if node.init is not None:
            self.stmt(node.init)

        self.br(head)
        self.open_block(head)
        if node.cond is None:
            self.br(body)                     # for (;;) is an infinite loop
        else:
            self.br_cond(self.as_bool(self.expr(node.cond), node), body, end)

        self.open_block(body)
        self.loops.append((step, end))        # continue -> step, break -> end
        self.stmt(node.body)
        self.loops.pop()
        if self.cur is not None:
            self.br(step)

        self.open_block(step)
        if node.step is not None:
            self.stmt(node.step)
        self.br(head)

        self.open_block(end)
        self.pop()

    # ======================================================================
    # Expressions
    # ======================================================================
    def expr(self, node: ast.Expr) -> Value:
        match node:
            case ast.IntLit():
                return ConstInt(node.value, Ty.I32)
            case ast.FloatLit():
                return ConstFloat(node.value)
            case ast.BoolLit():
                return ConstInt(1 if node.value else 0, Ty.I1)
            case ast.VarRef():
                return self.var_ref(node)
            case ast.Unary():
                return self.unary(node)
            case ast.Binary():
                return self.binary(node)
            case ast.Call():
                return self.call(node)
            case ast.Index():
                addr, ty = self.address_of(node)
                return self.emit(Instr("load", ty, self.tmp(ty), [addr]))
            case _:
                raise BuildError("E003", f"cannot lower {type(node).__name__}", node)

    def var_ref(self, node: ast.VarRef) -> Value:
        var = self.lookup(node.name, node)
        if var.kind == "reg":
            return var.reg
        if var.array_len is not None:
            return var.addr           # an array name is its base address
        return self.emit(Instr("load", var.ty, self.tmp(var.ty), [var.addr]))

    def address_of(self, node: ast.Expr) -> tuple[Value, Ty]:
        """The address an assignment writes through, and the type stored there."""
        if isinstance(node, ast.VarRef):
            var = self.lookup(node.name, node)
            if var.kind == "reg":
                # A parameter kept in a register is never assigned -- that is
                # exactly the condition under which build() chose 'reg'.
                raise BuildError("E004", f"{node.name!r} has no address", node)
            return var.addr, var.ty
        if isinstance(node, ast.Index):
            base = node.base
            if not isinstance(base, ast.VarRef):
                raise BuildError("E012", "only a named array can be indexed", node)
            var = self.lookup(base.name, base)
            if var.array_len is None:
                raise BuildError("E012", f"{base.name!r} is not an array", node)
            index = self.coerce(self.expr(node.index), Ty.I32, node)
            ptr = self.emit(Instr("gep", var.ty, self.tmp(Ty.PTR),
                                  [var.addr, index]))
            return ptr, var.ty
        raise BuildError("E004", "left-hand side of assignment is not assignable",
                         node)

    def unary(self, node: ast.Unary) -> Value:
        value = self.expr(node.operand)
        ty = self.type_of(value)
        if node.op == "-":
            # Fold negation of a literal here, so that -2147483648 is the
            # single constant INT_MIN rather than a subtraction from an
            # unrepresentable positive 2147483648.
            if isinstance(value, ConstInt):
                return ConstInt(wrap_int(-value.value, ty), ty)
            if isinstance(value, ConstFloat):
                return ConstFloat(-value.value)
            if ty is Ty.F64:
                return self.emit(Instr("neg", ty, self.tmp(ty), [value]))
            # CIR has 'neg', but integer negation is spelled as a subtraction
            # from zero: it is what docs/examples/abs.cir shows, and LLVM has
            # no integer negate instruction either.
            return self.emit(Instr("sub", ty, self.tmp(ty), [zero_of(ty), value]))
        if node.op == "~":
            if ty is Ty.F64:
                raise BuildError("E003", "'~' requires an integer operand", node)
            return self.emit(Instr("not", ty, self.tmp(ty), [value]))
        if node.op == "!":
            return self.emit(Instr("not", Ty.I1, self.tmp(Ty.I1),
                                   [self.as_bool(value, node)]))
        raise BuildError("E003", f"unknown unary operator {node.op!r}", node)

    def binary(self, node: ast.Binary) -> Value:
        if node.op in ("&&", "||"):
            return self.short_circuit(node)

        lhs = self.expr(node.lhs)
        rhs = self.expr(node.rhs)
        lty, rty = self.type_of(lhs), self.type_of(rhs)

        if node.op in ("<<", ">>"):
            # The shift count is independent of the value's type; CIR defines
            # it modulo the operand width (docs/divergence.md row 3).
            if lty is Ty.F64:
                raise BuildError("E003", "shift requires an integer operand", node)
            rhs = self.coerce(rhs, lty, node)
            op = "shl" if node.op == "<<" else "ashr"
            return self.emit(Instr(op, lty, self.tmp(lty), [lhs, rhs]))

        ty = self.unify(lhs, rhs, lty, rty, node)
        lhs = self.coerce(lhs, ty, node)
        rhs = self.coerce(rhs, ty, node)

        if node.op in _SIGNED_CMP:
            pred = (_FLOAT_CMP if ty is Ty.F64 else _SIGNED_CMP)[node.op]
            kind = "fcmp" if ty is Ty.F64 else "icmp"
            return self.emit(Instr(f"{kind}.{pred}", ty, self.tmp(Ty.I1), [lhs, rhs]))

        table = _FLOAT_BINOP if ty is Ty.F64 else _INT_BINOP
        if node.op not in table:
            raise BuildError("E003", f"operator {node.op!r} is not defined on "
                                     f"{ty}", node)
        return self.emit(Instr(table[node.op], ty, self.tmp(ty), [lhs, rhs]))

    def short_circuit(self, node: ast.Binary) -> Value:
        """`&&` and `||` evaluate the right operand only when it can matter.

        CIR is not in SSA form, so the result is merged through a one-slot
        alloca rather than a phi -- the same mechanism every mutable local
        uses.  docs/divergence.md row 9 fixes left-to-right evaluation order,
        which this shape preserves.
        """
        n = self.n_label
        self.n_label += 1
        kind = "and" if node.op == "&&" else "or"
        rhs_l, end_l = f"{kind}.rhs.{n}", f"{kind}.end.{n}"

        slot = self.alloca(f"{kind}.{n}", Ty.I1)
        lhs = self.as_bool(self.expr(node.lhs), node)
        self.emit(Instr("store", Ty.I1, None, [lhs, slot]))
        if node.op == "&&":
            self.br_cond(lhs, rhs_l, end_l)
        else:
            self.br_cond(lhs, end_l, rhs_l)

        self.open_block(rhs_l)
        rhs = self.as_bool(self.expr(node.rhs), node)
        self.emit(Instr("store", Ty.I1, None, [rhs, slot]))
        self.br(end_l)

        self.open_block(end_l)
        return self.emit(Instr("load", Ty.I1, self.tmp(Ty.I1), [slot]))

    def call(self, node: ast.Call) -> Value:
        if node.callee in BUILTINS:
            ret_ty, arg_ty, op = BUILTINS[node.callee]
            if len(node.args) != 1:
                raise BuildError("E007", f"{node.callee} takes one argument", node)
            value = self.coerce(self.expr(node.args[0]), arg_ty, node)
            self.emit(Instr(op, arg_ty, None, [value]))
            return ConstInt(0, Ty.I32)     # void; the value is never consumed

        if node.callee not in self.sigs:
            raise BuildError("E006", f"call to undeclared function "
                                     f"{node.callee!r}", node)
        ret_ty, param_tys = self.sigs[node.callee]
        if len(node.args) != len(param_tys):
            raise BuildError("E007", f"{node.callee} takes {len(param_tys)} "
                                     f"argument(s), {len(node.args)} given", node)
        args = [self.coerce(self.expr(a), t, node)
                for a, t in zip(node.args, param_tys)]
        dest = self.tmp(ret_ty) if ret_ty is not Ty.VOID else None
        return self.emit(Instr("call", ret_ty, dest, args, callee=node.callee)) \
            or ConstInt(0, Ty.I32)

    # -- typing helpers ----------------------------------------------------
    def type_of(self, value: Value) -> Ty:
        if isinstance(value, GlobalRef):
            return Ty.PTR
        return value.ty

    def unify(self, lhs: Value, rhs: Value, lty: Ty, rty: Ty,
              node: object) -> Ty:
        """The common type of a binary operation's operands.

        Only the widenings MiniLang inserts automatically are considered
        (docs/minilang-spec.md section 4).  Anything else is E003 and will be
        reported properly by M2.
        """
        if lty is rty:
            return lty
        if (lty, rty) in _WIDEN:
            return rty
        if (rty, lty) in _WIDEN:
            return lty
        raise BuildError("E003", f"operands have incompatible types "
                                 f"{lty} and {rty}", node)

    def coerce(self, value: Value, ty: Ty, node: object) -> Value:
        """Widen ``value`` to ``ty``, or fail with the code M2 will report."""
        vty = self.type_of(value)
        if vty is ty:
            return value
        if isinstance(value, ConstInt) and ty in _INT_TYPES:
            return ConstInt(value.value, ty)      # literals adopt their context
        if isinstance(value, ConstInt) and ty is Ty.F64:
            return ConstFloat(float(value.value))
        op = _WIDEN.get((vty, ty))
        if op is None:
            raise BuildError("E005", f"cannot convert {vty} to {ty} implicitly",
                             node)
        return self.emit(Instr(op, ty, self.tmp(ty), [value]))

    def as_bool(self, value: Value, node: object) -> Value:
        """A condition must be ``i1``; CIR has no implicit truthiness."""
        ty = self.type_of(value)
        if ty is Ty.I1:
            return value
        raise BuildError("E003", f"condition has type {ty}, expected bool", node)


# --------------------------------------------------------------------------
# Small AST queries
# --------------------------------------------------------------------------
def _assigned_names(node: object) -> set[str]:
    """Names that appear as the target of an assignment anywhere below ``node``.

    Used to decide which parameters need a memory slot.  Over-approximating is
    safe: it only costs an alloca that mem2reg removes again.
    """
    found: set[str] = set()

    def walk(n: object) -> None:
        if isinstance(n, ast.Assign) and isinstance(n.target, ast.VarRef):
            found.add(n.target.name)
        for value in getattr(n, "__dict__", {}).values():
            if isinstance(value, list):
                for item in value:
                    walk(item)
            elif isinstance(value, (ast.Stmt, ast.Expr)):
                walk(value)

    walk(node)
    return found


def _reaches(blocks: List[BasicBlock], label: str) -> bool:
    """Is ``label`` reachable from the first block of a partially built list?"""
    if not blocks:
        return False
    seen, stack = set(), [blocks[0].label]
    by_label = {b.label: b for b in blocks}
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        block = by_label.get(current)
        if block is not None:
            stack.extend(block.successors())
    return label in seen


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------
def build(program: ast.Program, name: str = "module") -> Module:
    """Walk the AST and emit a CIR Module.

    Signatures are collected over the whole program first, so a function may
    call one that is declared later -- recursion and mutual recursion both
    lower without a forward declaration.
    """
    mod = Module(name)
    sigs: Dict[str, tuple] = {}
    globals_: Dict[str, _Var] = {}

    for decl in program.decls:
        if isinstance(decl, ast.FnDecl):
            sigs[decl.name] = (ty_of_node(decl.ret_type),
                               [ty_of_node(p.type_node) for p in decl.params])

    for decl in program.decls:
        if not isinstance(decl, ast.GlobalDecl):
            continue
        if decl.type_node.array_len is not None:
            raise BuildError("E003", "global arrays are not lowered yet "
                                     "(scalar globals only)", decl)
        ty = ty_of_node(decl.type_node)
        init: Optional[Value] = None
        if decl.init is not None:
            if isinstance(decl.init, ast.IntLit):
                init = (ConstFloat(float(decl.init.value)) if ty is Ty.F64
                        else ConstInt(decl.init.value, ty))
            elif isinstance(decl.init, ast.FloatLit):
                init = ConstFloat(decl.init.value)
            elif isinstance(decl.init, ast.BoolLit):
                init = ConstInt(1 if decl.init.value else 0, Ty.I1)
            else:
                raise BuildError("E003", "a global initialiser must be a "
                                         "literal", decl)
        mod.globals.append(Global(decl.name, ty, init))
        globals_[decl.name] = _Var(ty, "global", addr=GlobalRef(decl.name))

    for decl in program.decls:
        if isinstance(decl, ast.FnDecl):
            mod.functions.append(_FnBuilder(decl, sigs, globals_).build())

    return mod
