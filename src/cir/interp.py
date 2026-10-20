"""Reference interpreter for CIR.

This is the semantic oracle: every back end must agree with it on
(return value, stdout, trapped).  Integer semantics follow docs/divergence.md
-- wraparound, shift counts mod width, traps on division by zero,
INT_MIN / -1, out-of-range fptosi and out-of-bounds indices.

Values are Python ints/floats.  Integers are kept in their *signed*
two's-complement range for their type; i1 is 0 or 1.  Pointers are
('mem', slot_id, index) or ('global', name).
"""
from __future__ import annotations
import math
import sys
from .ir import *


class _Trap(Exception):
    pass


def fmt_float(v):
    """The one print_float format, shared by every target: C's "%f"."""
    if math.isnan(v): return 'nan'
    if math.isinf(v): return 'inf' if v > 0 else '-inf'
    return f'{v:.6f}'


def _wrap(v, ty):
    if ty == Ty.I1: return v & 1
    return wrap_int(v, ty)


def _unsigned(v, ty):
    return v & ((1 << INT_WIDTH[ty]) - 1)


def _div_trunc(a, b):
    """Integer division rounding toward zero, as C, LLVM and Wasm do."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def _fdiv(a, b):
    if b == 0:
        if a == 0 or math.isnan(a): return math.nan
        return math.copysign(math.inf, a) * math.copysign(1.0, b)
    return a / b


_ICMP = {'eq': lambda a, b: a == b, 'ne': lambda a, b: a != b,
         'slt': lambda a, b: a < b, 'sle': lambda a, b: a <= b,
         'sgt': lambda a, b: a > b, 'sge': lambda a, b: a >= b}
_UCMP = {'ult': lambda a, b: a < b, 'ule': lambda a, b: a <= b,
         'ugt': lambda a, b: a > b, 'uge': lambda a, b: a >= b}
# Python's float comparisons are already 'ordered' (false on NaN) except !=.
_FCMP = {'oeq': lambda a, b: a == b,
         'one': lambda a, b: not (math.isnan(a) or math.isnan(b)) and a != b,
         'olt': lambda a, b: a < b, 'ole': lambda a, b: a <= b,
         'ogt': lambda a, b: a > b, 'oge': lambda a, b: a >= b}


def _binop(op, ty, a, b):
    if op in ('fadd', 'fsub', 'fmul', 'fdiv'):
        return {'fadd': lambda: a + b, 'fsub': lambda: a - b,
                'fmul': lambda: a * b, 'fdiv': lambda: _fdiv(a, b)}[op]()
    w = INT_WIDTH[ty]
    if op in ('sdiv', 'srem', 'udiv', 'urem'):
        if b == 0: raise _Trap()
        if op in ('sdiv', 'srem'):
            if a == -(1 << (w - 1)) and b == -1: raise _Trap()
            q = _div_trunc(a, b)
            return _wrap(q if op == 'sdiv' else a - q * b, ty)
        ua, ub = _unsigned(a, ty), _unsigned(b, ty)
        return _wrap(ua // ub if op == 'udiv' else ua % ub, ty)
    if op in ('shl', 'ashr', 'lshr'):
        c = _unsigned(b, ty) % w
        if op == 'shl': return _wrap(a << c, ty)
        if op == 'ashr': return _wrap(a >> c, ty)
        return _wrap(_unsigned(a, ty) >> c, ty)
    z = {'add': lambda: a + b, 'sub': lambda: a - b, 'mul': lambda: a * b,
         'and': lambda: a & b, 'or': lambda: a | b, 'xor': lambda: a ^ b}[op]()
    return _wrap(z, ty)


def _convert(op, ty, v, src_ty):
    if op == 'sext': return _wrap(-v if src_ty == Ty.I1 else v, ty)
    if op == 'trunc': return _wrap(v, ty)
    if op == 'zext': return _wrap(_unsigned(v, src_ty), ty)
    if op == 'sitofp': return float(v)
    if op == 'fptosi':
        bits = INT_WIDTH[ty]
        # Same bounds as the LLVM guard (src/backend/llvm/guards.py).
        low, high = float(-(1 << (bits - 1)) - 1), float(1 << (bits - 1))
        if math.isnan(v) or v <= low or v >= high: raise _Trap()
        return _wrap(math.trunc(v), ty)
    raise ValueError(f'unsupported conversion {op}')


def run(mod, entry='main', args=()):
    funcs = {f.name: f for f in mod.functions}
    globs = {g.name: (g.init.value if g.init else 0) for g in mod.globals}
    out = []
    slots = {}          # slot id -> list of elements
    next_slot = [0]

    def load(p):
        if p[0] == 'global': return globs[p[1]]
        return slots[p[1]][p[2]]

    def store(p, v):
        if p[0] == 'global': globs[p[1]] = v
        else: slots[p[1]][p[2]] = v

    def call(name, argv):
        f = funcs[name]
        regs = {p.name: v for p, v in zip(f.params, argv)}
        blocks = {b.label: b for b in f.blocks}
        owned = []

        def val(x):
            if isinstance(x, Reg): return regs[x.name]
            if isinstance(x, (ConstInt, ConstFloat)): return x.value
            if isinstance(x, GlobalRef): return ('global', x.name)
            raise ValueError(f'bad operand {x!r}')

        try:
            b = f.entry
            while True:
                for ins in b.instrs:
                    op, d = ins.op, ins.dest
                    if op == 'alloca':
                        n = val(ins.args[0]) if ins.args else 1
                        sid = next_slot[0]; next_slot[0] += 1
                        slots[sid] = [0.0 if ins.ty == Ty.F64 else 0] * n
                        owned.append(sid)
                        regs[d.name] = ('mem', sid, 0)
                    elif op == 'load': regs[d.name] = load(val(ins.args[0]))
                    elif op == 'store': store(val(ins.args[1]), val(ins.args[0]))
                    elif op == 'gep':
                        base, idx = val(ins.args[0]), val(ins.args[1])
                        if not 0 <= idx < len(slots[base[1]]): raise _Trap()
                        regs[d.name] = ('mem', base[1], idx)
                    elif op == 'call':
                        r = call(ins.callee, [val(a) for a in ins.args])
                        if d: regs[d.name] = r
                    elif op == 'print.i32': out.append(str(val(ins.args[0])))
                    elif op == 'print.f64': out.append(fmt_float(val(ins.args[0])))
                    elif op == 'trap': raise _Trap()
                    elif op == 'br': b = blocks[ins.labels[0]]; break
                    elif op == 'br.cond':
                        b = blocks[ins.labels[0] if val(ins.args[0]) else ins.labels[1]]; break
                    elif op == 'ret': return val(ins.args[0]) if ins.args else 0
                    elif op == 'neg':
                        v = val(ins.args[0])
                        regs[d.name] = -v if ins.ty == Ty.F64 else _wrap(-v, ins.ty)
                    elif op == 'not':
                        regs[d.name] = _wrap(~val(ins.args[0]), ins.ty)
                    elif op.startswith('icmp.'):
                        a, c, p = val(ins.args[0]), val(ins.args[1]), op[5:]
                        if p in _UCMP: a, c = _unsigned(a, ins.ty), _unsigned(c, ins.ty)
                        regs[d.name] = int((_ICMP.get(p) or _UCMP[p])(a, c))
                    elif op.startswith('fcmp.'):
                        regs[d.name] = int(_FCMP[op[5:]](val(ins.args[0]), val(ins.args[1])))
                    elif op in CONV_OPS:
                        src = getattr(ins.args[0], 'ty', Ty.I32)
                        regs[d.name] = _convert(op, ins.ty, val(ins.args[0]), src)
                    elif op in ARITH_OPS or op in BIT_OPS:
                        regs[d.name] = _binop(op, ins.ty, val(ins.args[0]), val(ins.args[1]))
                    else: raise ValueError(f'unsupported CIR op {op}')
        finally:
            for sid in owned: del slots[sid]

    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 20000))
    try:
        r = call(entry, list(args))
        return r, '\n'.join(out), False
    except _Trap:
        return 0, '\n'.join(out), True
    finally:
        sys.setrecursionlimit(old)
