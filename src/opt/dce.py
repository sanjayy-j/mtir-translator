"""Dead-code elimination over the CFG.

Module M4.  Owner: Member 3.
Status: IMPLEMENTED -- unreachable-block removal and dead-definition removal.

Two eliminations, one pass
--------------------------
1. **Unreachable blocks.**  A block with no path from the entry block cannot
   execute, so it and its edges are removed.  These blocks are not
   pathological: the builder creates one whenever a statement follows a
   ``return``, and constant folding creates one every time it turns a
   ``br.cond`` on a constant into a ``br``, which is what makes
   ``while (false) { ... }`` disappear entirely.

2. **Dead definitions.**  An instruction whose destination is never read, and
   whose execution is not itself observable, has no effect and is removed.
   Observability is the whole question: ``store``, ``call``, ``print.*`` and
   ``trap`` stay regardless of whether anything reads their result, because
   deleting them would change what the program does.  A ``call`` is kept even
   when its result is unused because CIR has no purity annotation -- assuming
   a call is pure would be exactly the kind of unsound shortcut this project
   is about avoiding.

Both run to a fixed point, since removing one definition can kill another and
removing one block can make another unreachable.  Liveness is computed over
the whole function rather than per block, which is conservative across loop
back edges and is all this milestone needs.
"""

from __future__ import annotations

from typing import List, Set

from ..cir.ir import Function, Instr, Module, Reg, SIDE_EFFECTING
from ..cir.cfg import reachable


def _used_registers(fn: Function) -> Set[str]:
    return {a.name for b in fn.blocks for i in b.instrs
            for a in i.args if isinstance(a, Reg)}


def remove_unreachable_blocks(fn: Function) -> bool:
    """Drop blocks with no path from the entry block."""
    live = reachable(fn)
    if len(live) == len(fn.blocks):
        return False
    fn.blocks = [b for b in fn.blocks if b.label in live]
    return True


def remove_dead_definitions(fn: Function) -> bool:
    """Drop instructions whose result is unread and whose execution is unobservable."""
    used = _used_registers(fn)
    changed = False
    for block in fn.blocks:
        kept: List[Instr] = []
        for ins in block.instrs:
            dead = (ins.dest is not None
                    and ins.dest.name not in used
                    and ins.op not in SIDE_EFFECTING
                    and not ins.is_terminator())
            if dead:
                changed = True
                continue
            kept.append(ins)
        block.instrs = kept
    return changed


def run_function(fn: Function) -> bool:
    changed = False
    while True:
        round_changed = remove_unreachable_blocks(fn)
        round_changed |= remove_dead_definitions(fn)
        changed |= round_changed
        if not round_changed:
            return changed


def run(module: Module) -> Module:
    """Eliminate dead code in every function.  Mutates in place."""
    for fn in module.functions:
        run_function(fn)
    return module
