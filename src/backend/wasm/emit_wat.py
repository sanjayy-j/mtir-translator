"""CIR -> WebAssembly Text (WAT).

Module M6c.  Owner: Member 4.

Control flow uses a program-counter dispatch loop: every CIR block becomes one
arm of a `loop`, and a branch sets `$.pc` and re-enters the loop.  That is
valid structured Wasm for any CFG, reducible or not; structurer.py is where
nicer nesting will come from.

Memory (docs/divergence.md row 8): Wasm locals have no address, so every
alloca lives in linear memory.  A shadow stack pointer global `$.sp` starts at
the top of memory and grows down; each function claims a frame on entry and
releases it on every return, so recursive calls get their own slots.  Every
element is 8 bytes, whatever its type.

Semantics realised here (docs/divergence.md):
  row 1/2  div_s and div_u trap natively; rem_s does NOT trap on INT_MIN % -1
           (it returns 0), so it gets an explicit guard.
  row 3    shifts mask the count natively.
  row 5    i1 is an i32 holding 0 or 1.
  row 6    i32/i64.trunc_f64_s trap natively on NaN and out of range.
  row 7    every gep into an alloca with a known extent is bounds-checked.

Names starting with '.' ($.pc, $.fp, $.sp) cannot collide with a CIR register,
because MiniLang identifiers never start with '.'.
"""
from __future__ import annotations

import math

from ...cir.ir import *

MEMORY_PAGES = 16                    # 1 MiB; the shadow stack starts at the top
ELEM_SIZE = 8


def wt(t):
    return {Ty.I1: 'i32', Ty.I32: 'i32', Ty.I64: 'i64', Ty.F64: 'f64',
            Ty.PTR: 'i32', Ty.VOID: 'i32'}[t]


def _const(ty, value):
    if ty == Ty.F64:
        v = float(value)
        if math.isnan(v): return '(f64.const nan)'
        if math.isinf(v): return f'(f64.const {"inf" if v > 0 else "-inf"})'
        return f'(f64.const {v!r})'
    if ty == Ty.I1: return f'(i32.const {value & 1})'     # row 5: 0 or 1, never -1
    return f'({wt(ty)}.const {wrap_int(value, ty) if ty in INT_WIDTH else value})'


def val(v):
    if isinstance(v, Reg): return f'(local.get ${v.name})'
    if isinstance(v, ConstFloat): return _const(Ty.F64, v.value)
    if isinstance(v, ConstInt): return _const(v.ty, v.value)
    if isinstance(v, GlobalRef): return f'(global.get ${v.name})'
    raise ValueError(f'cannot emit operand {v!r}')


_BIN = {'add': 'add', 'sub': 'sub', 'mul': 'mul', 'sdiv': 'div_s', 'udiv': 'div_u',
        'srem': 'rem_s', 'urem': 'rem_u', 'and': 'and', 'or': 'or', 'xor': 'xor',
        'shl': 'shl', 'ashr': 'shr_s', 'lshr': 'shr_u',
        'fadd': 'add', 'fsub': 'sub', 'fmul': 'mul', 'fdiv': 'div'}
_ICMP = {'eq': 'eq', 'ne': 'ne', 'slt': 'lt_s', 'sle': 'le_s', 'sgt': 'gt_s', 'sge': 'ge_s',
         'ult': 'lt_u', 'ule': 'le_u', 'ugt': 'gt_u', 'uge': 'ge_u'}
_FCMP = {'oeq': 'eq', 'one': 'ne', 'olt': 'lt', 'ole': 'le', 'ogt': 'gt', 'oge': 'ge'}


def emit(mod):
    top = MEMORY_PAGES * 65536
    lines = ['(module',
             '  (import "env" "print_int" (func $print_int (param i32)))',
             '  (import "env" "print_float" (func $print_float (param f64)))',
             f'  (memory (export "memory") {MEMORY_PAGES})',
             f'  (global $.sp (mut i32) (i32.const {top}))']
    for g in mod.globals:
        init = g.init.value if g.init is not None else 0
        lines.append(f'  (global ${g.name} (mut {wt(g.ty)}) {_const(g.ty, init)})')
    for f in mod.functions:
        lines += _Fn(f).emit()
        lines.append(f'  (export "{f.name}" (func ${f.name}))')
    lines.append(')')
    return '\n'.join(lines) + '\n'


