"""Tests for AST -> CIR lowering (M3a).  Owner: Member 3.

The abs() example is the primary end-to-end case: the module the builder
produces from docs/examples/abs.mini must be the same module that
docs/examples/abs.cir draws by hand, up to the choice of block labels.  The
hand-drawn file stays the golden file for the printer; this file pins that the
compiler actually agrees with the figure.
"""

import pytest

from src.cir.builder import BuildError, build
from src.cir.cfg import check_function, reachable, successors
from src.cir.ir import ConstInt, Ty
from src.cir.printer import print_module
from src.driver import _demo_cir_module
from src.frontend.parser import parse


def ops(fn):
    return [i.op for b in fn.blocks for i in b.instrs]


def shape(fn):
    """The CFG shape with labels replaced by their position, so two functions
    can be compared without depending on how blocks happen to be named."""
    index = {b.label: n for n, b in enumerate(fn.blocks)}
    return {index[b.label]: [index[s] for s in b.successors()] for b in fn.blocks}


# -- the worked example ----------------------------------------------------
def test_abs_lowers_to_the_figure_2_module(mini_module):
    """Same opcodes, same operands, same CFG as docs/examples/abs.cir."""
    built = mini_module("fn abs(x: int) -> int {\n"
                        "    if (x < 0) { return -x; }\n"
                        "    return x;\n}\n").function("abs")
    golden = _demo_cir_module().function("abs")

    assert ops(built) == ops(golden) == [
        "icmp.slt", "br.cond", "sub", "ret", "ret"]
    assert shape(built) == shape(golden)
    assert [p.ty for p in built.params] == [Ty.I32]
    assert built.ret_ty is Ty.I32


def test_abs_keeps_its_parameter_in_a_register(mini_module):
    """x is never assigned, so it needs no alloca -- the one departure from
    'every local lives in memory', and the reason the output matches Figure 2."""
    fn = mini_module("fn abs(x: int) -> int {\n"
                     "    if (x < 0) { return -x; }\n    return x;\n}\n").function("abs")
    assert "alloca" not in ops(fn)


def test_an_assigned_parameter_does_get_a_slot(mini_module):
    fn = mini_module("fn f(x: int) -> int { x = x + 1; return x; }").function("f")
    assert ops(fn)[:2] == ["alloca", "store"]


# -- structure -------------------------------------------------------------
@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_every_corpus_block_has_exactly_one_terminator(name, corpus_module):
    for fn in corpus_module(name).functions:
        for block in fn.blocks:
            assert block.terminator is not None, f"{fn.name}:{block.label}"
            assert sum(i.is_terminator() for i in block.instrs) == 1
        assert check_function(fn) == []


def test_every_branch_target_exists(corpus_module):
    for fn in corpus_module("valid/control_flow").functions:
        labels = {b.label for b in fn.blocks}
        for targets in successors(fn).values():
            assert set(targets) <= labels


# -- control-flow shapes from docs/cir-spec.md section 5 -------------------
def test_if_without_an_else_has_three_blocks(mini_module):
    fn = mini_module("fn f(c: bool) -> int {\n"
                     "  if (c) { return 1; }\n  return 0;\n}").function("f")
    assert [b.label for b in fn.blocks] == ["entry", "if.then.0", "if.end.0"]


def test_if_with_an_else_has_four_blocks(mini_module):
    fn = mini_module("fn f(c: bool) -> int {\n"
                     "  if (c) { return 1; } else { return 2; }\n  return 0;\n}"
                     ).function("f")
    assert [b.label for b in fn.blocks][:3] == ["entry", "if.then.0", "if.else.0"]


def test_a_join_block_is_not_created_when_both_arms_return(mini_module):
    """Both arms end the function, so nothing branches to the join."""
    fn = mini_module("fn f(c: bool) -> int {\n"
                     "  if (c) { return 1; } else { return 2; }\n}").function("f")
    assert [b.label for b in fn.blocks] == ["entry", "if.then.0", "if.else.0"]


def test_while_uses_the_head_body_end_shape(mini_module):
    fn = mini_module("fn f(n: int) -> void { while (n < 3) { } }").function("f")
    assert [b.label for b in fn.blocks] == [
        "entry", "while.head.0", "while.body.0", "while.end.0"]
    assert successors(fn)["while.head.0"] == ["while.body.0", "while.end.0"]
    assert successors(fn)["while.body.0"] == ["while.head.0"]


def test_for_uses_a_separate_step_block(mini_module):
    fn = mini_module("fn f() -> void { for (let i: int = 0; i < 3; i = i + 1) { } }"
                     ).function("f")
    assert [b.label for b in fn.blocks] == [
        "entry", "for.head.0", "for.body.0", "for.step.0", "for.end.0"]


def test_break_goes_to_the_exit_block_and_continue_to_the_step_block(mini_module):
    fn = mini_module(
        "fn f() -> void {\n"
        "  for (let i: int = 0; i < 9; i = i + 1) {\n"
        "    if (i > 3) { break; }\n"
        "    if (i > 1) { continue; }\n"
        "  }\n}").function("f")
    edges = successors(fn)
    assert "for.end.0" in edges["if.then.1"]     # break
    assert "for.step.0" in edges["if.then.2"]    # continue


def test_an_infinite_for_loop_has_an_unconditional_head(mini_module):
    fn = mini_module("fn f() -> void { for (;;) { } }").function("f")
    assert fn.block("for.head.0").terminator.op == "br"


