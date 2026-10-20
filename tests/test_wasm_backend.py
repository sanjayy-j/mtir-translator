"""Tests for the WebAssembly back end (M6c).  Owner: Member 4.

Each program is emitted as WAT, assembled and run under wasmtime, and must
agree with the CIR reference interpreter on (return value, stdout, trapped).
"""
import pytest

wasmtime = pytest.importorskip("wasmtime")

from src.backend.wasm.emit_wat import emit
from src.cir.interp import run
from tests.harness.runners import run_wasm

CORPUS = ["valid/arith", "valid/control_flow", "valid/recursion",
          "boundary/nesting", "boundary/shift_edge", "boundary/div_edge"]


def agree(module):
    expected = run(module)
    assert run_wasm(module) == expected
    return expected


@pytest.mark.parametrize("name", CORPUS)
def test_corpus_agrees_with_interpreter(corpus_module, name):
    agree(corpus_module(name))


@pytest.mark.parametrize("name", CORPUS)
def test_output_validates(corpus_module, name):
    wasmtime.Module(wasmtime.Engine(), wasmtime.wat2wasm(emit(corpus_module(name))))


SOURCES = {
    "recursion_with_locals": """
        fn f(n: int) -> int {
            let x: int = n;
            if (n > 0) { let y: int = f(n - 1); }
            return x;
        }
        fn main() -> int { print_int(f(5)); return 0; }""",
    "srem_int_min_traps": """
        fn r(a: int, b: int) -> int { return a % b; }
        fn main() -> int { print_int(r(7, 3)); print_int(r(-2147483648, -1)); return 0; }""",
    "conversions": """
        fn main() -> int {
            let i: int = -7; let l: long = i; let f: float = i;
            print_float(f / 2.0);
            if (l < 0) { print_int(1); }
            return 0;
        }""",
    "bounds_trap": """
        fn main() -> int { let xs: int[4]; let i: int = 4; xs[0] = 1; print_int(xs[0]); xs[i] = 2; return 0; }""",
    "long_arithmetic": """
        fn main() -> int {
            let a: long = 4294967296; let b: long = a * 3 - 1;
            if (b > a) { print_int(2); }
            let n: long = -a; if (n < 0) { print_int(-1); }
            return 0;
        }""",
    "globals_and_bools": """
        global g: int = 5;
        global flag: bool = true;
        fn bump() { g = g + 1; }
        fn main() -> int { bump(); bump(); if (!flag) { print_int(0); } print_int(g); return g; }""",
}


@pytest.mark.parametrize("name", SOURCES)
def test_program_agrees(mini_module, name):
    agree(mini_module(SOURCES[name]))


def test_traps_are_observed(mini_module):
    assert agree(mini_module(SOURCES["srem_int_min_traps"]))[2] is True
    assert agree(mini_module(SOURCES["bounds_trap"]))[2] is True
