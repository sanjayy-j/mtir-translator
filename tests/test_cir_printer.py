"""Tests for the CIR data structures, printer (M3a/M3c) and the reg-to-stack
peephole rule (M6a).

Owners: Member 3 (CIR), Member 4 (lowering).
"""

from pathlib import Path

from src.backend.wasm.reg2stack import StackOp, peephole
from src.cir.ir import BasicBlock, ConstInt, Function, Instr, Module, Param, Reg, Ty
from src.cir.printer import fmt_instr, print_module
from src.driver import _demo_cir_module

GOLDEN = Path(__file__).parents[1] / "docs" / "examples" / "abs.cir"


# -- printer ---------------------------------------------------------------
def test_abs_module_matches_the_golden_file():
    """The printed CIR must be byte-identical to Figure 2 of the Review 1
    report, which is checked in as docs/examples/abs.cir."""
    assert print_module(_demo_cir_module()) == GOLDEN.read_text()


def test_printing_is_deterministic():
    a = print_module(_demo_cir_module())
    b = print_module(_demo_cir_module())
    assert a == b


def test_instruction_formats():
    x = Reg("x", Ty.I32)
    t = Reg("t0", Ty.I32)
    assert fmt_instr(Instr("add", Ty.I32, t, [x, ConstInt(1)])) == "%t0 = add i32 %x, 1"
    assert fmt_instr(Instr("br", labels=["exit"])) == "br exit"
    assert fmt_instr(Instr("ret", Ty.I32, None, [x])) == "ret i32 %x"
    assert fmt_instr(Instr("ret")) == "ret void"
    assert fmt_instr(Instr("trap")) == "trap"


def test_call_with_and_without_a_destination():
    t = Reg("t0", Ty.I32)
    assert fmt_instr(Instr("call", Ty.I32, t, [ConstInt(3)], callee="f")) == \
        "%t0 = call i32 @f(3)"
    assert fmt_instr(Instr("call", Ty.VOID, None, [], callee="g")) == \
        "call void @g()"


# -- CFG -------------------------------------------------------------------
def test_cfg_successors():
    mod = _demo_cir_module()
    fn = mod.function("abs")
    assert fn.cfg() == {"entry": ["then", "exit"], "then": [], "exit": []}


def test_every_block_ends_in_a_terminator():
    # The property the verifier (M3b) will enforce for every module.
    for fn in _demo_cir_module().functions:
        for blk in fn.blocks:
            assert blk.terminator is not None, f"{blk.label} has no terminator"


def test_entry_block_is_first():
    fn = _demo_cir_module().function("abs")
    assert fn.entry.label == "entry"


# -- reg-to-stack peephole -------------------------------------------------
def test_peephole_removes_a_redundant_set_get_pair():
    seq = [
        StackOp("local.get", "x"),
        StackOp("i32.const", 0),
        StackOp("i32.lt_s"),
        StackOp("local.set", "t0"),
        StackOp("local.get", "t0"),
        StackOp("if"),
    ]
    out = peephole(seq, use_count={"t0": 1}, live_out=set())
    assert [str(o) for o in out] == [
        "local.get x", "i32.const 0", "i32.lt_s", "if",
    ]


def test_peephole_keeps_the_pair_when_the_local_is_used_twice():
    seq = [StackOp("local.set", "t0"), StackOp("local.get", "t0")]
    assert peephole(seq, use_count={"t0": 2}, live_out=set()) == seq


def test_peephole_keeps_the_pair_when_the_local_is_live_out():
    seq = [StackOp("local.set", "t0"), StackOp("local.get", "t0")]
    assert peephole(seq, use_count={"t0": 1}, live_out={"t0"}) == seq


def test_peephole_does_not_merge_across_different_locals():
    seq = [StackOp("local.set", "t0"), StackOp("local.get", "t1")]
    assert peephole(seq, use_count={"t0": 1, "t1": 1}, live_out=set()) == seq
