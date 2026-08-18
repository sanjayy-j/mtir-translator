"""End-to-end tests for the CLI driver (M9).  Owner: Member 1."""

from pathlib import Path

import pytest

from src.driver import main

EXAMPLE = Path(__file__).parents[1] / "docs" / "examples" / "abs.mini"


def test_emit_tokens(capsys):
    assert main(["--emit=tokens", str(EXAMPLE)]) == 0
    out = capsys.readouterr().out
    assert "KW_FN" in out and "IDENT" in out


def test_emit_ast(capsys):
    assert main(["--emit=ast", str(EXAMPLE)]) == 0
    out = capsys.readouterr().out
    assert out.startswith("Program")
    assert "FnDecl abs(x: int) -> int" in out


def test_demo_cir_matches_golden(capsys):
    golden = (Path(__file__).parents[1] / "docs" / "examples" / "abs.cir").read_text()
    assert main(["--demo-cir"]) == 0
    assert capsys.readouterr().out == golden


def test_missing_file_exits_2(capsys):
    assert main(["--emit=ast", "no/such/file.mini"]) == 2
    assert "no such file" in capsys.readouterr().err


def test_syntax_error_exits_1(tmp_path, capsys):
    bad = tmp_path / "bad.mini"
    bad.write_text("fn main() -> int { return 1 }")
    assert main(["--emit=ast", str(bad)]) == 1
    assert "syntax error" in capsys.readouterr().err


@pytest.mark.parametrize("stage", ["cir", "ll", "wat", "sbc"])
def test_unimplemented_stages_report_owner_and_week(stage, capsys):
    """A stage that is not built yet must say so precisely, rather than
    crashing -- the state of the project is readable from the tool."""
    assert main([f"--emit={stage}", str(EXAMPLE)]) == 3
    err = capsys.readouterr().err
    assert "not implemented yet" in err
    assert "Member" in err and "Week" in err
