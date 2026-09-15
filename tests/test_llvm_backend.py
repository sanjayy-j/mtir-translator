"""Tests for the LLVM IR back end (M5).  Owner: Member 3.

Two layers of checking:

  * ``assert_structurally_valid`` re-reads the emitted text and enforces the
    invariants LLVM itself enforces -- one terminator per block, every branch
    target defined, every register defined before use, no duplicate labels.
    It runs everywhere, including a machine with no LLVM installed.
  * ``test_llvm_as_accepts_*`` shells out to the real ``llvm-as`` and is
    skipped when the toolchain is absent.  CI installs LLVM, so the real
    verifier runs there.

The second is the authority; the first is what keeps the failure message
readable and the loop fast.
"""

import re
import shutil
import subprocess
from pathlib import Path

import pytest

from src.backend.llvm.emit_ll import EmitError, emit, fmt_float
from src.backend.llvm.typemap import llvm_ty
from src.cir.ir import Ty
from src.cir.parser import parse_cir
from src.driver import _demo_cir_module

EXAMPLES = Path(__file__).parents[1] / "docs" / "examples"
TERMINATORS = ("br ", "ret ", "ret void", "unreachable", "switch ")

_DEF_RE = re.compile(r"^\s*(%[\w.]+)\s*=")
_USE_RE = re.compile(r"%[\w.]+")
_LABEL_RE = re.compile(r"^([\w.]+):$")
_HEAD_RE = re.compile(r"^define\s+\S+\s+@[\w.]+\((.*?)\)\s*\{$")


def assert_structurally_valid(text: str) -> None:
    """Check the emitted module against the rules llvm-as would enforce."""
    in_fn = False
    labels: set = set()
    targets: list = []
    defined: set = set()
    used_before_def: list = []
    block_terminated = True

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith(";"):
            continue

        head = _HEAD_RE.match(line)
        if head:
            assert not in_fn, "nested 'define'"
            in_fn, labels, targets, defined = True, set(), [], set()
            block_terminated = True
            for param in filter(None, (p.strip() for p in head.group(1).split(","))):
                defined.add(param.split()[-1])
            continue

        if not in_fn:
            continue

        if line == "}":
            assert block_terminated, "function ends with an unterminated block"
            assert set(targets) <= labels, \
                f"branch to a label that does not exist: {set(targets) - labels}"
            assert not used_before_def, f"used before defined: {used_before_def}"
            in_fn = False
            continue

        label = _LABEL_RE.match(line)
        if label:
            assert block_terminated, \
                f"block before {label.group(1)!r} has no terminator"
            assert label.group(1) not in labels, \
                f"duplicate label {label.group(1)!r}"
            labels.add(label.group(1))
            block_terminated = False
            continue

        assert not block_terminated, f"instruction after a terminator: {line!r}"

        targets += re.findall(r"label %([\w.]+)", line)
        rhs = line.split("=", 1)[1] if _DEF_RE.match(line) else line
        rhs = re.sub(r"label %[\w.]+", "", rhs)
        for use in _USE_RE.findall(rhs):
            if use not in defined:
                used_before_def.append(use)
        definition = _DEF_RE.match(line)
        if definition:
            defined.add(definition.group(1))
        if line.startswith(TERMINATORS):
            block_terminated = True

    assert not in_fn, "unterminated 'define'"


def ll_of(source: str) -> str:
    return emit(parse_cir(source))


# -- the worked example ----------------------------------------------------
def test_abs_matches_the_hand_written_llvm_ir():
    """The generated abs() is identical to the hand-written one in
    docs/examples/abs.ll, which llvm-as already validates in CI."""
    text = emit(_demo_cir_module())
    body = [line for line in text.splitlines()
            if line and not line.startswith(";")]
    assert body == [
        "define i32 @abs(i32 %x) {",
        "entry:",
        "  %t0 = icmp slt i32 %x, 0",
        "  br i1 %t0, label %then, label %exit",
        "then:",
        "  %t1 = sub i32 0, %x",
        "  ret i32 %t1",
        "exit:",
        "  ret i32 %x",
        "}",
    ]
    assert_structurally_valid(text)


def test_generated_abs_ll_matches_the_checked_in_golden_file():
    text = emit(parse_cir((EXAMPLES / "abs.cir").read_text(), "abs"))
    assert text == (EXAMPLES / "abs.gen.ll").read_text()


@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_every_corpus_module_emits_structurally_valid_ir(name, corpus_module):
    assert_structurally_valid(emit(corpus_module(name)))


@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "boundary/div_edge"])
def test_optimised_modules_also_emit_valid_ir(name, corpus_module):
    from src.opt import run as run_opt
    assert_structurally_valid(emit(run_opt(corpus_module(name), 1)))


# -- type mapping ----------------------------------------------------------
@pytest.mark.parametrize("ty,expected", [
    (Ty.I1, "i1"), (Ty.I32, "i32"), (Ty.I64, "i64"),
    (Ty.F64, "double"), (Ty.PTR, "ptr"), (Ty.VOID, "void"),
])
def test_type_mapping(ty, expected):
    assert llvm_ty(ty) == expected


