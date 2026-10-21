"""MiniLang lexical analyser.

Module M1a.  Owner: Member 1.
Status: COMPLETE (Week 2-3).

Hand-written scanner: no generator, no regex engine driving the token loop.
Longest-match is obtained by ordering the operator table by descending length;
keywords are recognised by looking an identifier up in KEYWORDS after it has
been scanned, which keeps the identifier rule a single loop.

Every token carries a (line, column) position so that every downstream
diagnostic can point at real source coordinates.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from typing import Iterator, List


class TokKind(Enum):
    # literals and names
    IDENT = auto()
    INT_LIT = auto()
    FLOAT_LIT = auto()
    # keywords
    KW_FN = auto()
    KW_LET = auto()
    KW_GLOBAL = auto()
    KW_IF = auto()
    KW_ELSE = auto()
    KW_WHILE = auto()
    KW_FOR = auto()
    KW_BREAK = auto()
    KW_CONTINUE = auto()
    KW_RETURN = auto()
    KW_TRUE = auto()
    KW_FALSE = auto()
    KW_INT = auto()
    KW_LONG = auto()
    KW_FLOAT = auto()
    KW_BOOL = auto()
    KW_VOID = auto()
    # operators and punctuation
    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    PERCENT = auto()
    ASSIGN = auto()
    EQ = auto()
    NE = auto()
    LT = auto()
    LE = auto()
    GT = auto()
    GE = auto()
    AND_AND = auto()
    OR_OR = auto()
    BANG = auto()
    AMP = auto()
    PIPE = auto()
    CARET = auto()
    TILDE = auto()
    SHL = auto()
    SHR = auto()
    ARROW = auto()
    LPAREN = auto()
    RPAREN = auto()
    LBRACE = auto()
    RBRACE = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    COMMA = auto()
    SEMI = auto()
    COLON = auto()
    EOF = auto()


KEYWORDS = {
    "fn": TokKind.KW_FN,
    "let": TokKind.KW_LET,
    "global": TokKind.KW_GLOBAL,
    "if": TokKind.KW_IF,
    "else": TokKind.KW_ELSE,
    "while": TokKind.KW_WHILE,
    "for": TokKind.KW_FOR,
    "break": TokKind.KW_BREAK,
    "continue": TokKind.KW_CONTINUE,
    "return": TokKind.KW_RETURN,
    "true": TokKind.KW_TRUE,
    "false": TokKind.KW_FALSE,
    "int": TokKind.KW_INT,
    "long": TokKind.KW_LONG,
    "float": TokKind.KW_FLOAT,
    "bool": TokKind.KW_BOOL,
    "void": TokKind.KW_VOID,
}

# Ordered longest-first so that '<<' wins over '<' and '->' over '-'.
OPERATORS = [
    ("&&", TokKind.AND_AND),
    ("||", TokKind.OR_OR),
    ("==", TokKind.EQ),
    ("!=", TokKind.NE),
    ("<=", TokKind.LE),
    (">=", TokKind.GE),
    ("<<", TokKind.SHL),
    (">>", TokKind.SHR),
    ("->", TokKind.ARROW),
    ("+", TokKind.PLUS),
    ("-", TokKind.MINUS),
    ("*", TokKind.STAR),
    ("/", TokKind.SLASH),
    ("%", TokKind.PERCENT),
    ("=", TokKind.ASSIGN),
    ("<", TokKind.LT),
    (">", TokKind.GT),
    ("!", TokKind.BANG),
    ("&", TokKind.AMP),
    ("|", TokKind.PIPE),
    ("^", TokKind.CARET),
    ("~", TokKind.TILDE),
    ("(", TokKind.LPAREN),
    (")", TokKind.RPAREN),
    ("{", TokKind.LBRACE),
    ("}", TokKind.RBRACE),
    ("[", TokKind.LBRACKET),
    ("]", TokKind.RBRACKET),
    (",", TokKind.COMMA),
    (";", TokKind.SEMI),
    (":", TokKind.COLON),
]


@dataclass(frozen=True)
class Token:
    kind: TokKind
    text: str
    line: int
    col: int

    def __repr__(self) -> str:  # pragma: no cover - debug aid
        return f"{self.kind.name}({self.text!r})@{self.line}:{self.col}"


class LexError(Exception):
    """Raised on an unrecoverable lexical error, carrying source position."""

    def __init__(self, message: str, line: int, col: int) -> None:
        super().__init__(f"{line}:{col}: lexical error: {message}")
        self.message = message
        self.line = line
        self.col = col


class Lexer:
    def __init__(self, src: str, filename: str = "<input>") -> None:
        self.src = src
        self.filename = filename
        self.i = 0
        self.line = 1
        self.col = 1

    # -- character helpers -------------------------------------------------
    def _peek(self, k: int = 0) -> str:
        j = self.i + k
        return self.src[j] if j < len(self.src) else ""

    def _advance(self) -> str:
        ch = self.src[self.i]
        self.i += 1
        if ch == "\n":
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def _starts_with(self, s: str) -> bool:
        return self.src.startswith(s, self.i)

    @staticmethod
    def _is(ch: str, chars: str) -> bool:
        """Membership test that is False at end of input.

        Plain `ch in chars` is True when ch is the empty string, which sent
        every scanning loop past the end of the buffer.
        """
        return ch != "" and ch in chars

    # -- trivia ------------------------------------------------------------
    def _skip_trivia(self) -> None:
        while self.i < len(self.src):
            ch = self._peek()
            if self._is(ch, " \t\r\n"):
                self._advance()
            elif self._starts_with("//"):
                while self.i < len(self.src) and self._peek() != "\n":
                    self._advance()
            elif self._starts_with("/*"):
                start_line, start_col = self.line, self.col
                self._advance()
                self._advance()
                while True:
                    if self.i >= len(self.src):
                        raise LexError("unterminated block comment", start_line, start_col)
                    if self._starts_with("*/"):
                        self._advance()
                        self._advance()
                        break
                    self._advance()
            else:
                return

    # -- token rules -------------------------------------------------------
    def _lex_number(self) -> Token:
        line, col = self.line, self.col
        start = self.i
        if self._starts_with("0x") or self._starts_with("0X"):
            self._advance()
            self._advance()
            if not self._is(self._peek().lower(), "0123456789abcdef"):
                raise LexError("hexadecimal literal has no digits", line, col)
            while self._is(self._peek().lower(), "0123456789abcdef_"):
                self._advance()
            return Token(TokKind.INT_LIT, self.src[start:self.i], line, col)

        while self._peek().isdigit() or self._peek() == "_":
            self._advance()

        is_float = False
        # A '.' is part of the number only when followed by a digit, so that
        # '1..2' or a future member access does not silently become a float.
        if self._peek() == "." and self._peek(1).isdigit():
            is_float = True
            self._advance()
            while self._peek().isdigit() or self._peek() == "_":
                self._advance()
        if self._is(self._peek(), "eE") and (
            self._peek(1).isdigit()
            or (self._is(self._peek(1), "+-") and self._peek(2).isdigit())
        ):
            is_float = True
            self._advance()
            if self._is(self._peek(), "+-"):
                self._advance()
            while self._peek().isdigit():
                self._advance()

        kind = TokKind.FLOAT_LIT if is_float else TokKind.INT_LIT
        return Token(kind, self.src[start:self.i], line, col)

    def _lex_ident(self) -> Token:
        line, col = self.line, self.col
        start = self.i
        while self._peek().isalnum() or self._peek() == "_":
            self._advance()
        text = self.src[start:self.i]
        return Token(KEYWORDS.get(text, TokKind.IDENT), text, line, col)

    # -- driver ------------------------------------------------------------
    def tokens(self) -> Iterator[Token]:
        while True:
            self._skip_trivia()
            if self.i >= len(self.src):
                yield Token(TokKind.EOF, "", self.line, self.col)
                return

            ch = self._peek()
            if ch.isdigit():
                yield self._lex_number()
                continue
            if ch.isalpha() or ch == "_":
                yield self._lex_ident()
                continue

            for text, kind in OPERATORS:
                if self._starts_with(text):
                    line, col = self.line, self.col
                    for _ in text:
                        self._advance()
                    yield Token(kind, text, line, col)
                    break
            else:
                raise LexError(f"unexpected character {ch!r}", self.line, self.col)


def tokenize(src: str, filename: str = "<input>") -> List[Token]:
    """Convenience wrapper returning the full token list."""
    return list(Lexer(src, filename).tokens())
