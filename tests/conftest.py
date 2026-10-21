"""Shared pytest fixtures.

Added by Member 3 for the M3/M4/M5 test files; nothing here is specific to one
module, so it lives at the root of tests/ rather than in any one of them.
"""

from pathlib import Path

import pytest

from src.cir.builder import build
from src.frontend.parser import parse

ROOT = Path(__file__).parents[1]
CORPUS = ROOT / "tests" / "corpus"
EXAMPLES = ROOT / "docs" / "examples"


@pytest.fixture
def corpus_module():
    """Build the CIR module for a corpus program named 'valid/arith' etc."""
    def load(name: str):
        path = CORPUS / f"{name}.mini"
        return build(parse(path.read_text(), str(path)), path.stem)
    return load


@pytest.fixture
def mini_module():
    """Build the CIR module for a MiniLang source string."""
    def load(source: str, name: str = "test"):
        return build(parse(source, "<test>"), name)
    return load
