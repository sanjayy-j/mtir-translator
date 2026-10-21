"""Reference stack VM for the textual bytecode emitted by emit_sbc.

Module M7.  Owner: Member 4.

Returns the same (return value, stdout, trapped) triple as the CIR
interpreter.  Integer and float arithmetic reuse the interpreter's helpers, so
the VM tests the *lowering* (stack discipline, jumps, frames, memory) rather
than re-deriving divergence.md a second time.

Opcodes (integer ops are i32 unless suffixed .64 or .1):
  PUSHI n | PUSHF x | LOADL x | STOREL x | LOADG g | STOREG g | DROP
  ADD SUB MUL DIV_S DIV_U REM_S REM_U AND OR XOR SHL SHR_S SHR_U NEG NOT
  EQ NE LT_S LE_S GT_S GE_S LT_U LE_U GT_U GE_U
  FADD FSUB FMUL FDIV FNEG FEQ FNE FLT FLE FGT FGE
  SEXT/ZEXT/TRUNC/SITOFP/FPTOSI src dst
  ALLOCA n | LOAD | STORE | GEP           (STORE pops address, then value)
  JMP L | JZ L | CALL f nargs | RET | PRINTI | PRINTF | TRAP
"""
from __future__ import annotations

import sys

from ...cir.interp import (_FCMP, _ICMP, _UCMP, _Trap, _binop, _convert,
                           _unsigned, _wrap, fmt_float)
from ...cir.ir import Ty

_BIN = {'ADD': 'add', 'SUB': 'sub', 'MUL': 'mul', 'DIV_S': 'sdiv', 'DIV_U': 'udiv',
        'REM_S': 'srem', 'REM_U': 'urem', 'AND': 'and', 'OR': 'or', 'XOR': 'xor',
        'SHL': 'shl', 'SHR_S': 'ashr', 'SHR_U': 'lshr',
        'FADD': 'fadd', 'FSUB': 'fsub', 'FMUL': 'fmul', 'FDIV': 'fdiv'}
_CMP = {'EQ': 'eq', 'NE': 'ne', 'LT_S': 'slt', 'LE_S': 'sle', 'GT_S': 'sgt', 'GE_S': 'sge',
        'LT_U': 'ult', 'LE_U': 'ule', 'GT_U': 'ugt', 'GE_U': 'uge'}
_FC = {'FEQ': 'oeq', 'FNE': 'one', 'FLT': 'olt', 'FLE': 'ole', 'FGT': 'ogt', 'FGE': 'oge'}
_WIDTH = {'': Ty.I32, '64': Ty.I64, '1': Ty.I1}
_TY = {t.value: t for t in Ty}


def parse(bytecode):
    """(globals, function name -> (params, instruction list, label -> index))."""
    globs, funcs, cur = {}, {}, None
    for raw in bytecode.splitlines():
        s = raw.split(';', 1)[0].strip()
        if not s: continue
        if s.startswith('.global'):
            _, name, init = s.split()
            globs[name] = float(init) if any(c in init for c in '.ein') else int(init)
        elif s.endswith(':') and not s.startswith('L_'):
            cur = s[:-1]; funcs[cur] = ([], [], {})
        elif s.endswith(':'):
            funcs[cur][2][s[:-1]] = len(funcs[cur][1])
        elif s.startswith('.params'):
            funcs[cur][0].extend(s.split()[1:])
        else:
            funcs[cur][1].append(s.split())
    return globs, funcs


def run(bytecode, entry='main', args=()):
    globs, funcs = parse(bytecode)
    out, slots, next_slot = [], {}, [0]

    def call(name, argv):
        params, code, labels = funcs[name]
        local = dict(zip(params, argv))
        st, owned, pc = [], [], 0
        try:
            while pc < len(code):
                ins = code[pc]; pc += 1
                op, a = ins[0], ins[1:]
                base, _, suf = op.partition('.')
                if op == 'PUSHI': st.append(int(a[0]))
                elif op == 'PUSHF': st.append(float(a[0]))
                elif op == 'LOADL': st.append(local[a[0]])
                elif op == 'STOREL': local[a[0]] = st.pop()
                elif op == 'LOADG': st.append(globs.get(a[0], 0))
                elif op == 'STOREG': globs[a[0]] = st.pop()
                elif op == 'DROP': st.pop()
                elif op in ('FNEG',): st.append(-st.pop())
                elif base == 'NEG': st.append(_wrap(-st.pop(), _WIDTH[suf]))
                elif base == 'NOT': st.append(_wrap(~st.pop(), _WIDTH[suf]))
                elif base in _BIN:
                    y = st.pop(); x = st.pop()
                    st.append(_binop(_BIN[base], Ty.F64 if base[0] == 'F' else _WIDTH[suf], x, y))
                elif base in _CMP:
                    y = st.pop(); x = st.pop(); p = _CMP[base]; ty = _WIDTH[suf]
                    if p in _UCMP: x, y = _unsigned(x, ty), _unsigned(y, ty)
                    st.append(int((_ICMP.get(p) or _UCMP[p])(x, y)))
                elif op in _FC:
                    y = st.pop(); x = st.pop(); st.append(int(_FCMP[_FC[op]](x, y)))
                elif op in ('SEXT', 'ZEXT', 'TRUNC', 'SITOFP', 'FPTOSI'):
                    st.append(_convert(op.lower(), _TY[a[1]], st.pop(), _TY[a[0]]))
                elif op == 'ALLOCA':
                    sid = next_slot[0]; next_slot[0] += 1
                    slots[sid] = [0] * int(a[0]); owned.append(sid)
                    st.append((sid, 0))
                elif op == 'LOAD':
                    sid, i = st.pop(); st.append(slots[sid][i])
                elif op == 'STORE':
                    (sid, i), v = st.pop(), st.pop(); slots[sid][i] = v
                elif op == 'GEP':
                    i = st.pop(); sid, _ = st.pop()
                    if not 0 <= i < len(slots[sid]): raise _Trap()
                    st.append((sid, i))
                elif op == 'JMP': pc = labels[a[0]]
                elif op == 'JZ':
                    if st.pop() == 0: pc = labels[a[0]]
                elif op == 'CALL':
                    n = int(a[1]); argv2 = st[len(st) - n:] if n else []
                    del st[len(st) - n:]
                    st.append(call(a[0], argv2))
                elif op == 'RET': return st.pop() if st else 0
                elif op == 'PRINTI': out.append(str(st.pop()))
                elif op == 'PRINTF': out.append(fmt_float(st.pop()))
                elif op == 'TRAP': raise _Trap()
                else: raise ValueError(f'unknown opcode {op}')
            return 0
        finally:
            for sid in owned: del slots[sid]

    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 20000))
    try:
        return call(entry, list(args)), '\n'.join(out), False
    except _Trap:
        return 0, '\n'.join(out), True
    finally:
        sys.setrecursionlimit(old)
