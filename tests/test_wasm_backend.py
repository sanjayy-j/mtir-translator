"""Tests for the WebAssembly back end (M6c).  Owner: Member 4.

Each program is emitted as WAT, assembled and run under wasmtime, and must
agree with the CIR reference interpreter on (return value, stdout, trapped).
"""
import pytest

wasmtime = pytest.importorskip("wasmtime")

from src.backend.wasm.emit_wat import emit
from src.cir.interp import run
from tests.harness.runners import run_wasm
from tests.programs import CORPUS, SOURCES


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


@pytest.mark.parametrize("name", SOURCES)
def test_program_agrees(mini_module, name):
    agree(mini_module(SOURCES[name]))


def test_traps_are_observed(mini_module):
    assert agree(mini_module(SOURCES["srem_int_min_traps"]))[2] is True
    assert agree(mini_module(SOURCES["bounds_trap"]))[2] is True


# -- control flow: structured (default) vs the dispatch-loop fallback --------
@pytest.mark.parametrize("name", CORPUS)
def test_dispatch_fallback_still_agrees(corpus_module, name):
    module = corpus_module(name)
    assert run_wasm(module, structured=False) == run(module)


@pytest.mark.parametrize("name", CORPUS)
def test_structured_output_has_no_dispatch_loop(corpus_module, name):
    assert "$dispatch" not in emit(corpus_module(name))
