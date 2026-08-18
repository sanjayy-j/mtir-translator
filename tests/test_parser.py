"""Unit tests for the parser (M1b).  Owner: Member 1."""

from pathlib import Path

import pytest

from src.frontend.ast_nodes import (
    Assign, Binary, Block, Call, FnDecl, For, If, IntLit, Return, Unary,
    VarRef, While, dump,
)
from src.frontend.lexer import LexError
from src.frontend.parser import ParseError, parse

CORPUS = Path(__file__).parent / "corpus"


def only_fn(src):
    prog = parse(src)
    assert len(prog.decls) == 1
    return prog.decls[0]


# -- expression shape ------------------------------------------------------
def test_precedence_multiplication_binds_tighter_than_addition():
    fn = only_fn("fn f() -> int { return 2 + 3 * 4; }")
    top = fn.body.stmts[0].value
    assert isinstance(top, Binary) and top.op == "+"
    assert isinstance(top.rhs, Binary) and top.rhs.op == "*"


def test_parentheses_override_precedence():
    fn = only_fn("fn f() -> int { return (2 + 3) * 4; }")
    top = fn.body.stmts[0].value
    assert top.op == "*" and top.lhs.op == "+"


def test_comparison_binds_tighter_than_logical_and():
    fn = only_fn("fn f() -> bool { return a < b && c > d; }")
    top = fn.body.stmts[0].value
    assert top.op == "&&"
    assert top.lhs.op == "<" and top.rhs.op == ">"


def test_left_associativity_of_subtraction():
    # 10 - 3 - 2 must parse as (10 - 3) - 2, not 10 - (3 - 2).
    fn = only_fn("fn f() -> int { return 10 - 3 - 2; }")
    top = fn.body.stmts[0].value
    assert isinstance(top.lhs, Binary) and top.lhs.op == "-"
    assert isinstance(top.rhs, IntLit) and top.rhs.value == 2


def test_unary_minus_and_nesting():
    fn = only_fn("fn f() -> int { return - -x; }")
    top = fn.body.stmts[0].value
    assert isinstance(top, Unary) and isinstance(top.operand, Unary)


def test_call_and_index_expressions():
    fn = only_fn("fn f() -> int { return g(1, h(2)) + xs[i + 1]; }")
    top = fn.body.stmts[0].value
    assert isinstance(top.lhs, Call) and len(top.lhs.args) == 2
    assert isinstance(top.lhs.args[1], Call)


# -- statements ------------------------------------------------------------
def test_else_if_chain_nests_as_if_inside_else():
    fn = only_fn("fn f() -> int { if (a) { return 1; } else if (b) { return 2; } else { return 3; } }")
    outer = fn.body.stmts[0]
    assert isinstance(outer, If) and isinstance(outer.else_blk, If)
    assert isinstance(outer.else_blk.else_blk, Block)


def test_for_loop_clauses():
    fn = only_fn("fn f() -> void { for (let i: int = 0; i < 3; i = i + 1) { g(); } }")
    loop = fn.body.stmts[0]
    assert isinstance(loop, For)
    assert loop.init is not None and loop.cond is not None and loop.step is not None


def test_for_loop_with_all_clauses_empty():
    fn = only_fn("fn f() -> void { for (;;) { break; } }")
    loop = fn.body.stmts[0]
    assert loop.init is None and loop.cond is None and loop.step is None


def test_assignment_vs_expression_statement():
    fn = only_fn("fn f() -> void { x = 1; g(); }")
    assert isinstance(fn.body.stmts[0], Assign)
    assert not isinstance(fn.body.stmts[1], Assign)


def test_function_signature_and_params():
    fn = only_fn("fn add(a: int, b: long) -> long { return a + b; }")
    assert isinstance(fn, FnDecl) and fn.name == "add"
    assert [p.name for p in fn.params] == ["a", "b"]
    assert str(fn.ret_type) == "long"


def test_omitted_return_type_defaults_to_void():
    assert str(only_fn("fn f() { }").ret_type) == "void"


def test_array_type_carries_its_length():
    fn = only_fn("fn f() -> void { let xs: int[16] = 0; }")
    assert str(fn.body.stmts[0].type_node) == "int[16]"


def test_global_declaration():
    prog = parse("global n: int = 5;\nfn f() -> void { }")
    assert len(prog.decls) == 2


# -- source positions ------------------------------------------------------
def test_nodes_carry_source_positions():
    fn = only_fn("fn f() -> int {\n    return 1;\n}")
    ret = fn.body.stmts[0]
    assert isinstance(ret, Return) and ret.line == 2 and ret.col == 5


# -- error cases -----------------------------------------------------------
@pytest.mark.parametrize("src,fragment", [
    ("fn f() -> int { let a: int = 1 return a; }", "expected ';'"),
    ("fn f() -> int { return 0;", "expected '}'"),
    ("fn f() -> int { 3 = 4; }", "not assignable"),
    ("let a: int = 1;", "expected 'fn' or 'global'"),
    ("fn f() -> int { return 1 + ; }", "expected an expression"),
    ("fn f() -> nope { }", "expected a type name"),
])
def test_syntax_errors(src, fragment):
    with pytest.raises(ParseError) as e:
        parse(src)
    assert fragment in e.value.message


# -- corpus ----------------------------------------------------------------
@pytest.mark.parametrize("path", sorted((CORPUS / "valid").glob("*.mini")),
                         ids=lambda p: p.name)
def test_valid_corpus_parses(path):
    prog = parse(path.read_text(), str(path))
    assert prog.decls
    assert dump(prog).startswith("Program")


@pytest.mark.parametrize("path", sorted((CORPUS / "boundary").glob("*.mini")),
                         ids=lambda p: p.name)
def test_boundary_corpus_parses(path):
    # Boundary programs are syntactically valid; what is exceptional about
    # them is their runtime behaviour, which the differential harness checks.
    assert parse(path.read_text(), str(path)).decls


@pytest.mark.parametrize("path", sorted((CORPUS / "invalid").glob("*.mini")),
                         ids=lambda p: p.name)
def test_invalid_corpus_is_rejected(path):
    with pytest.raises((ParseError, LexError)):
        parse(path.read_text(), str(path))