# -- instruction selection -------------------------------------------------
def emitted(body: str, header: str = "func @f(i32 %x, i32 %y) -> void {") -> str:
    return ll_of(f"{header}\nentry:\n{body}\n  ret void\n}}\n")


@pytest.mark.parametrize("cir,llvm", [
    ("  %t = add i32 %x, %y", "%t = add i32 %x, %y"),
    ("  %t = mul i32 %x, %y", "%t = mul i32 %x, %y"),
    ("  %t = and i32 %x, %y", "%t = and i32 %x, %y"),
    ("  %t = icmp.slt i32 %x, %y", "%t = icmp slt i32 %x, %y"),
    ("  %t = icmp.uge i32 %x, %y", "%t = icmp uge i32 %x, %y"),
    ("  %t = sext i64 %x", "%t = sext i32 %x to i64"),
    ("  %t = zext i32 %x", "%t = zext i32 %x to i32"),
    ("  %t = trunc i32 %x", "%t = trunc i32 %x to i32"),
    ("  %t = sitofp f64 %x", "%t = sitofp i32 %x to double"),
    ("  %t = not i32 %x", "%t = xor i32 %x, -1"),
    ("  %t = neg i32 %x", "%t = sub i32 0, %x"),
    ("  %p = alloca i32", "%p = alloca i32"),
    ("  %p = alloca i32, 8", "%p = alloca i32, i32 8"),
    ("  %t = load i32 %x", "%t = load i32, ptr %x"),
    ("  store i32 1, %x", "store i32 1, ptr %x"),
    ("  print.i32 %x", "call i32 (ptr, ...) @printf(ptr @.fmt.i32, i32 %x)"),
])
def test_opcode_lowering(cir, llvm):
    assert llvm in emitted(cir)


def test_float_operations():
    text = ll_of("func @f(f64 %x) -> void {\nentry:\n"
                 "  %a = fadd f64 %x, 1.5\n"
                 "  %b = neg f64 %x\n"
                 "  %c = fcmp.olt f64 %x, 2.0\n"
                 "  print.f64 %a\n  ret void\n}\n")
    assert "%a = fadd double %x, 1.5" in text
    assert "%b = fneg double %x" in text
    assert "%c = fcmp olt double %x, 2.0" in text


def test_boolean_constants_are_spelled_true_and_false():
    text = ll_of("func @f() -> i1 {\nentry:\n"
                 "  br 1 ? a : b\n"
                 "a:\n  ret i1 1\n"
                 "b:\n  ret i1 0\n}\n")
    assert "br i1 true, label %a, label %b" in text
    assert "ret i1 true" in text and "ret i1 false" in text


def test_a_void_call_is_emitted_without_a_destination():
    text = ll_of("func @g() -> void {\nentry:\n  ret void\n}\n"
                 "func @f() -> void {\nentry:\n  call void @g()\n  ret void\n}\n")
    assert "  call void @g()" in text


def test_call_arguments_are_typed_from_the_callee_signature():
    text = ll_of("func @g(i64 %a) -> void {\nentry:\n  ret void\n}\n"
                 "func @f() -> void {\nentry:\n  call void @g(3)\n  ret void\n}\n")
    assert "call void @g(i64 3)" in text


def test_a_wrong_argument_count_is_reported_not_emitted():
    with pytest.raises(EmitError):
        ll_of("func @g(i32 %a) -> void {\nentry:\n  ret void\n}\n"
              "func @f() -> void {\nentry:\n  call void @g(1, 2)\n  ret void\n}\n")


def test_globals_are_emitted_with_their_initialiser():
    text = ll_of("global @counter : i32 = 3\n"
                 "global @rate : f64 = 2.5\n"
                 "global @blank : i32\n\n"
                 "func @f() -> void {\nentry:\n  ret void\n}\n")
    assert "@counter = global i32 3" in text
    assert "@rate = global double 2.5" in text
    assert "@blank = global i32 zeroinitializer" in text


def test_runtime_declarations_appear_only_when_used():
    plain = ll_of("func @f() -> void {\nentry:\n  ret void\n}\n")
    assert "@printf" not in plain and "llvm.trap" not in plain


# -- docs/divergence.md, realised ------------------------------------------
def test_row_1_and_2_division_guard():
    """Zero divisor and INT_MIN / -1 both branch to a trap block."""
    text = emitted("  %t = sdiv i32 %x, %y")
    assert "icmp eq i32 %y, 0" in text
    assert "icmp eq i32 %x, -2147483648" in text
    assert "icmp eq i32 %y, -1" in text
    assert "call void @llvm.trap()" in text
    assert "unreachable" in text
    assert_structurally_valid(text)


def test_unsigned_division_guards_only_the_zero_divisor():
    text = emitted("  %t = udiv i32 %x, %y")
    assert "icmp eq i32 %y, 0" in text
    assert "-2147483648" not in text


