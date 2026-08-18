"""Register-to-stack lowering.

Module M6a.  Owner: Member 4.
Status: DESIGNED (pseudocode below, from Listing 3 of the Review 1 report).
        Implementation Weeks 6-7.  The peephole rule is implemented and tested
        already, because it is the part with a non-obvious correctness
        condition and it is shared by two back ends.

Why this pass exists
--------------------
A three-address instruction assumes its operands are addressable at any time.
A stack machine can only reach the top of the stack.  Every CIR value must
therefore either be consumed immediately by the next instruction, or be parked
in a local.  Parking everything is always correct but produces one
local.set/local.get pair per temporary; deciding what can stay on the stack is
a liveness question over the block.

Naive schema (always correct, for any three-address sequence)
-------------------------------------------------------------
    procedure LowerBlock(B, localOf):
        out <- empty instruction list
        for each instruction I in B, in order:
            for each operand O of I, in left-to-right order:
                if O is a constant:  emit CONST(type(O), value(O))
                else:                emit LOCAL_GET(localOf[O])
            emit OPCODE(I)                    // consumes operands, pushes result
            if I defines a register R:
                emit LOCAL_SET(localOf[R])    // result parked in a local
        return Peephole(out)

Peephole rule
-------------
A value that is defined and then immediately consumed exactly once, in the same
block, never needs to leave the operand stack:

    for each adjacent pair (LOCAL_SET l, LOCAL_GET l) in seq:
        if useCount(l) == 1 and l is not live-out of the block:
            delete both instructions

On the abs example this takes the naive 11 instructions down to 7.  The
before/after instruction count across the corpus is a Review 3 result.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Set


@dataclass(frozen=True)
class StackOp:
    """One stack-machine instruction, target-neutral.

    The WebAssembly emitter (M6c) and the bytecode emitter (M7) both consume
    this, which is why the third target costs so little.
    """
    op: str                 # const | local.get | local.set | add | i32.lt_s | ...
    arg: str | int | None = None

    def __str__(self) -> str:
        return self.op if self.arg is None else f"{self.op} {self.arg}"


def peephole(seq: List[StackOp], use_count: Dict[str, int],
             live_out: Set[str]) -> List[StackOp]:
    """Delete `local.set l` / `local.get l` pairs that cannot be observed.

    Correctness condition: the local is read exactly once in the whole block
    and is not live on exit, so nothing else can observe the value in the
    local rather than on the stack.
    """
    out: List[StackOp] = []
    i = 0
    while i < len(seq):
        cur = seq[i]
        nxt = seq[i + 1] if i + 1 < len(seq) else None
        if (
            nxt is not None
            and cur.op == "local.set"
            and nxt.op == "local.get"
            and cur.arg == nxt.arg
            and use_count.get(str(cur.arg), 0) == 1
            and str(cur.arg) not in live_out
        ):
            i += 2          # drop both; the value simply stays on the stack
            continue
        out.append(cur)
        i += 1
    return out


def lower_block(block, local_of):  # pragma: no cover - Week 6-7
    """Lower one CIR basic block to a stack sequence.

    TODO(Member 4, Week 6): implement the naive schema above, then call
    peephole() with the block's use counts and live-out set.
    """
    raise NotImplementedError(
        "register-to-stack lowering is scheduled for Weeks 6-7 (M6a, Member 4)")
