"""Tests for the bytecode back end and reference VM (M7).  Owner: Member 4.

Review 2 defect #1 was that emit_sbc.py and vm.py spoke different
instruction sets.  These tests run the emitted bytecode on the VM and require
agreement with the CIR reference interpreter.
"""
from pathlib import Path

import pytest

from src.backend.stackvm.emit_sbc import emit
from src.backend.stackvm.vm import run as run_vm
from src.cir.interp import run
from src.cir.parser import parse_cir
from tests.programs import CORPUS, SOURCES

EXAMPLES = Path(__file__).parents[1] / "docs" / "examples"


def agree(module):
    expected = run(module)
    assert run_vm(emit(module)) == expected
    return expected


@pytest.mark.parametrize("name", CORPUS)
def test_corpus_agrees_with_interpreter(corpus_module, name):
    agree(corpus_module(name))


@pytest.mark.parametrize("name", SOURCES)
def test_program_agrees(mini_module, name):
    agree(mini_module(SOURCES[name]))


def test_abs_matches_the_hand_written_bytecode():
    """docs/examples/abs.sbc.txt is the format spec; the emitter must match it
    up to label names."""
    emitted = emit(parse_cir((EXAMPLES / "abs.cir").read_text(), "abs"))
    ops = [l.split()[0] for l in emitted.splitlines() if l.startswith(" ") and ".params" not in l]
    hand = [l.split()[0] for l in (EXAMPLES / "abs.sbc.txt").read_text().splitlines()
            if l.startswith(" ")]
    assert ops == hand


def test_hand_written_bytecode_runs():
    text = (EXAMPLES / "abs.sbc.txt").read_text()
    text = text.replace("abs:", "abs:\n        .params x")
    text += "main:\n        PUSHI -7\n        CALL abs 1\n        RET\n"
    assert run_vm(text) == (7, "", False)


def test_false_edge_does_not_fall_through(mini_module):
    """br.cond used to emit only its true edge."""
    agree(mini_module("""
        fn main() -> int {
            let i: int = 0;
            while (i < 3) { if (i == 1) { print_int(100); } else { print_int(i); } i = i + 1; }
            return i;
        }"""))
