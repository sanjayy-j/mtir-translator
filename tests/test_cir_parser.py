"""Tests for the CIR textual parser (M3c).  Owner: Member 3.

The property under test is the one docs/cir-spec.md section 4 states:

    print_module(parse_cir(t)) == t

It is what makes .cir a real interchange format rather than a debug dump, and
therefore what lets each back end be developed against a checked-in file.
"""

from pathlib import Path

import pytest

from src.cir.ir import ConstFloat, ConstInt, Reg, Ty
from src.cir.parser import CirParseError, parse_cir
from src.cir.printer import print_module
from src.driver import _demo_cir_module

GOLDEN = Path(__file__).parents[1] / "docs" / "examples" / "abs.cir"


def roundtrip(text: str) -> str:
    return print_module(parse_cir(text))


# -- the round-trip property ----------------------------------------------
def test_golden_file_round_trips_byte_for_byte():
    text = GOLDEN.read_text()
    assert roundtrip(text) == text


def test_parsing_the_golden_file_rebuilds_the_hand_built_module():
    """Text and object graph agree, not just text and text."""
    assert parse_cir(GOLDEN.read_text(), "abs") == _demo_cir_module()


@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_every_built_corpus_module_round_trips(name, corpus_module):
    text = print_module(corpus_module(name))
    assert roundtrip(text) == text


@pytest.mark.parametrize("body", [
    "  %t0 = add i32 %x, 1",
    "  %t0 = sub i64 %x, 1",
    "  %t0 = icmp.ult i32 %x, 7",
    "  %t0 = fcmp.olt f64 1.5, 2.5",
    "  %t0 = sext i64 %x",
    "  %t0 = sitofp f64 %x",
    "  %t0 = not i32 %x",
    "  %t0 = neg i32 %x",
    "  %t0 = alloca i32",
    "  %t0 = alloca i32, 8",
    "  %t0 = load i32 %x",
    "  store i32 1, %x",
    "  %t0 = gep i32 %x, 3",
    "  %t0 = call i32 @g(1, %x)",
    "  call void @g()",
    "  print.i32 %x",
    "  print.f64 2.5",
    "  trap",
])
def test_every_instruction_shape_round_trips(body):
    text = f"func @f(i32 %x) -> void {{\nentry:\n{body}\n  ret void\n}}\n"
    assert roundtrip(text) == text


def test_globals_round_trip():
    text = ("global @counter : i32 = 0\n"
            "global @rate : f64 = 2.5\n"
            "global @table : i32[8]\n"
            "\n"
            "func @f() -> void {\nentry:\n  ret void\n}\n")
    assert roundtrip(text) == text


def test_both_branch_forms_round_trip():
    text = ("func @f(i1 %c) -> void {\n"
            "entry:\n  br %c ? a : b\n"
            "a:\n  br b\n"
            "b:\n  ret void\n}\n")
    assert roundtrip(text) == text


# -- types recovered from definitions -------------------------------------
def test_a_comparison_defines_an_i1_from_an_i32_operand_type():
    fn = parse_cir("func @f(i32 %x) -> i1 {\nentry:\n"
                   "  %t0 = icmp.slt i32 %x, 0\n  ret i1 %t0\n}\n").functions[0]
    cmp_instr = fn.blocks[0].instrs[0]
    assert cmp_instr.ty is Ty.I32              # the printed operand type
    assert cmp_instr.dest == Reg("t0", Ty.I1)  # rule 7: the result is i1


def test_alloca_and_gep_define_pointers():
    fn = parse_cir("func @f() -> void {\nentry:\n"
                   "  %p = alloca i32, 4\n"
                   "  %q = gep i32 %p, 1\n"
                   "  ret void\n}\n").functions[0]
    assert fn.blocks[0].instrs[0].dest.ty is Ty.PTR
    assert fn.blocks[0].instrs[1].dest.ty is Ty.PTR


def test_a_register_may_be_used_before_its_definition_appears_in_the_text():
    """A loop back edge puts the use in an earlier block than the definition."""
    fn = parse_cir(
        "func @f() -> i32 {\n"
        "entry:\n  br head\n"
        "head:\n  ret i32 %v\n"
        "body:\n  %v = add i32 1, 2\n  br head\n}\n").functions[0]
    assert fn.block("head").instrs[0].args[0] == Reg("v", Ty.I32)


def test_float_and_integer_literals_are_distinguished():
    fn = parse_cir("func @f() -> void {\nentry:\n"
                   "  %a = add i32 1, 2\n"
                   "  %b = fadd f64 1.5, 2.0\n"
                   "  ret void\n}\n").functions[0]
    assert fn.blocks[0].instrs[0].args[0] == ConstInt(1, Ty.I32)
    assert fn.blocks[0].instrs[1].args[0] == ConstFloat(1.5)


def test_negative_literals_parse():
    fn = parse_cir("func @f() -> i32 {\nentry:\n  ret i32 -2147483648\n}\n")
    assert fn.functions[0].blocks[0].instrs[0].args[0].value == -2147483648


# -- diagnostics -----------------------------------------------------------
@pytest.mark.parametrize("text,fragment", [
    ("func @f() -> i32 {\nentry:\n  %t = frobnicate i32 1\n  ret i32 0\n}\n",
     "unknown opcode"),
    ("func @f() -> i32 {\nentry:\n  ret i32 %missing\n}\n",
     "never defined"),
    ("func @f() -> i32 {\nentry:\n  ret i32 0\n",
     "unterminated function"),
    ("func @f() -> quux {\nentry:\n  ret i32 0\n}\n",
     "unknown type"),
    ("not a declaration\n", "expected 'func' or 'global'"),
    ("func @f() -> i32 {\n  ret i32 0\n}\n",
     "instruction before the first block label"),
    ("func @f() -> i32 {\nentry:\n  %t = neg i32 1, 2\n  ret i32 0\n}\n",
     "exactly one operand"),
])
def test_malformed_input_is_reported_not_crashed(text, fragment):
    with pytest.raises(CirParseError) as excinfo:
        parse_cir(text)
    assert fragment in str(excinfo.value)


def test_the_error_carries_a_line_number():
    with pytest.raises(CirParseError) as excinfo:
        parse_cir("func @f() -> i32 {\nentry:\n  ret i32 %nope\n}\n")
    assert excinfo.value.lineno == 3
