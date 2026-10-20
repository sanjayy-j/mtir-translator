"""Four-way differential tests (M8b): every program must agree on the
interpreter, LLVM (via clang), Wasm (wasmtime) and the bytecode VM.

Targets whose tool is missing are skipped, not failed.  The full 500-program
run is `python -m tests.harness.differential --random 500 [--opt 1]`; this
file runs the fixed corpus plus a smaller random sample so `pytest` stays fast.
"""
from pathlib import Path

import pytest

from tests.harness.differential import corpus_paths, run_all
from tests.harness.gen_random import generate


def check(outcome):
    assert outcome.error is None, outcome.error
    assert outcome.agrees, {t: r for t, r in outcome.results.items()}


@pytest.mark.parametrize("path", corpus_paths(),
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
    """Regression: these seeds use 4294967296 in an int context.  The
    interpreter and the constant folder used the unwrapped value; every back
    end wraps it to 0 (and so traps on dividing by it)."""
    check(run_all(source=generate(seed), name=f"seed{seed}", opt=1))


def test_generator_is_deterministic():
    assert generate(7) == generate(7) and generate(7) != generate(8)
