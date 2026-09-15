"""Constant folding and propagation.

Module M4.  Owner: Member 3.
Status: IMPLEMENTED for integer arithmetic, bitwise and shift operations,
        integer and float comparisons, widening conversions, and constant
        branch folding.

Deliberately *not* folded, and why
----------------------------------
An optimisation may not turn a program that traps into one that does not, nor
silently adopt the host's arithmetic where CIR has defined its own:

  * ``sdiv``/``srem``/``udiv``/``urem`` fold only when the operands prove the
    operation cannot trap -- a non-zero divisor, and not ``INT_MIN / -1``
    (docs/divergence.md rows 1 and 2).  Folding the trapping cases would
    delete a trap the program is defined to take, so those are left alone for
    the back end to guard.
  * ``fdiv`` is not folded: CIR does not yet fix what division by zero means
    for floats, and Python would raise where IEEE-754 gives an infinity.
  * Nothing is folded when an operand is NaN, for the same reason -- the
    ordered/unordered distinction is the back end's to realise.
  * Every integer result goes through ``wrap_int``, because CIR defines
    overflow as two's-complement wraparound (row 4) and Python's integers are
    unbounded.  Shift counts go through the same modulo-width rule as row 3.

Why the pass is two-phase
-------------------------
CIR is not in SSA form, so a register may in principle be assigned more than
once.  Only registers with exactly one definition in the function are
propagated; for those, well-formedness rule 4 (every use is defined on every
path that reaches it) means every path to a use runs through that one
definition, which is what makes substituting the constant sound.  Analysis
therefore completes before any rewriting, so the IR is never left in a state
where a definition has been deleted but a use has not been substituted.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional

from ..cir.ir import (ConstFloat, ConstInt, Function, INT_WIDTH, Instr, Module,
                      Reg, Ty, Value, wrap_int)

_INT_BIN = {
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "and": lambda a, b: a & b,
    "or": lambda a, b: a | b,
    "xor": lambda a, b: a ^ b,
}
_FLOAT_BIN = {
    "fadd": lambda a, b: a + b,
    "fsub": lambda a, b: a - b,
    "fmul": lambda a, b: a * b,
}
_ICMP = {
    "eq": lambda a, b: a == b, "ne": lambda a, b: a != b,
    "slt": lambda a, b: a < b, "sle": lambda a, b: a <= b,
    "sgt": lambda a, b: a > b, "sge": lambda a, b: a >= b,
}
_FCMP = {
    "oeq": lambda a, b: a == b, "one": lambda a, b: a != b,
    "olt": lambda a, b: a < b, "ole": lambda a, b: a <= b,
    "ogt": lambda a, b: a > b, "oge": lambda a, b: a >= b,
}


def _as_unsigned(value: int, ty: Ty) -> int:
    return value & ((1 << INT_WIDTH[ty]) - 1)


def _fold_divrem(op: str, lhs: int, rhs: int, ty: Ty) -> Optional[ConstInt]:
    """Fold a division or remainder only where it provably cannot trap.

    CIR traps on a zero divisor and on ``INT_MIN / -1``; returning None for
    those leaves the instruction in place so the back end's guard still fires.
    Python's ``//`` floors, so the signed cases are computed on magnitudes and
    the sign reapplied, which is the truncate-toward-zero rule that LLVM,
    WebAssembly and C all use.
    """
    if rhs == 0:
        return None
    if op in ("sdiv", "srem") and lhs == -(1 << (INT_WIDTH[ty] - 1)) and rhs == -1:
        return None
    if op in ("udiv", "urem"):
        lhs, rhs = _as_unsigned(lhs, ty), _as_unsigned(rhs, ty)
        result = lhs // rhs if op == "udiv" else lhs % rhs
        return ConstInt(wrap_int(result, ty), ty)
    quotient = abs(lhs) // abs(rhs)
    if (lhs < 0) != (rhs < 0):
        quotient = -quotient
    if op == "sdiv":
        return ConstInt(wrap_int(quotient, ty), ty)
    return ConstInt(wrap_int(lhs - quotient * rhs, ty), ty)


def fold(ins: Instr) -> Optional[Value]:
    """The constant this instruction computes, or None if it is not foldable."""
    ty = ins.ty
    args = ins.args
    if any(not isinstance(a, (ConstInt, ConstFloat)) for a in args):
        return None
    if any(isinstance(a, ConstFloat) and math.isnan(a.value) for a in args):
        return None
    op = ins.op

    if op in _INT_BIN and len(args) == 2:
        return ConstInt(wrap_int(_INT_BIN[op](args[0].value, args[1].value), ty), ty)

    if op in ("shl", "ashr", "lshr") and len(args) == 2:
        count = args[1].value & (INT_WIDTH[ty] - 1)      # row 3: mod width
        value = args[0].value
        if op == "shl":
            return ConstInt(wrap_int(value << count, ty), ty)
        if op == "ashr":
            return ConstInt(wrap_int(value >> count, ty), ty)
        return ConstInt(wrap_int(_as_unsigned(value, ty) >> count, ty), ty)

    if op in ("sdiv", "udiv", "srem", "urem") and len(args) == 2:
        return _fold_divrem(op, args[0].value, args[1].value, ty)

    if op == "not" and len(args) == 1:
        return ConstInt(wrap_int(~args[0].value, ty), ty)

    if op == "neg" and len(args) == 1:
        if ty is Ty.F64:
            return ConstFloat(-args[0].value)
        return ConstInt(wrap_int(-args[0].value, ty), ty)

    if op in _FLOAT_BIN and len(args) == 2:
        return ConstFloat(_FLOAT_BIN[op](float(args[0].value),
                                         float(args[1].value)))

    if op.startswith("icmp.") and len(args) == 2:
        pred = op.split(".", 1)[1]
        a, b = args[0].value, args[1].value
        if pred in _ICMP:
            return ConstInt(1 if _ICMP[pred](a, b) else 0, Ty.I1)
        # The unsigned predicates are the signed ones applied to the operands
        # reinterpreted as unsigned, which is exactly what the hardware does.
        signed = {"ult": "slt", "ule": "sle", "ugt": "sgt", "uge": "sge"}[pred]
        ua, ub = _as_unsigned(a, ty), _as_unsigned(b, ty)
        return ConstInt(1 if _ICMP[signed](ua, ub) else 0, Ty.I1)

    if op.startswith("fcmp.") and len(args) == 2:
        pred = op.split(".", 1)[1]
        if pred in _FCMP:
            return ConstInt(
                1 if _FCMP[pred](float(args[0].value), float(args[1].value))
                else 0, Ty.I1)
        return None

    if op in ("sext", "trunc") and len(args) == 1:
        return ConstInt(wrap_int(args[0].value, ty), ty)
    if op == "zext" and len(args) == 1:
        return ConstInt(wrap_int(_as_unsigned(args[0].value, args[0].ty), ty), ty)
    if op == "sitofp" and len(args) == 1:
        return ConstFloat(float(args[0].value))

    return None


def _single_defs(fn: Function) -> set:
    """Registers defined exactly once in the function."""
    counts: Dict[str, int] = {}
    for block in fn.blocks:
        for ins in block.instrs:
            if ins.dest is not None:
                counts[ins.dest.name] = counts.get(ins.dest.name, 0) + 1
    return {name for name, n in counts.items() if n == 1}


def _substitute(ins: Instr, known: Dict[str, Value]) -> bool:
    changed = False
    for i, operand in enumerate(ins.args):
        if isinstance(operand, Reg) and operand.name in known:
            ins.args[i] = known[operand.name]
            changed = True
    return changed


def _fold_branch(block, known: Dict[str, Value]) -> bool:
    """Rewrite ``br.cond`` on a constant into an unconditional ``br``.

    This is what removes the dead arm of ``while (false)`` and of an ``if``
    whose condition folded; dead-code elimination then drops the block the
    branch no longer reaches.
    """
    term = block.terminator
    if term is None or term.op != "br.cond":
        return False
    cond = term.args[0]
    if isinstance(cond, Reg):
        cond = known.get(cond.name)
    if not isinstance(cond, ConstInt):
        return False
    taken = term.labels[0] if cond.value & 1 else term.labels[1]
    block.instrs[-1] = Instr("br", Ty.VOID, None, [], labels=[taken])
    return True


def run_function(fn: Function) -> bool:
    """Fold and propagate over one function.  Returns True if anything changed."""
    changed = False
    single = _single_defs(fn)

    while True:
        # Phase A: analysis only.  Nothing is deleted while uses may remain.
        known: Dict[str, Value] = {}
        for block in fn.blocks:
            for ins in block.instrs:
                args = [known.get(a.name, a) if isinstance(a, Reg) else a
                        for a in ins.args]
                if ins.dest is None or ins.dest.name not in single:
                    continue
                probe = Instr(ins.op, ins.ty, ins.dest, args)
                value = fold(probe)
                if value is not None:
                    known[ins.dest.name] = value

        # Phase B: rewrite every use, then drop the definitions that are now
        # redundant.
        round_changed = False
        for block in fn.blocks:
            kept: List[Instr] = []
            for ins in block.instrs:
                round_changed |= _substitute(ins, known)
                if ins.dest is not None and ins.dest.name in known:
                    round_changed = True
                    continue
                kept.append(ins)
            block.instrs = kept
            round_changed |= _fold_branch(block, known)

        changed |= round_changed
        if not round_changed:
            return changed


def run(module: Module) -> Module:
    """Constant-fold and propagate through every function.  Mutates in place."""
    for fn in module.functions:
        run_function(fn)
    return module
