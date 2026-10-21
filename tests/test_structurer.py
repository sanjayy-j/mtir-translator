"""Tests for the CFG structurer (M6b).  Owner: Member 4."""
import pytest

from src.backend.wasm.structurer import (Block, Br, Code, If, Irreducible, Loop,
                                         Return, structure)
from src.cir.ir import BasicBlock, ConstInt, Function, Instr, Reg, Ty

from tests.programs import CORPUS


def walk(nodes, enclosing=()):
    """Yield (node, labels of enclosing blocks/loops) for every node."""
    for n in nodes:
        yield n, enclosing
        if isinstance(n, (Block, Loop)):
            yield from walk(n.body, enclosing + ((type(n).__name__, n.label),))
        elif isinstance(n, If):
            yield from walk(n.then, enclosing)
            yield from walk(n.else_, enclosing)


def reachable_labels(fn):
    seen, stack = set(), [fn.entry.label]
    while stack:
        l = stack.pop()
        if l not in seen:
            seen.add(l)
            stack += fn.block(l).successors()
    return seen


@pytest.mark.parametrize("name", CORPUS)
def test_every_reachable_block_is_emitted_exactly_once(corpus_module, name):
    for fn in corpus_module(name).functions:
        emitted = [n.block.label for n, _ in walk(structure(fn)) if isinstance(n, Code)]
        assert sorted(emitted) == sorted(reachable_labels(fn))


@pytest.mark.parametrize("name", CORPUS)
def test_every_branch_has_an_enclosing_target(corpus_module, name):
    for fn in corpus_module(name).functions:
        for n, enclosing in walk(structure(fn)):
            if isinstance(n, Br):
                want = ("Block" if n.kind == "block" else "Loop", n.target)
                assert want in enclosing


def _fn(edges):
    """A function whose blocks branch as `edges` says (label -> successors)."""
    c = Reg("c", Ty.I1)
    blocks = []
    for label, succ in edges.items():
        if len(succ) == 2:
            term = Instr("br.cond", Ty.I1, None, [c], labels=succ)
        elif succ:
            term = Instr("br", Ty.VOID, None, [], labels=succ)
        else:
            term = Instr("ret", Ty.I32, None, [ConstInt(0)])
        blocks.append(BasicBlock(label, [term]))
    from src.cir.ir import Param
    return Function("f", [Param("c", Ty.I1)], Ty.I32, blocks)


def test_loop_and_merge():
    fn = _fn({"entry": ["head"], "head": ["body", "end"], "body": ["head", "end"],
              "end": []})
    tree = structure(fn)
    loops = [n for n, _ in walk(tree) if isinstance(n, Loop)]
    blocks = [n for n, _ in walk(tree) if isinstance(n, Block)]
    assert [l.label for l in loops] == ["head"]
    assert [b.label for b in blocks] == ["end"]       # two forward in-edges


def test_irreducible_cfg_is_refused():
    # entry branches into the middle of the a <-> b cycle from both sides.
    fn = _fn({"entry": ["a", "b"], "a": ["b"], "b": ["a", "out"], "out": []})
    with pytest.raises(Irreducible):
        structure(fn)


def test_irreducible_cfg_falls_back_to_dispatch():
    from src.backend.wasm.emit_wat import emit
    from src.cir.ir import Module
    fn = _fn({"entry": ["a", "b"], "a": ["b"], "b": ["a", "out"], "out": []})
    assert "$dispatch" in emit(Module("m", [], [fn]))