def test_a_provably_non_zero_divisor_needs_no_guard():
    text = emitted("  %t = sdiv i32 %x, 4")
    assert "llvm.trap" not in text
    assert "%t = sdiv i32 %x, 4" in text


def test_a_constant_minus_one_divisor_keeps_the_overflow_guard():
    text = emitted("  %t = sdiv i32 %x, -1")
    assert "icmp eq i32 %x, -2147483648" in text


def test_row_3_constant_shift_count_is_masked_at_compile_time():
    assert "%t = shl i32 %x, 1" in emitted("  %t = shl i32 %x, 33")
    assert "%t = shl i32 %x, 0" in emitted("  %t = shl i32 %x, 32")


def test_row_3_dynamic_shift_count_is_masked_with_an_and():
    text = emitted("  %t = shl i32 %x, %y")
    assert "and i32 %y, 31" in text
    assert re.search(r"%t = shl i32 %x, %g\.\d+", text)


def test_row_3_uses_the_right_mask_for_i64():
    text = ll_of("func @f(i64 %x, i64 %y) -> void {\nentry:\n"
                 "  %t = shl i64 %x, %y\n  ret void\n}\n")
    assert "and i64 %y, 63" in text


def test_row_4_no_nsw_or_nuw_flags_are_emitted():
    """CIR defines signed overflow as wraparound, so the flags that would make
    it poison must not appear anywhere."""
    text = emitted("  %t = add i32 %x, %y\n  %u = mul i32 %x, %y\n"
                   "  %v = sub i32 %x, %y")
    assert "nsw" not in text and "nuw" not in text


def test_row_6_fptosi_checks_nan_and_range():
    text = ll_of("func @f(f64 %x) -> void {\nentry:\n"
                 "  %t = fptosi i32 %x\n  ret void\n}\n")
    assert "fcmp uno double %x, %x" in text
    assert "fcmp ole double %x, -2147483649.0" in text
    assert "fcmp oge double %x, 2147483648.0" in text
    assert_structurally_valid(text)


def test_row_7_a_gep_off_a_sized_alloca_is_bounds_checked():
    text = ll_of("func @f(i32 %i) -> void {\nentry:\n"
                 "  %p = alloca i32, 8\n"
                 "  %q = gep i32 %p, %i\n  ret void\n}\n")
    assert "icmp uge i32 %i, 8" in text
    assert "call void @llvm.trap()" in text
    assert_structurally_valid(text)


def test_row_7_a_constant_index_in_range_needs_no_check():
    text = ll_of("func @f() -> void {\nentry:\n"
                 "  %p = alloca i32, 8\n"
                 "  %q = gep i32 %p, 3\n  ret void\n}\n")
    assert "llvm.trap" not in text


def test_row_7_a_constant_index_out_of_range_is_still_checked():
    text = ll_of("func @f() -> void {\nentry:\n"
                 "  %p = alloca i32, 8\n"
                 "  %q = gep i32 %p, 9\n  ret void\n}\n")
    assert "icmp uge i32 9, 8" in text


def test_a_cir_trap_does_not_terminate_the_llvm_block():
    """'trap' is not a CIR terminator, so the block's own terminator follows
    it; emitting 'unreachable' here would make the LLVM block malformed."""
    text = ll_of("func @f() -> void {\nentry:\n  trap\n  ret void\n}\n")
    assert "  call void @llvm.trap()\n  ret void" in text
    assert_structurally_valid(text)


# -- float literal spelling ------------------------------------------------
@pytest.mark.parametrize("value,expected", [
    (1.5, "1.5"), (100.0, "100.0"), (0.0, "0.0"), (-2.5, "-2.5"),
])
def test_decimal_float_literals(value, expected):
    assert fmt_float(value) == expected


def test_exponent_float_literals_keep_a_decimal_point():
    """LLVM's lexer only sees a float once it has read a '.', so '1e-05' would
    lex as the integer 1."""
    text = fmt_float(1e-05)
    assert "." in text.split("e")[0]


def test_infinity_uses_the_hexadecimal_form():
    assert fmt_float(float("inf")) == "0x7FF0000000000000"


# -- the real verifier -----------------------------------------------------
LLVM_AS = shutil.which("llvm-as")


@pytest.mark.skipif(LLVM_AS is None, reason="llvm-as is not installed")
@pytest.mark.parametrize("name", ["valid/arith", "valid/control_flow",
                                  "valid/recursion", "boundary/nesting",
                                  "boundary/div_edge", "boundary/shift_edge"])
def test_llvm_as_accepts_the_generated_ir(name, corpus_module, tmp_path):
    path = tmp_path / "out.ll"
    path.write_text(emit(corpus_module(name)))
    result = subprocess.run([LLVM_AS, str(path), "-o", str(tmp_path / "out.bc")],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


@pytest.mark.skipif(LLVM_AS is None, reason="llvm-as is not installed")
def test_llvm_as_accepts_the_generated_golden_file(tmp_path):
    result = subprocess.run(
        [LLVM_AS, str(EXAMPLES / "abs.gen.ll"), "-o", str(tmp_path / "abs.bc")],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
