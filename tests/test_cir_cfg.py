"""Tests for CIR control-flow graph construction and structural checks (M3a).

Owner: Member 3.

Each malformed-IR test names the rule of docs/cir-spec.md section 6 that it
covers, so the eight well-formedness classes required by objective O1 can be
traced from the spec to a test.
"""

import pytest

from src.cir.cfg import (check_cfg, check_defs, check_function, predecessors,
                         reachable, successors)
from src.cir.ir import BasicBlock, ConstInt, Function, Instr, Param, Reg, Ty
from src.driver import _demo_cir_module


def fn_of(*blocks, params=(), ret=Ty.I32, name="f"):
    return Function(name, [Param(n, t) for n, t in params], ret, list(blocks))


def ret(value=0, ty=Ty.I32):
    return Instr("ret", ty, None, [ConstInt(value, ty)])


# -- queries ---------------------------------------------------------------
def test_successors_follow_terminator_order():
    fn = _demo_cir_module().function("abs")
    assert successors(fn) == {"entry": ["then", "exit"], "then": [], "exit": []}


def test_predecessors_invert_the_successor_map():
    fn = _demo_cir_module().function("abs")
    assert predecessors(fn) == {"entry": [], "then": ["entry"], "exit": ["entry"]}


def test_predecessors_do_not_duplicate_a_doubled_edge():
    """br.cond with the same target twice is one predecessor, not two."""
    fn = fn_of(
        BasicBlock("entry", [Instr("br.cond", Ty.I1, None, [ConstInt(1, Ty.I1)],
                                   labels=["join", "join"])]),
        BasicBlock("join", [ret()]),
    )
    assert predecessors(fn)["join"] == ["entry"]


def test_reachable_excludes_an_orphan_block():
    fn = fn_of(
        BasicBlock("entry", [ret()]),
        BasicBlock("orphan", [ret(1)]),
    )
    assert reachable(fn) == {"entry"}


def test_a_loop_back_edge_is_a_predecessor():
    fn = fn_of(
        BasicBlock("entry", [Instr("br", Ty.VOID, None, [], labels=["head"])]),
        BasicBlock("head", [Instr("br.cond", Ty.I1, None, [ConstInt(1, Ty.I1)],
                                  labels=["head", "end"])]),
        BasicBlock("end", [ret()]),
    )
    assert set(predecessors(fn)["head"]) == {"entry", "head"}
    assert reachable(fn) == {"entry", "head", "end"}


# -- structural checks -----------------------------------------------------
def test_a_well_formed_function_has_no_diagnostics():
    for fn in _demo_cir_module().functions:
        assert check_function(fn) == []


def test_rule_1_block_without_a_terminator():
    fn = fn_of(BasicBlock("entry", [Instr("add", Ty.I32, Reg("t", Ty.I32),
                                          [ConstInt(1), ConstInt(2)])]))
    assert any("does not end in a terminator" in e for e in check_cfg(fn))


def test_rule_1_empty_block_is_also_unterminated():
    assert any("terminator" in e for e in check_cfg(fn_of(BasicBlock("entry"))))


def test_rule_2_instruction_after_a_terminator():
    fn = fn_of(BasicBlock("entry", [
        ret(),
        Instr("add", Ty.I32, Reg("t", Ty.I32), [ConstInt(1), ConstInt(2)]),
        ret(),
    ]))
    assert any("after a terminator" in e for e in check_cfg(fn))


def test_rule_3_branch_to_a_block_that_does_not_exist():
    fn = fn_of(BasicBlock("entry", [Instr("br", Ty.VOID, None, [],
                                          labels=["nowhere"])]))
    assert any("undefined block 'nowhere'" in e for e in check_cfg(fn))


def test_rule_5_entry_block_with_a_predecessor():
    fn = fn_of(
        BasicBlock("entry", [Instr("br", Ty.VOID, None, [], labels=["back"])]),
        BasicBlock("back", [Instr("br", Ty.VOID, None, [], labels=["entry"])]),
    )
    assert any("entry block" in e and "predecessor" in e for e in check_cfg(fn))


def test_duplicate_block_labels_are_rejected():
    fn = fn_of(BasicBlock("entry", [ret()]), BasicBlock("entry", [ret(1)]))
    assert any("duplicate block label" in e for e in check_cfg(fn))


