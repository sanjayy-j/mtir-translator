"""Tests for the CIR reference interpreter (the semantic oracle).

Each test pins one row of docs/divergence.md or one opcode whose meaning the
back ends are checked against.  Ops MiniLang cannot spell (lshr, udiv,
fptosi, ...) are built directly as CIR.
"""
import math

import pytest

from src.cir.interp import fmt_float, run
from src.cir.ir import (BasicBlock, ConstFloat, ConstInt, Function, Instr,
                        Module, Reg, Ty)


def run_src(mini_module, src):
    return run(mini_module(src))


def one_op(op, ty, args, ret_ty=None):
    """main() { %r = op ty args; ret %r }"""
    ret_ty = ret_ty or ty
    r = Reg("r", ret_ty)
    fn = Function("main", [], ret_ty, [BasicBlock("entry", [
        Instr(op, ty, r, args), Instr("ret", ret_ty, None, [r])])])
    return run(Module("m", [], [fn]))


@pytest.mark.parametrize("name,expected,trapped", [
    ("valid/arith", ["55"], False),
    ("valid/arrays", ["107.000000", "140"], False),
    ("valid/control_flow", ["16"], False),
    ("valid/recursion", ["3628800", "1"], False),
    ("boundary/nesting", ["3"], False),
    ("boundary/shift_edge", ["1", "-2147483648", "1", "2"], False),
    ("boundary/div_edge", ["3"], True),
])
def test_corpus(corpus_module, name, expected, trapped):
    _, out, t = run(corpus_module(name))
    assert out.split() == expected and t is trapped


def test_int_to_float_conversion(mini_module):
    _, out, t = run_src(mini_module, """
        fn main() -> int { let i: int = 3; let f: float = i; print_float(f / 2.0); return 0; }""")
    assert (out, t) == ("1.500000", False)


def test_print_float_matches_c_percent_f():
    assert fmt_float(107.0) == "107.000000"
    assert fmt_float(-0.0) == "-0.000000"
    assert fmt_float(math.inf) == "inf" and fmt_float(math.nan) == "nan"


def test_not_on_bool(mini_module):
    _, out, _ = run_src(mini_module, """
        fn main() -> int { if (!true) { print_int(1); } else { print_int(2); } return 0; }""")
    assert out == "2"


def test_lshr_shifts_in_zeros():
    assert one_op("lshr", Ty.I32, [ConstInt(-8), ConstInt(1)])[0] == 0x7FFFFFFC
    assert one_op("ashr", Ty.I32, [ConstInt(-8), ConstInt(1)])[0] == -4


def test_shift_count_mod_width():
    assert one_op("shl", Ty.I32, [ConstInt(1), ConstInt(33)])[0] == 2
    assert one_op("shl", Ty.I64, [ConstInt(1, Ty.I64), ConstInt(33, Ty.I64)])[0] == 1 << 33


def test_unsigned_division_and_compare():
    assert one_op("udiv", Ty.I32, [ConstInt(-1), ConstInt(2)])[0] == 0x7FFFFFFF
    assert one_op("urem", Ty.I32, [ConstInt(-1), ConstInt(10)])[0] == 5
    assert one_op("icmp.ult", Ty.I64, [ConstInt(1 << 40, Ty.I64), ConstInt(-1, Ty.I64)],
                  Ty.I1)[0] == 1


def test_signed_division_truncates_toward_zero_exactly_for_i64():
    big = (1 << 62) + 1
    assert one_op("sdiv", Ty.I64, [ConstInt(big, Ty.I64), ConstInt(3, Ty.I64)])[0] == big // 3
    assert one_op("sdiv", Ty.I32, [ConstInt(-7), ConstInt(2)])[0] == -3
    assert one_op("srem", Ty.I32, [ConstInt(-7), ConstInt(2)])[0] == -1


@pytest.mark.parametrize("op,a,b", [("sdiv", 1, 0), ("urem", 1, 0),
                                    ("srem", -2147483648, -1)])
def test_division_traps(op, a, b):
    assert one_op(op, Ty.I32, [ConstInt(a), ConstInt(b)])[2] is True


def test_overflow_wraps():
    assert one_op("add", Ty.I32, [ConstInt(2147483647), ConstInt(1)])[0] == -2147483648


@pytest.mark.parametrize("v,trapped", [(2147483647.9, False), (2147483648.0, True),
                                       (-2147483648.5, False), (math.nan, True)])
def test_fptosi_range(v, trapped):
    assert one_op("fptosi", Ty.I32, [ConstFloat(v)])[2] is trapped


def test_zext_i32_to_i64():
    assert one_op("zext", Ty.I64, [ConstInt(-1, Ty.I32)])[0] == 0xFFFFFFFF


def test_fdiv_by_zero_is_ieee():
    assert one_op("fdiv", Ty.F64, [ConstFloat(1.0), ConstFloat(0.0)])[0] == math.inf
    assert one_op("fcmp.one", Ty.F64, [ConstFloat(math.nan), ConstFloat(1.0)], Ty.I1)[0] == 0


@pytest.mark.parametrize("index", ["-1", "4"])
def test_out_of_bounds_index_traps(mini_module, index):
    _, _, t = run_src(mini_module, f"""
        fn main() -> int {{ let xs: int[4]; let i: int = {index}; xs[i] = 1; return 0; }}""")
    assert t is True


def test_locals_are_per_call(mini_module):
    _, out, _ = run_src(mini_module, """
        fn f(n: int) -> int {
            let x: int = n;
            if (n > 0) { let y: int = f(n - 1); }
            return x;
        }
        fn main() -> int { print_int(f(5)); return 0; }""")
    assert out == "5"
