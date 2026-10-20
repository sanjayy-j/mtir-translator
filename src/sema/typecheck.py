"""MiniLang type checker.

Module M2.  Owner: Member 2.

Detects the twelve diagnostics of docs/minilang-spec.md section 7 and annotates
every expression's ``ty`` with its type name.  Types are the strings 'int',
'long', 'float', 'bool', 'void', and 'T[]' for an array of T.

Conversions (spec section 4): int -> long and int -> float are implicit, in
assignments, arguments, returns and binary operands.  Every other change of
numeric type is a narrowing conversion (E005); a change between unrelated
types is E004 in an assignment, E008 in an argument and E003 in an operator.
"""
from __future__ import annotations

from ..frontend import ast_nodes as A
from .diagnostics import Diagnostic
from .symtab import Symbol, SymbolTable

NUMERIC = ('int', 'long', 'float')
INTEGRAL = ('int', 'long')
_WIDEN = {('int', 'long'), ('int', 'float')}
_BUILTINS = {'print_int': 'int', 'print_float': 'float'}


class TypeError_(Exception):
    def __init__(self, code, message, node=None, filename="<input>"):
        self.diag = Diagnostic(code, message, getattr(node, 'line', 0),
                               getattr(node, 'col', 0), filename)
        self.code = code
        where = f"{self.diag.line}:{self.diag.col}: " if self.diag.line else ""
        super().__init__(f"{where}error[{code}]: {message}")


def _tyname(t):
    if t is None: return 'void'
    return f"{t.name}[]" if t.array_len is not None else t.name


def _widens(actual, expected):
    return actual == expected or (actual, expected) in _WIDEN


def _unify(a, b):
    if a == b: return a
    if (a, b) in _WIDEN: return b
    if (b, a) in _WIDEN: return a
    return None


def check(program):
    return _Checker(program).run()