def test_a_function_with_no_blocks_is_rejected():
    assert check_cfg(fn_of()) == ["@f: function has no blocks"]


# -- rule 4: definition before use ----------------------------------------
def test_rule_4_use_of_an_undefined_register():
    fn = fn_of(BasicBlock("entry", [
        Instr("add", Ty.I32, Reg("t", Ty.I32), [Reg("ghost", Ty.I32), ConstInt(1)]),
        ret(),
    ]))
    assert any("%ghost" in e for e in check_defs(fn))


def test_rule_4_accepts_a_parameter():
    fn = fn_of(BasicBlock("entry", [
        Instr("add", Ty.I32, Reg("t", Ty.I32), [Reg("x", Ty.I32), ConstInt(1)]),
        Instr("ret", Ty.I32, None, [Reg("t", Ty.I32)]),
    ]), params=[("x", Ty.I32)])
    assert check_defs(fn) == []


def test_rule_4_rejects_a_register_defined_on_only_one_path():
    """%only is defined in one arm of a branch and used after the join."""
    fn = fn_of(
        BasicBlock("entry", [Instr("br.cond", Ty.I1, None, [ConstInt(1, Ty.I1)],
                                   labels=["a", "join"])]),
        BasicBlock("a", [
            Instr("add", Ty.I32, Reg("only", Ty.I32), [ConstInt(1), ConstInt(2)]),
            Instr("br", Ty.VOID, None, [], labels=["join"]),
        ]),
        BasicBlock("join", [Instr("ret", Ty.I32, None, [Reg("only", Ty.I32)])]),
    )
    assert any("%only" in e for e in check_defs(fn))


def test_rule_4_accepts_a_register_defined_on_every_path():
    fn = fn_of(
        BasicBlock("entry", [Instr("br.cond", Ty.I1, None, [ConstInt(1, Ty.I1)],
                                   labels=["a", "b"])]),
        BasicBlock("a", [
            Instr("add", Ty.I32, Reg("v", Ty.I32), [ConstInt(1), ConstInt(2)]),
            Instr("br", Ty.VOID, None, [], labels=["join"]),
        ]),
        BasicBlock("b", [
            Instr("add", Ty.I32, Reg("v", Ty.I32), [ConstInt(3), ConstInt(4)]),
            Instr("br", Ty.VOID, None, [], labels=["join"]),
        ]),
        BasicBlock("join", [Instr("ret", Ty.I32, None, [Reg("v", Ty.I32)])]),
    )
    assert check_defs(fn) == []


def test_rule_4_ignores_unreachable_blocks():
    """An orphan block has no incoming path, so definedness is vacuous there."""
    fn = fn_of(
        BasicBlock("entry", [ret()]),
        BasicBlock("orphan", [Instr("ret", Ty.I32, None, [Reg("g", Ty.I32)])]),
    )
    assert check_defs(fn) == []


def test_rule_4_terminates_on_a_loop():
    """The dataflow is iterative; a back edge must not make it diverge."""
    fn = fn_of(
        BasicBlock("entry", [
            Instr("add", Ty.I32, Reg("i", Ty.I32), [ConstInt(0), ConstInt(0)]),
            Instr("br", Ty.VOID, None, [], labels=["head"]),
        ]),
        BasicBlock("head", [
            Instr("add", Ty.I32, Reg("n", Ty.I32), [Reg("i", Ty.I32), ConstInt(1)]),
            Instr("br.cond", Ty.I1, None, [ConstInt(1, Ty.I1)],
                  labels=["head", "end"]),
        ]),
        BasicBlock("end", [Instr("ret", Ty.I32, None, [Reg("n", Ty.I32)])]),
    )
    assert check_defs(fn) == []


def test_check_function_stops_after_a_broken_graph():
    """A dangling branch target makes the dataflow meaningless, so it is the
    only thing reported."""
    fn = fn_of(BasicBlock("entry", [
        Instr("add", Ty.I32, Reg("t", Ty.I32), [Reg("ghost", Ty.I32), ConstInt(1)]),
        Instr("br", Ty.VOID, None, [], labels=["nowhere"]),
    ]))
    errors = check_function(fn)
    assert len(errors) == 1 and "nowhere" in errors[0]


@pytest.mark.parametrize("path", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_every_built_corpus_function_is_structurally_sound(path, corpus_module):
    for fn in corpus_module(path).functions:
        assert check_function(fn) == []
