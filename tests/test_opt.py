"""Tests for the CIR optimisation passes (M4).  Owner: Member 3.

Each pass is tested for what it does *and* for what it must refuse to do.  The
refusals matter more: an optimisation that deletes a trap, or that adopts
Python's arithmetic where CIR has defined its own, would silently break the
cross-target agreement the whole project is measured on.
"""

import pytest

from src.cir.cfg import check_function, reachable
from src.cir.ir import ConstFloat, ConstInt, Instr, Reg, Ty, wrap_int
from src.cir.parser import parse_cir
from src.cir.printer import print_module
from src.opt import count_instructions, run
from src.opt import constfold, copyprop, dce


def module_of(body: str, header="func @f(i32 %x, i32 %y) -> i32 {"):
    return parse_cir(f"{body}\n" if body.startswith("global")
                     else f"{header}\nentry:\n{body}\n}}\n")


def fn_of(body: str, header="func @f(i32 %x, i32 %y) -> i32 {"):
    return module_of(body, header).functions[0]


def ops(fn):
    return [i.op for b in fn.blocks for i in b.instrs]


# ==========================================================================
# wrap_int -- the shared definition of what i32 arithmetic means
# ==========================================================================
@pytest.mark.parametrize("value,ty,expected", [
    (0, Ty.I32, 0),
    (2147483647, Ty.I32, 2147483647),
    (2147483648, Ty.I32, -2147483648),
    (-2147483649, Ty.I32, 2147483647),
    (1 << 40, Ty.I32, 0),
    (1 << 64, Ty.I64, 0),
    (1, Ty.I1, -1),
])
def test_wrap_int_is_twos_complement(value, ty, expected):
    assert wrap_int(value, ty) == expected


# ==========================================================================
# Constant folding
# ==========================================================================
def test_arithmetic_folds():
    fn = fn_of("  %a = mul i32 3, 4\n  %b = add i32 2, %a\n  ret i32 %b")
    constfold.run_function(fn)
    assert ops(fn) == ["ret"]
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(14, Ty.I32)


def test_overflow_wraps_rather_than_growing():
    """Row 4: CIR defines signed overflow as wraparound, and Python's integers
    are unbounded, so the folder must wrap explicitly."""
    fn = fn_of("  %a = add i32 2147483647, 1\n  ret i32 %a")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-2147483648, Ty.I32)


def test_shift_counts_fold_modulo_the_width():
    """Row 3: 1 << 33 is 1 << 1, not 0 and not a huge number."""
    fn = fn_of("  %a = shl i32 1, 33\n  ret i32 %a")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(2, Ty.I32)


def test_lshr_folds_as_unsigned_and_ashr_as_signed():
    fn = fn_of("  %a = lshr i32 -1, 28\n  %b = ashr i32 -1, 28\n"
               "  %c = add i32 %a, %b\n  ret i32 %c")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(15 + -1, Ty.I32)


def test_comparisons_fold_to_i1():
    fn = fn_of("  %a = icmp.slt i32 1, 2\n  %b = zext i32 %a\n  ret i32 %b")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(1, Ty.I32)


def test_unsigned_comparison_folds_on_the_unsigned_reading():
    fn = fn_of("  %a = icmp.ult i32 -1, 1\n  %b = zext i32 %a\n  ret i32 %b")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(0, Ty.I32)


def test_safe_division_folds():
    fn = fn_of("  %a = sdiv i32 -7, 2\n  ret i32 %a")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-3, Ty.I32)  # toward zero


def test_safe_remainder_folds_with_the_sign_of_the_dividend():
    fn = fn_of("  %a = srem i32 -7, 2\n  ret i32 %a")
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-1, Ty.I32)


@pytest.mark.parametrize("body", [
    "  %a = sdiv i32 1, 0\n  ret i32 %a",
    "  %a = srem i32 1, 0\n  ret i32 %a",
    "  %a = udiv i32 1, 0\n  ret i32 %a",
    "  %a = sdiv i32 -2147483648, -1\n  ret i32 %a",
    "  %a = srem i32 -2147483648, -1\n  ret i32 %a",
])
def test_a_trapping_division_is_never_folded(body):
    """Rows 1 and 2: these trap, so folding them would delete the trap."""
    fn = fn_of(body)
    constfold.run_function(fn)
    assert ops(fn)[0] in ("sdiv", "srem", "udiv")


