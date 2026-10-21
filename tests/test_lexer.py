"""Unit tests for the lexer (M1a).  Owner: Member 1."""

import pytest

from src.frontend.lexer import LexError, TokKind, tokenize


def kinds(src):
    return [t.kind for t in tokenize(src)][:-1]   # drop EOF


def test_keywords_are_not_identifiers():
    assert kinds("fn let if else while for return") == [
        TokKind.KW_FN, TokKind.KW_LET, TokKind.KW_IF, TokKind.KW_ELSE,
        TokKind.KW_WHILE, TokKind.KW_FOR, TokKind.KW_RETURN,
    ]


def test_identifier_containing_a_keyword_stays_an_identifier():
    assert kinds("iffy format returned") == [TokKind.IDENT] * 3


def test_longest_match_wins():
    assert kinds("< <= << = == ! != - ->") == [
        TokKind.LT, TokKind.LE, TokKind.SHL, TokKind.ASSIGN, TokKind.EQ,
        TokKind.BANG, TokKind.NE, TokKind.MINUS, TokKind.ARROW,
    ]


def test_integer_and_float_literals():
    assert kinds("0 42 1_000 0xFF") == [TokKind.INT_LIT] * 4
    assert kinds("1.5 1e9 2.5e-3 1.0E+2") == [TokKind.FLOAT_LIT] * 4


def test_dot_not_followed_by_digit_is_not_part_of_a_number():
    # Guards against '1..2' silently lexing as a float.
    toks = tokenize("1")
    assert toks[0].kind is TokKind.INT_LIT and toks[0].text == "1"


def test_line_and_column_tracking():
    toks = tokenize("fn\n  main")
    assert (toks[0].line, toks[0].col) == (1, 1)
    assert (toks[1].line, toks[1].col) == (2, 3)


def test_comments_are_skipped():
    assert kinds("// gone\n/* also gone */ 1") == [TokKind.INT_LIT]


def test_unterminated_block_comment_is_an_error():
    with pytest.raises(LexError) as e:
        tokenize("/* never closed")
    assert "unterminated block comment" in e.value.message


def test_unexpected_character_reports_position():
    with pytest.raises(LexError) as e:
        tokenize("let a = 1 $ 2;")
    assert e.value.line == 1 and e.value.col == 11


def test_hex_literal_without_digits_is_an_error():
    with pytest.raises(LexError):
        tokenize("0x")


def test_empty_input_yields_only_eof():
    assert [t.kind for t in tokenize("")] == [TokKind.EOF]