class _Fn:
    def __init__(self, f):
        self.f = f
        self.addr = {}            # alloca dest -> frame offset
        self.extent = {}          # alloca dest -> element count
        self.frame = 0
        for b in f.blocks:
            for i in b.instrs:
                if i.op == 'alloca':
                    n = i.args[0].value if i.args else 1
                    self.addr[i.dest.name] = self.frame
                    self.extent[i.dest.name] = n
                    self.frame += n * ELEM_SIZE

    def header(self):
        f = self.f
        params = ''.join(f' (param ${p.name} {wt(p.ty)})' for p in f.params)
        result = '' if f.ret_ty == Ty.VOID else f' (result {wt(f.ret_ty)})'
        regs = {}
        for b in f.blocks:
            for i in b.instrs:
                if i.dest is not None: regs.setdefault(i.dest.name, i.dest.ty)
        locals_ = ''.join(f' (local ${n} {wt(t)})' for n, t in regs.items())
        return f'  (func ${f.name}{params}{result}{locals_} (local $.fp i32) (local $.pc i32)'

    def prologue(self):
        if not self.frame: return []
        return [f'(local.set $.fp (i32.sub (global.get $.sp) (i32.const {self.frame})))',
                '(global.set $.sp (local.get $.fp))']

    def ret(self, t):
        out = []
        if self.frame:
            out.append(f'(global.set $.sp (i32.add (local.get $.fp) (i32.const {self.frame})))')
        out.append(f'(return {val(t.args[0])})' if t.args else '(return)')
        return out

    def emit(self):
        lines = [self.header()]
        body = self.prologue() + self.dispatch()
        lines += ['    ' + x for x in body]
        lines.append('  )')
        return lines

    # -- control flow: the program-counter dispatch loop --------------------
    def dispatch(self):
        f = self.f
        index = {b.label: n for n, b in enumerate(f.blocks)}
        out = ['(local.set $.pc (i32.const 0))', '(loop $dispatch']
        for n, b in enumerate(f.blocks):
            out.append(f'  (if (i32.eq (local.get $.pc) (i32.const {n})) (then')
            for i in b.instrs[:-1]:
                out += ['    ' + x for x in self.instr(i)]
            t = b.instrs[-1]
            if t.op == 'br':
                out.append(f'    (local.set $.pc (i32.const {index[t.labels[0]]}))')
                out.append('    (br $dispatch)')
            elif t.op == 'br.cond':
                out.append(f'    (local.set $.pc (select (i32.const {index[t.labels[0]]}) '
                           f'(i32.const {index[t.labels[1]]}) {val(t.args[0])}))')
                out.append('    (br $dispatch)')
            else:
                out += ['    ' + x for x in self.ret(t)]
            out.append('  ))')
        out += [')', '(unreachable)']
        return out

    # -- straight-line instructions ----------------------------------------
    def instr(self, i):
        op, d = i.op, i.dest
        set_ = (lambda e: [f'(local.set ${d.name} {e})'])
        a = [val(x) for x in i.args]
        p = wt(i.ty)
        if op == 'alloca':
            return set_(f'(i32.add (local.get $.fp) (i32.const {self.addr[d.name]}))')
        if op == 'load':
            if isinstance(i.args[0], GlobalRef): return set_(a[0])
            return set_(f'({p}.load {a[0]})')
        if op == 'store':
            if isinstance(i.args[1], GlobalRef): return [f'(global.set ${i.args[1].name} {a[0]})']
            return [f'({p}.store {a[1]} {a[0]})']
        if op == 'gep':
            out = []
            base = i.args[0]
            if isinstance(base, Reg) and base.name in self.extent:
                out.append(f'(if (i32.ge_u {a[1]} (i32.const {self.extent[base.name]})) '
                           f'(then (unreachable)))')
            return out + set_(f'(i32.add {a[0]} (i32.mul {a[1]} (i32.const {ELEM_SIZE})))')
        if op == 'call':
            c = f'(call ${i.callee}' + ''.join(' ' + x for x in a) + ')'
            return set_(c) if d else [c]
        if op == 'print.i32': return [f'(call $print_int {a[0]})']
        if op == 'print.f64': return [f'(call $print_float {a[0]})']
        if op == 'trap': return ['(unreachable)']
        if op == 'neg':
            if i.ty == Ty.F64: return set_(f'(f64.neg {a[0]})')
            return set_(f'({p}.sub ({p}.const 0) {a[0]})')
        if op == 'not':
            ones = 1 if i.ty == Ty.I1 else -1
            return set_(f'({p}.xor {a[0]} ({p}.const {ones}))')
        if op.startswith('icmp.'):
            return set_(f'({p}.{_ICMP[op[5:]]} {a[0]} {a[1]})')
        if op.startswith('fcmp.'):
            return set_(f'(f64.{_FCMP[op[5:]]} {a[0]} {a[1]})')
        if op in CONV_OPS:
            return set_(self.convert(op, i.ty, getattr(i.args[0], 'ty', Ty.I32), a[0]))
        if op in _BIN:
            out = []
            if op == 'srem':
                lo = -(1 << (INT_WIDTH[i.ty] - 1))
                out.append(f'(if (i32.and ({p}.eq {a[0]} ({p}.const {lo})) '
                           f'({p}.eq {a[1]} ({p}.const -1))) (then (unreachable)))')
            if i.ty == Ty.I1 and op in ('add', 'sub', 'mul', 'shl'):
                return out + set_(f'(i32.and (i32.{_BIN[op]} {a[0]} {a[1]}) (i32.const 1))')
            return out + set_(f'({p}.{_BIN[op]} {a[0]} {a[1]})')
        raise ValueError(f'no Wasm lowering for CIR opcode {op!r}')

    @staticmethod
    def convert(op, ty, src, x):
        if op == 'sitofp': return f'(f64.convert_{wt(src)}_s {x})'
        if op == 'fptosi': return f'({wt(ty)}.trunc_f64_s {x})'
        if op == 'trunc':
            v = f'(i32.wrap_i64 {x})' if src == Ty.I64 else x
            return f'(i32.and {v} (i32.const 1))' if ty == Ty.I1 else v
        if op == 'sext':
            if src == Ty.I1:     # i1 -1/0 sign-extension: 0 - x
                return f'({wt(ty)}.sub ({wt(ty)}.const 0) ' + \
                       (f'(i64.extend_i32_u {x}))' if ty == Ty.I64 else f'{x})')
            return f'(i64.extend_i32_s {x})' if ty == Ty.I64 and src != Ty.I64 else x
        if op == 'zext':
            return f'(i64.extend_i32_u {x})' if ty == Ty.I64 and src != Ty.I64 else x
        raise ValueError(f'unknown conversion {op}')