def test_float_division_is_not_folded():
    fn = parse_cir("func @f() -> f64 {\nentry:\n"
                   "  %a = fdiv f64 1.0, 0.0\n  ret f64 %a\n}\n").functions[0]
    constfold.run_function(fn)
    assert "fdiv" in ops(fn)


def test_float_addition_folds():
    fn = parse_cir("func @f() -> f64 {\nentry:\n"
                   "  %a = fadd f64 1.5, 2.0\n  ret f64 %a\n}\n").functions[0]
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstFloat(3.5)


def test_widening_conversions_fold():
    fn = parse_cir("func @f() -> i64 {\nentry:\n"
                   "  %a = sext i64 -1\n  ret i64 %a\n}\n").functions[0]
    constfold.run_function(fn)
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-1, Ty.I64)


def test_a_register_assigned_twice_is_not_propagated():
    """CIR is not in SSA form; only a single definition may be substituted."""
    fn = fn_of("  %a = add i32 1, 2\n  %a = add i32 3, 4\n  ret i32 %a")
    constfold.run_function(fn)
    assert ops(fn) == ["add", "add", "ret"]


def test_a_constant_branch_becomes_unconditional():
    fn = fn_of("  %c = icmp.slt i32 1, 2\n  br %c ? a : b\n"
               "a:\n  ret i32 1\n"
               "b:\n  ret i32 2", header="func @f() -> i32 {")
    constfold.run_function(fn)
    assert fn.block("entry").terminator.op == "br"
    assert fn.block("entry").terminator.labels == ["a"]


def test_folding_does_not_touch_a_call_result():
    fn = fn_of("  %a = call i32 @g(1, 2)\n  ret i32 %a",
               header="func @f() -> i32 {")
    constfold.run_function(fn)
    assert ops(fn) == ["call", "ret"]


def test_nan_operands_are_left_alone():
    ins = Instr("fadd", Ty.F64, Reg("t", Ty.F64),
                [ConstFloat(float("nan")), ConstFloat(1.0)])
    assert constfold.fold(ins) is None


# ==========================================================================
# Copy propagation
# ==========================================================================
@pytest.mark.parametrize("body,expected", [
    ("  %a = add i32 %x, 0\n  ret i32 %a", "x"),
    ("  %a = add i32 0, %x\n  ret i32 %a", "x"),
    ("  %a = sub i32 %x, 0\n  ret i32 %a", "x"),
    ("  %a = mul i32 %x, 1\n  ret i32 %a", "x"),
    ("  %a = mul i32 1, %x\n  ret i32 %a", "x"),
    ("  %a = sdiv i32 %x, 1\n  ret i32 %a", "x"),
    ("  %a = or i32 %x, 0\n  ret i32 %a", "x"),
    ("  %a = xor i32 %x, 0\n  ret i32 %a", "x"),
    ("  %a = and i32 %x, -1\n  ret i32 %a", "x"),
    ("  %a = shl i32 %x, 0\n  ret i32 %a", "x"),
    ("  %a = ashr i32 %x, 0\n  ret i32 %a", "x"),
])
def test_identity_operations_are_copies(body, expected):
    fn = fn_of(body)
    copyprop.run_function(fn)
    assert ops(fn) == ["ret"]
    assert fn.blocks[0].instrs[0].args[0] == Reg(expected, Ty.I32)


@pytest.mark.parametrize("body", [
    "  %a = sub i32 0, %x\n  ret i32 %a",     # 0 - x is a negation, not a copy
    "  %a = sdiv i32 1, %x\n  ret i32 %a",    # 1 / x is not a copy either
    "  %a = shl i32 0, %x\n  ret i32 %a",
    "  %a = and i32 %x, 1\n  ret i32 %a",
    "  %a = mul i32 %x, 2\n  ret i32 %a",
])
def test_non_identities_are_left_alone(body):
    fn = fn_of(body)
    copyprop.run_function(fn)
    assert len(ops(fn)) == 2


def test_a_chain_of_copies_collapses():
    fn = fn_of("  %a = add i32 %x, 0\n  %b = mul i32 %a, 1\n"
               "  %c = or i32 %b, 0\n  ret i32 %c")
    copyprop.run_function(fn)
    assert ops(fn) == ["ret"]
    assert fn.blocks[0].instrs[0].args[0] == Reg("x", Ty.I32)


