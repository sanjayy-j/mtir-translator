"""CIR -> textual stack bytecode (the format of docs/examples/abs.sbc.txt).

Module M7.  Owner: Member 4.

Layout:

    abs:                      function label (no 'L_' prefix)
            .params x         parameter names, in order
            LOADL   x
            PUSHI   0
            LT_S
            JZ      L_exit
            ...
    L_exit:                   block label (always 'L_' + the CIR label)

Labels stay flat and jumps are absolute, so no structuring pass is needed.
Operands are pushed left to right (divergence.md row 9).  Integer opcodes act
on i32 unless suffixed '.64' (i64) or '.1' (i1); float opcodes start with 'F'.
The full opcode set is the one src/backend/stackvm/vm.py executes.

Lowering reuses reg2stack's naive schema + peephole: every value is parked
with STOREL and reloaded with LOADL, then adjacent STOREL/LOADL pairs that
nothing else can observe are deleted.
"""
from ...backend.wasm.reg2stack import (block_live_out, block_use_counts,
                                       local_table, peephole, StackOp)
from ...cir.ir import *

_INT = {'add': 'ADD', 'sub': 'SUB', 'mul': 'MUL', 'sdiv': 'DIV_S', 'udiv': 'DIV_U',
        'srem': 'REM_S', 'urem': 'REM_U', 'and': 'AND', 'or': 'OR', 'xor': 'XOR',
        'shl': 'SHL', 'ashr': 'SHR_S', 'lshr': 'SHR_U', 'neg': 'NEG', 'not': 'NOT'}
_FLT = {'fadd': 'FADD', 'fsub': 'FSUB', 'fmul': 'FMUL', 'fdiv': 'FDIV', 'neg': 'FNEG'}
_ICMP = {'eq': 'EQ', 'ne': 'NE', 'slt': 'LT_S', 'sle': 'LE_S', 'sgt': 'GT_S', 'sge': 'GE_S',
         'ult': 'LT_U', 'ule': 'LE_U', 'ugt': 'GT_U', 'uge': 'GE_U'}
_FCMP = {'oeq': 'FEQ', 'one': 'FNE', 'olt': 'FLT', 'ole': 'FLE', 'ogt': 'FGT', 'oge': 'FGE'}
_SUFFIX = {Ty.I32: '', Ty.I64: '.64', Ty.I1: '.1', Ty.PTR: '', Ty.VOID: ''}


def _push(v, local_of):
    if isinstance(v, Reg): return StackOp('local.get', local_of[v.name])
    if isinstance(v, ConstFloat): return StackOp('PUSHF', repr(float(v.value)))
    if isinstance(v, ConstInt):
        if v.ty == Ty.I1: return StackOp('PUSHI', v.value & 1)
        return StackOp('PUSHI', wrap_int(v.value, v.ty) if v.ty in INT_WIDTH else v.value)
    raise ValueError(f'cannot push {v!r}')


def _lower_instr(i, local_of, next_label):
    """Stack ops for one CIR instruction (operands, opcode, result parking)."""
    op, out = i.op, []
    push = lambda vs: out.extend(_push(v, local_of) for v in vs)
    park = lambda: out.append(StackOp('local.set', local_of[i.dest.name]))

    if op == 'load' and isinstance(i.args[0], GlobalRef):
        out.append(StackOp('LOADG', i.args[0].name)); park(); return out
    if op == 'store' and isinstance(i.args[1], GlobalRef):
        push(i.args[:1]); out.append(StackOp('STOREG', i.args[1].name)); return out
    if op == 'br':
        if i.labels[0] != next_label: out.append(StackOp('JMP', 'L_' + i.labels[0]))
        return out
    if op == 'br.cond':
        push(i.args)
        out.append(StackOp('JZ', 'L_' + i.labels[1]))
        if i.labels[0] != next_label: out.append(StackOp('JMP', 'L_' + i.labels[0]))
        return out
    if op == 'ret':
        push(i.args); out.append(StackOp('RET')); return out
    if op == 'call':
        push(i.args); out.append(StackOp('CALL', f'{i.callee} {len(i.args)}'))
        if i.dest: park()
        else: out.append(StackOp('DROP'))
        return out
    if op == 'alloca':
        out.append(StackOp('ALLOCA', i.args[0].value if i.args else 1)); park(); return out

    push(i.args)
    if op == 'load': out.append(StackOp('LOAD'))
    elif op == 'store': out.append(StackOp('STORE'))
    elif op == 'gep': out.append(StackOp('GEP'))
    elif op == 'print.i32': out.append(StackOp('PRINTI'))
    elif op == 'print.f64': out.append(StackOp('PRINTF'))
    elif op == 'trap': out.append(StackOp('TRAP'))
    elif op.startswith('icmp.'): out.append(StackOp(_ICMP[op[5:]] + _SUFFIX[i.ty]))
    elif op.startswith('fcmp.'): out.append(StackOp(_FCMP[op[5:]]))
    elif op in CONV_OPS:
        src = getattr(i.args[0], 'ty', Ty.I32)
        out.append(StackOp(op.upper(), f'{src} {i.ty}'))
    elif i.ty == Ty.F64 and op in _FLT: out.append(StackOp(_FLT[op]))
    elif op in _INT: out.append(StackOp(_INT[op] + _SUFFIX[i.ty]))
    else: raise ValueError(f'no bytecode lowering for CIR opcode {op!r}')
    if i.dest is not None: park()
    return out


def _fmt(s):
    name = {'local.get': 'LOADL', 'local.set': 'STOREL'}.get(s.op, s.op)
    arg = s.arg
    if isinstance(arg, str) and s.op.startswith('local.'): arg = arg.lstrip('$')
    return name if arg is None else f'{name:<8}{arg}'


def emit(mod):
    lines = []
    for g in mod.globals:
        init = _push(g.init, {}).arg if g.init is not None else (0.0 if g.ty == Ty.F64 else 0)
        lines.append(f'.global {g.name} {init}')
    for f in mod.functions:
        lines.append(f'{f.name}:')
        lines.append('        .params' + ''.join(' ' + p.name for p in f.params))
        local_of = local_table(f)
        for n, b in enumerate(f.blocks):
            next_label = f.blocks[n + 1].label if n + 1 < len(f.blocks) else None
            seq = [s for i in b.instrs for s in _lower_instr(i, local_of, next_label)]
            seq = peephole(seq, block_use_counts(b, local_of), block_live_out(f, b, local_of))
            if n: lines.append(f'L_{b.label}:')
            lines += ['        ' + _fmt(s) for s in seq]
    return '\n'.join(lines) + '\n'
