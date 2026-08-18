"""Register-to-stack lowering.

Module M6a.  Owner: Member 4.
Status: PROTOTYPE.  The naive schema and the peephole rule are both
        implemented and tested; production hardening (calls, memory ops,
        multi-type locals) is Weeks 6-7.  This pass is shared by the
        WebAssembly back end (M6c) and the bytecode back end (M7).

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

Measured on the abs example of Figure 2 (run `python -m src.driver
--demo-stack` to reproduce): the naive schema emits 14 stack instructions and
the peephole rule reduces this to 10, a 28.6% reduction.  The corresponding
before/after figure across the whole corpus is a Review 3 result.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Set

from ...cir.ir import (BasicBlock, ConstFloat, ConstInt, Function, GlobalRef,
                       Instr, Reg, Ty)


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


# --------------------------------------------------------------------------
# Opcode mapping
# --------------------------------------------------------------------------
_PREFIX = {Ty.I1: "i32", Ty.I32: "i32", Ty.I64: "i64",
           Ty.F64: "f64", Ty.PTR: "i32", Ty.VOID: "i32"}

_CMP = {"eq": "eq", "ne": "ne",
        "slt": "lt_s", "sle": "le_s", "sgt": "gt_s", "sge": "ge_s",
        "ult": "lt_u", "ule": "le_u", "ugt": "gt_u", "uge": "ge_u",
        "oeq": "eq", "one": "ne", "olt": "lt", "ole": "le",
        "ogt": "gt", "oge": "ge"}

_BINOP = {"add": "add", "sub": "sub", "mul": "mul",
          "sdiv": "div_s", "udiv": "div_u", "srem": "rem_s", "urem": "rem_u",
          "fadd": "add", "fsub": "sub", "fmul": "mul", "fdiv": "div",
          "and": "and", "or": "or", "xor": "xor",
          "shl": "shl", "ashr": "shr_s", "lshr": "shr_u"}


def opcode_for(instr: Instr) -> StackOp:
    """Map one CIR opcode to its stack-machine opcode.

    `instr.ty` is the operand type for comparisons and the result type
    otherwise, which is why a single prefix lookup serves both.
    """
    p = _PREFIX[instr.ty]
    op = instr.op
    if op == "ret":
        return StackOp("return")
    if op == "br":
        return StackOp("br", instr.labels[0])
    if op == "br.cond":
        return StackOp("br_if", instr.labels[0])
    if op.startswith(("icmp.", "fcmp.")):
        return StackOp(f"{p}.{_CMP[op.split('.', 1)[1]]}")
    if op in _BINOP:
        return StackOp(f"{p}.{_BINOP[op]}")
    if op == "call":
        return StackOp("call", f"${instr.callee}")
    if op.startswith("print."):
        return StackOp("call", f"$print_{op.split('.', 1)[1]}")
    if op == "trap":
        return StackOp("unreachable")
    return StackOp(f"{p}.{op}")


def _push_operand(value, local_of: Dict[str, str]) -> StackOp:
    if isinstance(value, (ConstInt, ConstFloat)):
        return StackOp(f"{_PREFIX[value.ty]}.const", value.value)
    if isinstance(value, Reg):
        return StackOp("local.get", local_of[value.name])
    if isinstance(value, GlobalRef):
        return StackOp("global.get", f"${value.name}")
    raise TypeError(f"cannot push operand of type {type(value).__name__}")


# --------------------------------------------------------------------------
# The pass
# --------------------------------------------------------------------------
def lower_block_naive(block: BasicBlock, local_of: Dict[str, str]) -> List[StackOp]:
    """The naive schema: push every operand, apply the opcode, park the result.

    Always correct for any three-address sequence, and deliberately kept as a
    separate function so the peephole rule can be measured against it.
    """
    out: List[StackOp] = []
    for instr in block.instrs:
        for operand in instr.args:
            out.append(_push_operand(operand, local_of))
        out.append(opcode_for(instr))
        if instr.dest is not None:
            out.append(StackOp("local.set", local_of[instr.dest.name]))
    return out


def block_use_counts(block: BasicBlock, local_of: Dict[str, str]) -> Dict[str, int]:
    """How many times each local is read inside this block."""
    counts: Dict[str, int] = {}
    for instr in block.instrs:
        for operand in instr.args:
            if isinstance(operand, Reg):
                local = local_of[operand.name]
                counts[local] = counts.get(local, 0) + 1
    return counts


def block_live_out(fn: Function, block: BasicBlock,
                   local_of: Dict[str, str]) -> Set[str]:
    """Locals read by some block other than this one.

    A deliberately conservative over-approximation: a real liveness analysis
    over the CFG (M4, Week 10) will shrink this set and so expose more
    peephole opportunities.  Over-approximating is safe -- it only ever
    suppresses an optimisation, never enables an unsound one.
    """
    live: Set[str] = set()
    for other in fn.blocks:
        if other is block:
            continue
        for instr in other.instrs:
            for operand in instr.args:
                if isinstance(operand, Reg):
                    live.add(local_of[operand.name])
    return live


def lower_block(block: BasicBlock, local_of: Dict[str, str],
                use_count: Dict[str, int] | None = None,
                live_out: Set[str] | None = None) -> List[StackOp]:
    """Lower one CIR basic block to a peephole-cleaned stack sequence."""
    naive = lower_block_naive(block, local_of)
    if use_count is None:
        use_count = block_use_counts(block, local_of)
    if live_out is None:
        live_out = set()
    return peephole(naive, use_count, live_out)


def local_table(fn: Function) -> Dict[str, str]:
    """Assign a stack-machine local to every parameter and defined register."""
    table: Dict[str, str] = {p.name: f"${p.name}" for p in fn.params}
    for block in fn.blocks:
        for instr in block.instrs:
            if instr.dest is not None:
                table.setdefault(instr.dest.name, f"${instr.dest.name}")
    return table


def lower_function(fn: Function) -> Dict[str, List[StackOp]]:
    """Lower every block of a function.  Returns label -> stack sequence."""
    local_of = local_table(fn)
    return {
        b.label: lower_block(b, local_of,
                             block_use_counts(b, local_of),
                             block_live_out(fn, b, local_of))
        for b in fn.blocks
    }


def count_function(fn: Function) -> tuple[int, int]:
    """(naive instruction count, peepholed instruction count) for a function."""
    local_of = local_table(fn)
    naive = sum(len(lower_block_naive(b, local_of)) for b in fn.blocks)
    tuned = sum(len(seq) for seq in lower_function(fn).values())
    return naive, tuned