def test_copies_propagate_across_blocks():
    fn = fn_of("  %a = add i32 %x, 0\n  br next\n"
               "next:\n  ret i32 %a")
    copyprop.run_function(fn)
    assert fn.block("next").instrs[0].args[0] == Reg("x", Ty.I32)


def test_float_identities_are_not_treated_as_copies():
    """fadd x, 0.0 is not x: it turns -0.0 into +0.0."""
    fn = parse_cir("func @f(f64 %x) -> f64 {\nentry:\n"
                   "  %a = fadd f64 %x, 0.0\n  ret f64 %a\n}\n").functions[0]
    copyprop.run_function(fn)
    assert "fadd" in ops(fn)


# ==========================================================================
# Dead-code elimination
# ==========================================================================
def test_an_unread_definition_is_removed():
    fn = fn_of("  %a = add i32 %x, %y\n  ret i32 %x")
    dce.run_function(fn)
    assert ops(fn) == ["ret"]


def test_a_chain_of_dead_definitions_is_removed():
    fn = fn_of("  %a = add i32 %x, %y\n  %b = mul i32 %a, %a\n  ret i32 %x")
    dce.run_function(fn)
    assert ops(fn) == ["ret"]


@pytest.mark.parametrize("body", [
    "  store i32 1, %x\n  ret i32 0",
    "  %a = call i32 @g(1)\n  ret i32 0",
    "  print.i32 1\n  ret i32 0",
    "  trap\n  ret i32 0",
])
def test_observable_instructions_are_kept(body):
    """A call is kept even with an unused result: CIR has no purity
    annotation, so assuming a call is pure would be unsound."""
    fn = fn_of(body)
    dce.run_function(fn)
    assert len(ops(fn)) == 2


def test_an_unreachable_block_is_removed():
    fn = fn_of("  ret i32 0\n"
               "orphan:\n  ret i32 1", header="func @f() -> i32 {")
    dce.run_function(fn)
    assert [b.label for b in fn.blocks] == ["entry"]


def test_a_loop_body_is_not_mistaken_for_dead_code():
    fn = parse_cir(
        "func @f(i32 %n) -> i32 {\n"
        "entry:\n  br head\n"
        "head:\n  %c = icmp.slt i32 %n, 3\n  br %c ? body : end\n"
        "body:\n  print.i32 %n\n  br head\n"
        "end:\n  ret i32 0\n}\n").functions[0]
    dce.run_function(fn)
    assert len(fn.blocks) == 4


# ==========================================================================
# The pipeline
# ==========================================================================
def test_level_0_changes_nothing(corpus_module):
    before = print_module(corpus_module("valid/arith"))
    after = print_module(run(corpus_module("valid/arith"), 0))
    assert before == after


def test_the_pipeline_folds_the_arithmetic_corpus(corpus_module):
    module = corpus_module("valid/arith")
    before = count_instructions(module)
    run(module, 1)
    after = count_instructions(module)
    assert after < before
    assert "mul" not in [i.op for f in module.functions
                         for b in f.blocks for i in b.instrs]


def test_a_constant_false_loop_disappears(mini_module):
    """Folding turns the head's br.cond into a br, and DCE then drops the body
    -- neither pass could do it alone."""
    module = mini_module("fn f() -> void { while (false) { print_int(1); } }")
    run(module, 1)
    labels = [b.label for b in module.function("f").blocks]
    assert "while.body.0" not in labels
    assert "print.i32" not in [i.op for b in module.function("f").blocks
                               for i in b.instrs]


@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_optimisation_preserves_well_formedness(name, corpus_module):
    module = run(corpus_module(name), 1)
    for fn in module.functions:
        assert check_function(fn) == []
        assert reachable(fn) == {b.label for b in fn.blocks}


@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "boundary/div_edge"])
def test_optimised_output_still_round_trips(name, corpus_module):
    text = print_module(run(corpus_module(name), 1))
    assert print_module(parse_cir(text)) == text


def test_optimisation_reaches_a_fixed_point(corpus_module):
    once = print_module(run(corpus_module("valid/arith"), 1))
    twice = print_module(run(parse_cir(once), 1))
    assert once == twice


def test_a_trap_survives_optimisation(corpus_module):
    """The INT_MIN / -1 call in div_edge must still reach a guarded sdiv."""
    module = run(corpus_module("boundary/div_edge"), 1)
    assert "sdiv" in [i.op for b in module.function("div").blocks
                      for i in b.instrs]