# -- expressions -----------------------------------------------------------
def test_expressions_are_three_address(mini_module):
    fn = mini_module("fn f(a: int, b: int, c: int) -> int { return a + b * c; }"
                     ).function("f")
    for instr in (i for b in fn.blocks for i in b.instrs):
        assert len(instr.args) <= 2, instr


def test_short_circuit_and_only_evaluates_the_right_operand_when_needed(mini_module):
    fn = mini_module("fn f(a: bool, b: bool) -> bool { return a && b; }").function("f")
    assert [b.label for b in fn.blocks] == ["entry", "and.rhs.0", "and.end.0"]
    assert successors(fn)["entry"] == ["and.rhs.0", "and.end.0"]


def test_short_circuit_or_inverts_the_branch(mini_module):
    fn = mini_module("fn f(a: bool, b: bool) -> bool { return a || b; }").function("f")
    assert successors(fn)["entry"] == ["or.end.0", "or.rhs.0"]


def test_negating_a_literal_folds_rather_than_emitting_a_subtraction(mini_module):
    """-2147483648 must be one i32 constant, not 0 - 2147483648, which does
    not fit in i32 at all."""
    fn = mini_module("fn f() -> int { return -2147483648; }").function("f")
    assert ops(fn) == ["ret"]
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(-2147483648, Ty.I32)


def test_integer_negation_is_a_subtraction_from_zero(mini_module):
    fn = mini_module("fn f(x: int) -> int { return -x; }").function("f")
    assert ops(fn) == ["sub", "ret"]


def test_float_negation_uses_neg(mini_module):
    fn = mini_module("fn f(x: float) -> float { return -x; }").function("f")
    assert ops(fn) == ["neg", "ret"]


def test_comparisons_pick_signed_or_ordered_predicates(mini_module):
    ints = mini_module("fn f(a: int, b: int) -> bool { return a < b; }").function("f")
    floats = mini_module("fn f(a: float, b: float) -> bool { return a < b; }"
                         ).function("f")
    assert ops(ints)[0] == "icmp.slt"
    assert ops(floats)[0] == "fcmp.olt"


def test_a_comparison_result_is_typed_i1(mini_module):
    fn = mini_module("fn f(a: int) -> bool { return a < 1; }").function("f")
    assert fn.blocks[0].instrs[0].dest.ty is Ty.I1


def test_an_int_argument_widens_to_a_float_parameter(mini_module):
    fn = mini_module("fn g(x: float) -> void { }\n"
                     "fn f(n: int) -> void { g(n); }").function("f")
    assert ops(fn)[:2] == ["sitofp", "call"]


def test_builtins_lower_to_intrinsics(mini_module):
    fn = mini_module("fn f() -> void { print_int(1); print_float(2.5); }"
                     ).function("f")
    assert ops(fn)[:2] == ["print.i32", "print.f64"]


def test_arrays_lower_to_alloca_gep_and_load_store(mini_module):
    fn = mini_module("fn f() -> int {\n"
                     "  let xs: int[4];\n"
                     "  xs[0] = 7;\n"
                     "  return xs[0];\n}").function("f")
    assert ops(fn) == ["alloca", "gep", "store", "gep", "load", "ret"]
    assert fn.blocks[0].instrs[0].args == [ConstInt(4, Ty.I32)]


def test_scalar_globals_lower_to_a_module_global(mini_module):
    mod = mini_module("global counter: int = 3;\n"
                      "fn f() -> int { counter = counter + 1; return counter; }")
    assert [(g.name, g.ty, g.init) for g in mod.globals] == [
        ("counter", Ty.I32, ConstInt(3, Ty.I32))]
    assert ops(mod.function("f")) == ["load", "add", "store", "load", "ret"]


def test_code_after_a_return_lands_in_an_unreachable_block(mini_module):
    fn = mini_module("fn f() -> int { return 1; print_int(2); }").function("f")
    assert "dead.0" in [b.label for b in fn.blocks]
    assert "dead.0" not in reachable(fn)


def test_recursion_needs_no_forward_declaration(mini_module):
    mod = mini_module("fn a(n: int) -> int { return b(n); }\n"
                      "fn b(n: int) -> int { return n; }")
    call = [i for i in mod.function("a").blocks[0].instrs if i.op == "call"][0]
    assert call.callee == "b" and call.ty is Ty.I32


# -- refusals, each naming the diagnostic M2 will own ----------------------
@pytest.mark.parametrize("source,code", [
    ("fn f() -> int { return nope; }", "E001"),
    ("fn f() -> int { return g(); }", "E006"),
    ("fn g(a: int) -> int { return a; }\nfn f() -> int { return g(1, 2); }", "E007"),
    ("fn f() -> int { }", "E009"),
    ("fn f() -> void { return 1; }", "E010"),
    ("fn f() -> void { break; }", "E011"),
    ("fn f() -> void { continue; }", "E011"),
    ("fn f(a: int) -> int { return a[0]; }", "E012"),
    ("fn f(a: bool, b: float) -> void { print_float(a + b); }", "E003"),
    ("fn f(a: int, b: float) -> void { print_int(a + b); }", "E005"),
    ("fn f(n: int) -> void { if (n) { } }", "E003"),
    ("fn f() -> void { let xs: int[4] = 0; }", "E004"),
    ("global t: int[4];\nfn f() -> void { }", "E003"),
])
def test_untypeable_input_is_refused_with_the_right_code(source, code):
    with pytest.raises(BuildError) as excinfo:
        build(parse(source, "<test>"))
    assert excinfo.value.code == code


def test_the_builder_output_is_deterministic(corpus_module):
    assert print_module(corpus_module("valid/control_flow")) == \
        print_module(corpus_module("valid/control_flow"))
