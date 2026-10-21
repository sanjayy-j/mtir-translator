"""Four-way differential tests (M8b): every program must agree on the
interpreter, LLVM (via clang), Wasm (wasmtime) and the bytecode VM.

Targets whose tool is missing are skipped, not failed.  The full 500-program
run is `python -m tests.harness.differential --random 500 [--opt 1]`; this
file runs the fixed corpus plus a smaller random sample so `pytest` stays fast.
"""
from pathlib import Path

import pytest

from tests.harness.differential import _same, corpus_paths, run_all
from tests.harness.gen_random import generate
from tests.harness.runners import _exit_status, _native_stdout

# Rejected with E004 by design (decision 0001); the C++ tests use it as
# their semantically invalid example, so it stays as it is.
ARRAYS = "arrays.mini"


def check(outcome):
    assert outcome.error is None, outcome.error
    assert outcome.agrees, {t: r for t, r in outcome.results.items()}


@pytest.mark.parametrize("path", [p for p in corpus_paths() if not p.endswith(ARRAYS)],
                         ids=lambda p: Path(p).name)
@pytest.mark.parametrize("opt", [0, 1])
def test_corpus(path, opt):
    check(run_all(path, opt=opt))


@pytest.mark.parametrize("seed", range(40))
@pytest.mark.parametrize("opt", [0, 1])
def test_random(seed, opt):
    check(run_all(source=generate(seed), name=f"seed{seed}", opt=opt))


@pytest.mark.parametrize("seed", [122, 475])
def test_out_of_range_int_literal_wraps_everywhere(seed):
    """Regression: these seeds use 4294967296 in all-literal expressions such
    as `0 / (4294967296 | 1)`.  Those used to be computed in i32, where the
    interpreter and the constant folder used the unwrapped value and every
    back end wrapped it to 0.  Since literals are typed by value (spec
    section 4) they are long arithmetic, which all targets must agree on."""
    check(run_all(source=generate(seed), name=f"seed{seed}", opt=1))


@pytest.mark.parametrize("seed", [93, 167, 286])
@pytest.mark.parametrize("opt", [0, 1])
def test_nan_sign_is_not_compared(seed, opt):
    """Regression: these seeds print inf - inf.  On x86 that NaN is negative
    and the native binary prints "-nan"; the other targets print "nan"."""
    check(run_all(source=generate(seed), name=f"seed{seed}", opt=opt))


@pytest.mark.parametrize("returncode,windows,expected", [
    (0, False, (0, False)),
    (255, False, (-1, False)),             # main returned -1
    (-4, False, (0, True)),                # SIGILL from llvm.trap
    (-5, False, (0, True)),                # SIGTRAP
    (0xFFFFFFFF, True, (-1, False)),       # Windows keeps all 32 bits of -1
    (0xFFFFFFFB, True, (-5, False)),       # ... so a negative return isn't a trap
    (0xC000001D, True, (0, True)),         # ud2: STATUS_ILLEGAL_INSTRUCTION
    (0x80000003, True, (0, True)),         # STATUS_BREAKPOINT
])
def test_native_exit_status(returncode, windows, expected):
    # Runs on every OS, so the Windows rules are checked on the Mac and in CI.
    assert _exit_status(returncode, windows) == expected


def test_native_stdout_reduces_msvc_nan_spellings():
    assert _native_stdout("1\n-nan(ind)\nnan(snan)\ninf\n") == "1\n-nan\nnan\ninf"


def test_same_ignores_only_the_sign_of_nan():
    # Runs without clang, so the rule is checked on machines that skip llvm.
    want = (0, "1\nnan\n-inf", False)
    assert _same("llvm", (0, "1\n-nan\n-inf", False), want)
    assert not _same("llvm", (0, "1\nnan\ninf", False), want)
    assert not _same("llvm", (0, "-1\nnan\n-inf", False), want)


def test_generator_is_deterministic():
    assert generate(7) == generate(7) and generate(7) != generate(8)
