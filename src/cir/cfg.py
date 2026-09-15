"""CIR control-flow graph: construction, queries and structural checks.

Module M3a (CFG half).  Owner: Member 3.
Status: COMPLETE for the constructs MiniLang can produce.

Why this is a separate module
-----------------------------
``Function.cfg()`` in ir.py answers "what are the successors of each block",
which is all the printer needs.  Everything downstream needs more than that:

  * the LLVM back end needs to know a block is reachable before it emits it,
    because LLVM rejects a use of a register that is not defined on every
    path into the block;
  * dead-code elimination (M4) needs reachability from the entry block;
  * the verifier (M3b, Member 2) needs the structural rules of
    docs/cir-spec.md section 6 as checkable predicates.

The edges are *explicit*: a block's successors are exactly the labels of its
terminator, so building the graph is a scan, not an analysis.  That is the
whole point of the "explicit control flow" design decision -- the LLVM back
end consumes these edges unchanged, and the WebAssembly back end (M6b,
Member 4) has a real graph to restructure.
"""

from __future__ import annotations

from typing import Dict, List, Set

from .ir import Function, Reg


# --------------------------------------------------------------------------
# Queries
# --------------------------------------------------------------------------
def successors(fn: Function) -> Dict[str, List[str]]:
    """Successor adjacency list, in terminator order (so `then` before `else`)."""
    return fn.cfg()


def predecessors(fn: Function) -> Dict[str, List[str]]:
    """Predecessor adjacency list.

    Built by inverting the successor list rather than stored, so it cannot go
    stale when a pass rewrites a terminator.  Blocks are visited in definition
    order, which keeps the result deterministic.
    """
    preds: Dict[str, List[str]] = {b.label: [] for b in fn.blocks}
    for block in fn.blocks:
        for target in block.successors():
            if target in preds and block.label not in preds[target]:
                preds[target].append(block.label)
    return preds


def reachable(fn: Function) -> Set[str]:
    """Labels reachable from the entry block by following terminator edges."""
    if fn.entry is None:
        return set()
    seen: Set[str] = set()
    stack = [fn.entry.label]
    while stack:
        label = stack.pop()
        if label in seen:
            continue
        seen.add(label)
        block = fn.block(label)
        if block is not None:
            stack.extend(block.successors())
    return seen


# --------------------------------------------------------------------------
# Structural checks
# --------------------------------------------------------------------------
def check_cfg(fn: Function) -> List[str]:
    """Rules 1, 2, 3 and 5 of docs/cir-spec.md section 6, plus label uniqueness.

    Returns a list of human-readable diagnostics; empty means the CFG is
    structurally sound.  These are the checks that are about the *graph*; the
    type and def-use rules live in check_defs() and in the verifier (M3b).
    """
    errors: List[str] = []
    if not fn.blocks:
        return [f"@{fn.name}: function has no blocks"]

    seen: Set[str] = set()
    for block in fn.blocks:
        if block.label in seen:
            errors.append(f"@{fn.name}: duplicate block label {block.label!r}")
        seen.add(block.label)

    for block in fn.blocks:
        where = f"@{fn.name}:{block.label}"
        # Rule 1: exactly one terminator, and it is last.
        if block.terminator is None:
            errors.append(f"{where}: block does not end in a terminator")
        # Rule 2: nothing follows a terminator.
        for instr in block.instrs[:-1]:
            if instr.is_terminator():
                errors.append(
                    f"{where}: instruction {instr.op!r} appears after a terminator")
        # Rule 3: every branch target names a block in this function.
        for target in block.successors():
            if target not in seen:
                errors.append(f"{where}: branch to undefined block {target!r}")

    # Rule 5: the entry block has no predecessors, so it cannot be re-entered.
    entry = fn.entry.label
    for pred in predecessors(fn).get(entry, []):
        errors.append(
            f"@{fn.name}: entry block {entry!r} has a predecessor {pred!r}")

    return errors


def check_defs(fn: Function) -> List[str]:
    """Rule 4: every register is defined on every path that reaches its use.

    A forward dataflow over the CFG whose lattice is "set of registers defined
    so far" and whose meet is *intersection* -- a register only counts as
    available at a block if every predecessor makes it available.  Parameters
    are available on entry.  Unreachable blocks are skipped: they have no
    incoming paths, so the property is vacuous there and reporting them would
    duplicate what dead-code elimination removes.

    CIR is not in SSA form, so a register may be assigned more than once; this
    check is about definedness, not about single assignment.
    """
    errors: List[str] = []
    if fn.entry is None:
        return errors

    live = reachable(fn)
    preds = predecessors(fn)
    params = {p.name for p in fn.params}

    def defined_by(label: str, incoming: Set[str]) -> Set[str]:
        out = set(incoming)
        block = fn.block(label)
        for instr in block.instrs:
            if instr.dest is not None:
                out.add(instr.dest.name)
        return out

    # Iterate to a fixed point.  The lattice is finite and the transfer
    # function is monotone, so this terminates; MiniLang loops give at most a
    # couple of rounds in practice.
    entry_in: Dict[str, Set[str]] = {}
    all_regs = params | {i.dest.name for b in fn.blocks for i in b.instrs
                         if i.dest is not None}
    for label in live:
        entry_in[label] = set() if label == fn.entry.label else set(all_regs)
    entry_in[fn.entry.label] = set(params)

    changed = True
    while changed:
        changed = False
        for label in sorted(live):
            if label == fn.entry.label:
                continue
            incoming = [defined_by(p, entry_in[p]) for p in preds[label]
                        if p in live]
            new = set.intersection(*incoming) if incoming else set()
            if new != entry_in[label]:
                entry_in[label] = new
                changed = True

    for label in sorted(live):
        available = set(entry_in[label])
        for instr in fn.block(label).instrs:
            for operand in instr.args:
                if isinstance(operand, Reg) and operand.name not in available:
                    errors.append(
                        f"@{fn.name}:{label}: use of %{operand.name} before it "
                        f"is defined on every path")
            if instr.dest is not None:
                available.add(instr.dest.name)
    return errors


def check_function(fn: Function) -> List[str]:
    """Every CFG-level check, in the order the diagnostics read best."""
    errors = check_cfg(fn)
    if errors:
        # A broken graph makes the dataflow meaningless, so stop here rather
        # than emit a cascade of follow-on errors.
        return errors
    return check_defs(fn)
