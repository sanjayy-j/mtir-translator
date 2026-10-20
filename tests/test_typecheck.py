"""Tests for the type checker (M2): one per diagnostic in minilang-spec.md section 7,
plus the conversions and scoping rules of sections 4 and 5."""
import pytest

from src.frontend.parser import parse
from src.sema.symtab import Symbol, SymbolTable
from src.sema.typecheck import TypeError_, check


def chk(body, extra=""):
    return check(parse(f"{extra}\nfn main() -> int {{ {body} return 0; }}", "<t>"))


@pytest.mark.parametrize("code,body,extra", [
    ("E001", "x = 1;", ""),
    ("E002", "let x: int = 1; let x: int = 2;", ""),
    ("E002", "", "fn f() {} fn f() {}"),
    ("E003", "let b: bool = true; let x: int = b + 1;", ""),
    ("E003", "let l: long = 1; let f: float = 1.0; let y: float = l * f;", ""),
    ("E003", "if (1) { }", ""),
    ("E004", "let b: bool = 1;", ""),
    ("E005", "let l: long = 1; let x: int = l;", ""),
    ("E005", "let f: float = 1.0; let x: int = f;", ""),
    ("E006", "nope();", ""),
    ("E007", "f(1, 2);", "fn f(a: int) {}"),
    ("E008", "f(true);", "fn f(a: int) {}"),
    ("E009", "", "fn f(a: int) -> int { if (a > 0) { return 1; } }"),
    ("E010", "", "fn f() { return 1; }"),
    ("E011", "break;", ""),
    ("E012", "let x: int = 1; let y: int = x[0];", ""),
    ("E012", "let xs: int[4]; let y: int = xs[true];", ""),
])
def test_diagnostics(code, body, extra):
    with pytest.raises(TypeError_) as e:
        chk(body, extra)
    assert e.value.code == code
    assert f"error[{code}]" in str(e.value)


@pytest.mark.parametrize("body", [
    "let l: long = 5; let m: long = l + 1;",             # int -> long in an operand
    "let f: float = 2.0; let g: float = f * 3;",          # int -> float in an operand
    "let f: float = 1; print_float(2);",                  # ... in a let and an argument
    "let l: long = 1; let s: long = l << 3;",             # shift count widens
    "let x: int = 1; { let x: float = 2.0; }",            # shadowing is legal
    "let b: bool = true & false;",
    "for (let i: int = 0; i < 2; i = i + 1) { if (i == 1) { continue; } }",
])
def test_accepted(body):
    chk(body)


def test_infinite_loop_needs_no_return():
    check(parse("fn f() -> int { while (true) { } } fn main() -> int { return 0; }", "<t>"))


def test_annotates_expression_types():
    prog = chk("let l: long = 2; let m: long = l + 1;")
    let_m = prog.decls[0].body.stmts[1]
    assert let_m.init.ty == "long"


def test_symbol_table_scoping():
    t = SymbolTable()
    assert t.declare(Symbol("x", "var", "int"))
    assert not t.declare(Symbol("x", "var", "int"))
    t.push()
    assert t.declare(Symbol("x", "var", "float"))
    assert t.resolve("x").ty == "float"
    t.pop()
    assert t.resolve("x").ty == "int" and t.resolve("y") is None
