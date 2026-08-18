"""Tests for register-to-stack lowering (M6a, Member 4).

The point of these tests is not that the pass produces some particular
listing, but that the two properties we rely on actually hold:

  1. the naive schema is a faithful, operand-order-preserving translation, and
  2. the peephole rule only ever removes a set/get pair that nothing else
     could have observed.
"""

import pytest

from src.backend.wasm.reg2stack import (StackOp, block_live_out,
                                        block_use_counts, count_function,
                                        local_table, lower_block,
                                        lower_block_naive, lower_function,
                                        opcode_for, peephole)
from src.cir.ir import BasicBlock, ConstInt, Function, Instr, Param, Reg, Ty
from src.driver import _demo_cir_module


@pytest.fixture
def abs_fn():
    return _demo_cir_module().function("abs")


# --------------------------------------------------------------------------
# Opcode mapping
# --------------------------------------------------------------------------
def test_signed_compare_maps_to_signed_stack_opcode():
    instr = Instr("icmp.slt", Ty.I32, Reg("t", Ty.I1), [])
    assert str(opcode_for(instr)) == "i32.lt_s"


def test_unsigned_compare_maps_to_unsigned_stack_opcode():
    instr = Instr("icmp.ult", Ty.I32, Reg("t", Ty.I1), [])
    assert str(opcode_for(instr)) == "i32.lt_u"


def test_float_ops_take_the_f64_prefix():
    assert str(opcode_for(Instr("fadd", Ty.F64, Reg("t", Ty.F64), []))) == "f64.add"


def test_i1_is_represented_as_i32_on_the_stack():
    # WebAssembly has no i1; the divergence table requires 0/1 in an i32.
    assert str(opcode_for(Instr("add", Ty.I1, Reg("t", Ty.I1), []))) == "i32.add"


def test_signed_division_maps_to_div_s():
    assert str(opcode_for(Instr("sdiv", Ty.I32, Reg("t", Ty.I32), []))) == "i32.div_s"


def test_ret_maps_to_return():
    assert str(opcode_for(Instr("ret", Ty.I32, None, []))) == "return"


def test_trap_maps_to_unreachable():
    assert str(opcode_for(Instr("trap", Ty.VOID, None, []))) == "unreachable"


# --------------------------------------------------------------------------
# Naive schema
# --------------------------------------------------------------------------
def test_operands_are_pushed_left_to_right(abs_fn):
    # CIR says `sub i32 0, %x`, so the constant must be pushed before %x.
    # Getting this backwards silently computes x - 0 instead of 0 - x.
    seq = lower_block_naive(abs_fn.block("then"), local_table(abs_fn))
    assert [str(op) for op in seq[:3]] == ["i32.const 0", "local.get $x", "i32.sub"]


def test_every_defining_instruction_parks_its_result(abs_fn):
    local_of = local_table(abs_fn)
    block = abs_fn.block("entry")
    seq = lower_block_naive(block, local_of)
    defines = sum(1 for i in block.instrs if i.dest is not None)
    assert sum(1 for op in seq if op.op == "local.set") == defines


def test_naive_count_for_abs(abs_fn):
    naive, _ = count_function(abs_fn)
    assert naive == 14


# --------------------------------------------------------------------------
# Peephole rule
# --------------------------------------------------------------------------
def test_peephole_removes_a_single_use_set_get_pair():
    seq = [StackOp("i32.const", 1), StackOp("local.set", "$t"),
           StackOp("local.get", "$t"), StackOp("return")]
    out = peephole(seq, {"$t": 1}, set())
    assert [str(op) for op in out] == ["i32.const 1", "return"]


def test_peephole_keeps_the_pair_when_the_local_is_read_twice():
    seq = [StackOp("local.set", "$t"), StackOp("local.get", "$t")]
    assert peephole(seq, {"$t": 2}, set()) == seq


def test_peephole_keeps_the_pair_when_the_local_is_live_out():
    seq = [StackOp("local.set", "$t"), StackOp("local.get", "$t")]
    assert peephole(seq, {"$t": 1}, {"$t"}) == seq


def test_peephole_does_not_pair_different_locals():
    seq = [StackOp("local.set", "$a"), StackOp("local.get", "$b")]
    assert peephole(seq, {"$a": 1, "$b": 1}, set()) == seq


def test_peephole_does_not_touch_non_adjacent_pairs():
    seq = [StackOp("local.set", "$t"), StackOp("i32.const", 0),
           StackOp("local.get", "$t")]
    assert peephole(seq, {"$t": 1}, set()) == seq


def test_peephole_never_lengthens_a_sequence(abs_fn):
    naive, tuned = count_function(abs_fn)
    assert tuned <= naive


def test_peepholed_count_for_abs(abs_fn):
    # The measured figure quoted in the Review 1 report, section 11.5.
    naive, tuned = count_function(abs_fn)
    assert (naive, tuned) == (14, 10)


# --------------------------------------------------------------------------
# Liveness approximation
# --------------------------------------------------------------------------
def test_live_out_includes_a_register_used_by_another_block(abs_fn):
    local_of = local_table(abs_fn)
    entry = abs_fn.block("entry")
    # %x is read in `then` and `exit`, so it must not be peepholed away here.
    assert "$x" in block_live_out(abs_fn, entry, local_of)


def test_live_out_excludes_a_block_local_temporary(abs_fn):
    local_of = local_table(abs_fn)
    entry = abs_fn.block("entry")
    assert "$t0" not in block_live_out(abs_fn, entry, local_of)


def test_use_counts_are_per_block(abs_fn):
    local_of = local_table(abs_fn)
    counts = block_use_counts(abs_fn.block("entry"), local_of)
    assert counts["$t0"] == 1 and counts["$x"] == 1


# --------------------------------------------------------------------------
# Whole-function lowering
# --------------------------------------------------------------------------
def test_lower_function_covers_every_block(abs_fn):
    assert set(lower_function(abs_fn)) == {"entry", "then", "exit"}


def test_local_table_covers_params_and_definitions(abs_fn):
    assert local_table(abs_fn) == {"x": "$x", "t0": "$t0", "t1": "$t1"}


def test_straight_line_block_needs_no_locals_after_peephole():
    # (2 + 3) * 4 with each temporary used once: everything stays on the stack.
    a, b = Reg("a", Ty.I32), Reg("b", Ty.I32)
    block = BasicBlock("entry", [
        Instr("add", Ty.I32, a, [ConstInt(2), ConstInt(3)]),
        Instr("mul", Ty.I32, b, [a, ConstInt(4)]),
        Instr("ret", Ty.I32, None, [b]),
    ])
    fn = Function("k", [], Ty.I32, [block])
    out = lower_function(fn)["entry"]
    assert not [op for op in out if op.op in ("local.set", "local.get")]
    assert [str(op) for op in out] == [
        "i32.const 2", "i32.const 3", "i32.add",
        "i32.const 4", "i32.mul", "return",
    ]
