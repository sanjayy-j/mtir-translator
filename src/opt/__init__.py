"""CIR optimisation passes.

Module M4.  Owner: Member 3.

The passes are ordered so that each one creates work for the next: constant
folding turns subexpressions into constants, which turns the operations around
them into identity copies, which copy propagation removes, which leaves
definitions nobody reads for dead-code elimination.  Folding a ``br.cond``
into a ``br`` likewise only pays off once DCE drops the block it no longer
reaches, so the sequence is repeated to a fixed point rather than run once.

Every pass preserves CIR semantics as fixed in docs/divergence.md -- in
particular none of them may remove a trap.  Each pass documents what it
refuses to transform and why.
"""

from __future__ import annotations

from ..cir.ir import Module
from . import constfold, copyprop, dce

PASSES = (
    ("constfold", constfold.run_function),
    ("copyprop", copyprop.run_function),
    ("dce", dce.run_function),
)

MAX_ROUNDS = 8


def run(module: Module, level: int = 1) -> Module:
    """Run the pass pipeline over a module in place and return it.

    ``level`` 0 is a no-op, so the driver can ask for unoptimised CIR without
    a special case.  The round limit is a safety net: every pass is monotone
    (it only ever removes instructions), so the loop terminates on its own,
    and the cap just bounds the work on a pathological input.
    """
    if level <= 0:
        return module
    for fn in module.functions:
        for _ in range(MAX_ROUNDS):
            if not any(run_pass(fn) for _, run_pass in PASSES):
                break
    return module


def count_instructions(module: Module) -> int:
    """Total instruction count, for before/after measurements."""
    return sum(len(b.instrs) for f in module.functions for b in f.blocks)
