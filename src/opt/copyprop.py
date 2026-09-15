"""Copy propagation.

Module M4.  Owner: Member 3.
Status: IMPLEMENTED for the identity-algebra copies listed below.

What counts as a copy in CIR
----------------------------
CIR has no move instruction -- three-address code with a register file does
not need one -- so there is nothing for a textbook copy-propagation pass to
chase.  What *does* produce copies is arithmetic with an identity operand:

    %t = add i32 %x, 0        %t = mul i32 %x, 1        %t = shl i32 %x, 0
    %t = sub i32 %x, 0        %t = sdiv i32 %x, 1       %t = or  i32 %x, 0
    %t = and i32 %x, -1       %t = xor i32 %x, 0

Each of these defines a register that is just another name for ``%x``.  This
pass rewrites every use of ``%t`` to ``%x`` and then drops the instruction --
after the rewrite nothing refers to ``%t``, and none of these opcodes has a
side effect, so nothing is left for dead-code elimination to discover.

Such instructions come mostly from the constant folder: once a subexpression
folds to 0 or 1, what surrounded it becomes an identity.  Running copy
propagation after folding rather than before is therefore not arbitrary.

Soundness
---------
Only registers with exactly one definition in the function are propagated.
Well-formedness rule 4 then means every path reaching a use of ``%t`` runs
through that definition, and therefore through a point where ``%x`` is
already defined -- so replacing the use cannot move a read above its write.

Not included, deliberately: ``srem %x, 1`` is 0 rather than a copy (constant
folding's job), and ``sdiv``/``udiv`` are treated as copies only for the
divisor 1, where neither the zero nor the ``INT_MIN / -1`` trap can arise.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from ..cir.ir import ConstInt, Function, Instr, Module, Reg, Ty, Value, wrap_int

# op -> which operand may be the identity: 'rhs' only, or 'either' when the
# operation is commutative.
_IDENTITY = {
    "add": (0, "either"), "or": (0, "either"), "xor": (0, "either"),
    "sub": (0, "rhs"), "shl": (0, "rhs"), "ashr": (0, "rhs"),
    "lshr": (0, "rhs"),
    "mul": (1, "either"), "sdiv": (1, "rhs"), "udiv": (1, "rhs"),
    "and": (-1, "either"),
}


def copy_source(ins: Instr) -> Optional[Value]:
    """The value this instruction is a copy of, or None if it is not a copy."""
    if ins.dest is None or ins.op not in _IDENTITY or len(ins.args) != 2:
        return None
    if ins.ty not in (Ty.I1, Ty.I32, Ty.I64):
        return None                       # float identities are not exact
    unit, where = _IDENTITY[ins.op]
    unit = wrap_int(unit, ins.ty)
    lhs, rhs = ins.args

    if isinstance(rhs, ConstInt) and wrap_int(rhs.value, ins.ty) == unit:
        return lhs
    if where == "either" and isinstance(lhs, ConstInt) \
            and wrap_int(lhs.value, ins.ty) == unit:
        return rhs
    return None


def _single_defs(fn: Function) -> set:
    counts: Dict[str, int] = {}
    for block in fn.blocks:
        for ins in block.instrs:
            if ins.dest is not None:
                counts[ins.dest.name] = counts.get(ins.dest.name, 0) + 1
    return {name for name, n in counts.items() if n == 1}


def run_function(fn: Function) -> bool:
    """Propagate identity copies through one function."""
    changed = False
    single = _single_defs(fn)

    while True:
        aliases: Dict[str, Value] = {}
        for block in fn.blocks:
            for ins in block.instrs:
                if ins.dest is None or ins.dest.name not in single:
                    continue
                source = copy_source(ins)
                if source is None:
                    continue
                # Chase through an alias already found, so a chain of
                # identities collapses in one round.
                while isinstance(source, Reg) and source.name in aliases:
                    source = aliases[source.name]
                if not (isinstance(source, Reg) and source.name == ins.dest.name):
                    aliases[ins.dest.name] = source

        if not aliases:
            return changed

        round_changed = False
        for block in fn.blocks:
            kept: List[Instr] = []
            for ins in block.instrs:
                for i, operand in enumerate(ins.args):
                    if isinstance(operand, Reg) and operand.name in aliases:
                        ins.args[i] = aliases[operand.name]
                        round_changed = True
                if ins.dest is not None and ins.dest.name in aliases:
                    round_changed = True
                    continue          # the definition is now unreachable by name
                kept.append(ins)
            block.instrs = kept

        changed |= round_changed
        if not round_changed:
            return changed


def run(module: Module) -> Module:
    """Propagate identity copies through every function.  Mutates in place."""
    for fn in module.functions:
        run_function(fn)
    return module