class _Checker:
    def __init__(self, program):
        self.program = program
        self.syms = SymbolTable()
        self.funcs = {}
        self.ret = 'void'
        self.loops = 0

    def err(self, code, msg, node):
        raise TypeError_(code, msg, node)

    # -- declarations ------------------------------------------------------
    def run(self):
        for d in self.program.decls:
            if isinstance(d, A.FnDecl):
                if d.name in _BUILTINS or not self.syms.declare(Symbol(d.name, 'fn', d, d)):
                    self.err('E002', f"duplicate declaration of {d.name!r}", d)
                self.funcs[d.name] = d
            elif isinstance(d, A.GlobalDecl):
                if not self.syms.declare(Symbol(d.name, 'global', _tyname(d.type_node), d)):
                    self.err('E002', f"duplicate declaration of {d.name!r}", d)
                if d.init is not None:
                    self.assignable(self.expr(d.init), _tyname(d.type_node), d.init, 'E004')
        for d in self.program.decls:
            if isinstance(d, A.FnDecl):
                self.function(d)
        return self.program

    def function(self, f):
        self.ret = _tyname(f.ret_type)
        self.syms.push()
        for p in f.params:
            if not self.syms.declare(Symbol(p.name, 'param', _tyname(p.type_node), p)):
                self.err('E002', f"duplicate parameter {p.name!r}", f)
        self.block(f.body)
        self.syms.pop()
        if self.ret != 'void' and not _returns(f.body):
            self.err('E009', f"function {f.name!r} does not return on every path", f)

    # -- statements --------------------------------------------------------
    def block(self, b):
        self.syms.push()
        for s in b.stmts:
            self.stmt(s)
        self.syms.pop()

    def stmt(self, s):
        if isinstance(s, A.Block): self.block(s)
        elif isinstance(s, A.Let):
            ty = _tyname(s.type_node)
            if s.init is not None and s.type_node.array_len is not None:
                # minilang-spec.md section 5, decision 0001 (option A).
                self.err('E004', "an array declaration cannot take an initialiser", s)
            if s.init is not None:
                self.assignable(self.expr(s.init), ty, s.init, 'E004')
            if not self.syms.declare(Symbol(s.name, 'var', ty, s)):
                self.err('E002', f"duplicate declaration of {s.name!r} in the same scope", s)
        elif isinstance(s, A.Assign):
            target = self.lvalue(s.target)
            self.assignable(self.expr(s.value), target, s, 'E004')
        elif isinstance(s, A.ExprStmt): self.expr(s.expr)
        elif isinstance(s, A.Return):
            if s.value is None:
                if self.ret != 'void':
                    self.err('E009', "'return' without a value in a non-void function", s)
            elif self.ret == 'void':
                self.err('E010', "'return' with a value in a void function", s)
            else:
                self.assignable(self.expr(s.value), self.ret, s, 'E004')
        elif isinstance(s, A.If):
            self.cond(s.cond)
            self.block(s.then_blk)
            if s.else_blk is not None: self.stmt(s.else_blk)
        elif isinstance(s, A.While):
            self.cond(s.cond)
            self.loop_body(s.body)
        elif isinstance(s, A.For):
            self.syms.push()                 # the init clause scopes to the loop
            if s.init is not None: self.stmt(s.init)
            if s.cond is not None: self.cond(s.cond)
            if s.step is not None: self.stmt(s.step)
            self.loop_body(s.body)
            self.syms.pop()
        elif isinstance(s, (A.Break, A.Continue)):
            if self.loops == 0:
                kw = 'break' if isinstance(s, A.Break) else 'continue'
                self.err('E011', f"{kw!r} outside a loop", s)

    def loop_body(self, body):
        self.loops += 1
        self.block(body)
        self.loops -= 1

    def cond(self, e):
        t = self.expr(e)
        if t != 'bool': self.err('E003', f"condition has type {t}, expected bool", e)

    def assignable(self, actual, expected, node, code):
        if _widens(actual, expected): return
        if actual in NUMERIC and expected in NUMERIC:
            self.err('E005', f"cannot convert {actual} to {expected} implicitly", node)
        self.err(code, f"cannot assign {actual} to {expected}", node)

    def lvalue(self, e):
        if isinstance(e, A.VarRef):
            sym = self.resolve(e)
            if sym.ty.endswith('[]'):
                self.err('E004', f"cannot assign to array {e.name!r} as a whole", e)
            e.ty = sym.ty
            return sym.ty
        if isinstance(e, A.Index): return self.expr(e)
        self.err('E004', "left-hand side of assignment is not assignable", e)

    def resolve(self, e):
        sym = self.syms.resolve(e.name)
        if sym is None or sym.kind == 'fn':
            self.err('E001', f"undeclared identifier {e.name!r}", e)
        return sym

    # -- expressions -------------------------------------------------------
    def expr(self, e):
        e.ty = self._expr(e)
        return e.ty

    def _expr(self, e):
        if isinstance(e, A.IntLit): return 'int'
        if isinstance(e, A.FloatLit): return 'float'
        if isinstance(e, A.BoolLit): return 'bool'
        if isinstance(e, A.VarRef): return self.resolve(e).ty
        if isinstance(e, A.Index):
            base = self.expr(e.base)
            if not base.endswith('[]'): self.err('E012', "indexing a non-array", e)
            if self.expr(e.index) != 'int': self.err('E012', "array index must be int", e.index)
            return base[:-2]
        if isinstance(e, A.Unary):
            t = self.expr(e.operand)
            ok = {'!': ('bool',), '-': NUMERIC, '~': INTEGRAL}[e.op]
            if t not in ok: self.err('E003', f"operator {e.op!r} is not defined on {t}", e)
            return t
        if isinstance(e, A.Binary): return self.binary(e)
        if isinstance(e, A.Call): return self.call(e)
        self.err('E003', f"unsupported expression {type(e).__name__}", e)

    def binary(self, e):
        a, b, op = self.expr(e.lhs), self.expr(e.rhs), e.op
        bad = lambda: self.err('E003', f"operator {op!r} is not defined on {a} and {b}", e)
        if op in ('&&', '||'):
            if a != 'bool' or b != 'bool': bad()
            return 'bool'
        if op in ('<<', '>>'):
            # The count adopts the value's type (divergence.md row 3), as in
            # the builder, so it may widen but not narrow.
            if a not in INTEGRAL or b not in INTEGRAL: bad()
            if not _widens(b, a): self.err('E005', f"shift count {b} narrows to {a}", e)
            return a
        t = _unify(a, b)
        if t is None: bad()
        if op in ('==', '!=', '<', '<=', '>', '>='): return 'bool'
        if op in ('&', '|', '^'):
            if t not in ('int', 'long', 'bool'): bad()
            return t
        if t not in NUMERIC: bad()                  # + - * / %
        if op == '%' and t == 'float': bad()
        return t

    def call(self, e):
        if e.callee in _BUILTINS:
            want = _BUILTINS[e.callee]
            if len(e.args) != 1:
                self.err('E007', f"{e.callee} takes 1 argument, {len(e.args)} given", e)
            t = self.expr(e.args[0])
            if not _widens(t, want):
                self.err('E008', f"{e.callee} expects {want}, got {t}", e.args[0])
            return 'void'
        f = self.funcs.get(e.callee)
        if f is None: self.err('E006', f"call to undeclared function {e.callee!r}", e)
        if len(e.args) != len(f.params):
            self.err('E007', f"{e.callee} takes {len(f.params)} argument(s), "
                             f"{len(e.args)} given", e)
        for arg, p in zip(e.args, f.params):
            t, want = self.expr(arg), _tyname(p.type_node)
            if not _widens(t, want):
                self.err('E008', f"argument {p.name!r} of {e.callee} expects {want}, got {t}", arg)
        return _tyname(f.ret_type)


def _returns(s):
    """Does every path through ``s`` end in a return (or never finish)?"""
    if isinstance(s, A.Return): return True
    if isinstance(s, A.Block): return any(_returns(x) for x in s.stmts)
    if isinstance(s, A.If):
        return s.else_blk is not None and _returns(s.then_blk) and _returns(s.else_blk)
    if isinstance(s, (A.While, A.For)):
        infinite = (s.cond is None if isinstance(s, A.For)
                    else isinstance(s.cond, A.BoolLit) and s.cond.value)
        return infinite and not _breaks(s.body)
    return False


def _breaks(s):
    """Does ``s`` contain a break that exits *this* loop (not a nested one)?"""
    if isinstance(s, A.Break): return True
    if isinstance(s, (A.While, A.For)): return False
    if isinstance(s, A.Block): return any(_breaks(x) for x in s.stmts)
    if isinstance(s, A.If):
        return _breaks(s.then_blk) or (s.else_blk is not None and _breaks(s.else_blk))
    return False
