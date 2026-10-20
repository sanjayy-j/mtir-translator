"""CFG -> nested block/loop/if regions.

Module M6b.  Owner: Member 4.

WebAssembly has no branch to an arbitrary label: only branches out of an
enclosing block/loop/if.  A CFG must therefore be re-expressed as nested
regions.  This is the dominator-tree algorithm of Ramsey, "Beyond Relooper:
recursive translation of unstructured control flow to structured control
flow" (ICFP 2022), for reducible CFGs:

  * blocks are numbered in reverse postorder (RPO); an edge u -> v with
    rpo(v) <= rpo(u) is a back edge, and v is a loop header;
  * a block with two or more forward in-edges is a merge node;
  * the translation of block x is
        do_tree(x)  = loop $L.x { node_within(x, merges) }   if x is a header
                    = node_within(x, merges)                  otherwise
        node_within(x, [y, ...rest])
                    = block $B.y { node_within(x, rest) } ; do_tree(y)
        node_within(x, [])
                    = x's instructions, then its terminator
    where `merges` are x's dominator-tree children that are merge nodes,
    outermost (highest RPO number) first;
  * a branch to v is `br $L.v` if it is a back edge, `br $B.v` if v is a
    merge node, and otherwise v's code inlined in place (v then has exactly
    one forward predecessor, so it is emitted exactly once).

Every forward predecessor of a merge node y is dominated by idom(y), so it is
emitted inside `block $B.y`; every back edge u -> v has u dominated by v, so
it is emitted inside `loop $L.v`.  That is why every br has an enclosing
target.

Irreducible CFGs cannot arise from MiniLang (there is no goto, and no pass
adds edges); structure() raises Irreducible and the emitter falls back to its
program-counter dispatch loop.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List

from ...cir.ir import BasicBlock, Function, Instr, Value


class Irreducible(Exception):
    """The CFG has a loop with more than one entry."""


@dataclass
class Block:            # block $B.label { body }   -- br exits to after it
    label: str
    body: list = field(default_factory=list)


@dataclass
class Loop:             # loop $L.label { body }    -- br re-enters it
    label: str
    body: list = field(default_factory=list)


@dataclass
class Code:             # the non-terminator instructions of one CIR block
    block: BasicBlock


@dataclass
class If:
    cond: Value
    then: list
    else_: list


@dataclass
class Br:
    target: str
    kind: str           # 'block' | 'loop'


@dataclass
class Return:
    instr: Instr


def reverse_postorder(fn: Function) -> List[str]:
    succ = {b.label: b.successors() for b in fn.blocks}
    seen, order = set(), []
    stack = [(fn.entry.label, iter(succ[fn.entry.label]))]
    seen.add(fn.entry.label)
    while stack:
        node, it = stack[-1]
        nxt = next((s for s in it if s not in seen), None)
        if nxt is None:
            order.append(node)
            stack.pop()
        else:
            seen.add(nxt)
            stack.append((nxt, iter(succ[nxt])))
    return order[::-1]


def dominators(rpo: List[str], preds: Dict[str, List[str]]) -> Dict[str, str]:
    """Immediate dominators (Cooper, Harvey and Kennedy's iterative algorithm)."""
    index = {l: i for i, l in enumerate(rpo)}
    idom = {rpo[0]: rpo[0]}

    def intersect(a, b):
        while a != b:
            while index[a] > index[b]: a = idom[a]
            while index[b] > index[a]: b = idom[b]
        return a

    changed = True
    while changed:
        changed = False
        for v in rpo[1:]:
            done = [p for p in preds[v] if p in idom]
            new = done[0]
            for p in done[1:]:
                new = intersect(p, new)
            if idom.get(v) != new:
                idom[v] = new
                changed = True
    return idom


def _dominates(idom, a, b):
    while True:
        if a == b: return True
        if idom[b] == b: return False
        b = idom[b]


def structure(fn: Function) -> list:
    """The function body as a tree of Block/Loop/Code/If/Br/Return nodes."""
    blocks = {b.label: b for b in fn.blocks}
    rpo = reverse_postorder(fn)                # unreachable blocks are dropped
    index = {l: i for i, l in enumerate(rpo)}
    preds: Dict[str, List[str]] = {l: [] for l in rpo}
    for u in rpo:
        for v in blocks[u].successors():
            preds[v].append(u)
    idom = dominators(rpo, preds)

    headers, fwd_in = set(), {l: 0 for l in rpo}
    for v in rpo:
        for u in preds[v]:
            if index[v] <= index[u]:
                if not _dominates(idom, v, u):
                    raise Irreducible(f"{fn.name}: loop at {v} has a second entry from {u}")
                headers.add(v)
            else:
                fwd_in[v] += 1
    merges = {l for l, n in fwd_in.items() if n >= 2}
    children: Dict[str, List[str]] = {l: [] for l in rpo}
    for l in rpo[1:]:
        children[idom[l]].append(l)

    def do_tree(x):
        ys = sorted((y for y in children[x] if y in merges), key=index.get, reverse=True)
        body = node_within(x, ys)
        return [Loop(x, body)] if x in headers else body

    def node_within(x, ys):
        if ys:
            return [Block(ys[0], node_within(x, ys[1:]))] + do_tree(ys[0])
        b = blocks[x]
        t = b.instrs[-1]
        out: list = [Code(b)]
        if t.op == "br":
            out += branch(x, t.labels[0])
        elif t.op == "br.cond":
            out.append(If(t.args[0], branch(x, t.labels[0]), branch(x, t.labels[1])))
        else:
            out.append(Return(t))
        return out

    def branch(src, dst):
        if index[dst] <= index[src]: return [Br(dst, "loop")]
        if dst in merges: return [Br(dst, "block")]
        return do_tree(dst)

    return do_tree(rpo[0])
