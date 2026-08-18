"""MiniLang recursive-descent parser.

Module M1b.  Owner: Member 1.
Status: WORKING for the whole grammar in docs/minilang-spec.md.
        Error recovery beyond statement-level resynchronisation is Week 5.

The statement grammar is LL(1) after left-factoring, so each statement rule
dispatches on a single lookahead token.  Expressions use precedence climbing
rather than one rule per precedence level: the grammar stays flat and adding a
level is a table edit, not a new function.
"""

from __future__ import annotations

from typing import List, Optional

from .ast_nodes import (
    Assign, Binary, Block, BoolLit, Break, Call, Continue, ExprStmt, FloatLit,
    FnDecl, For, GlobalDecl, If, Index, IntLit, Let, Param, Program, Return,
    Stmt, TypeNode, Unary, VarRef, While, Expr,
)
from .lexer import Lexer, TokKind, Token

# Binding power table: token -> (precedence, ast operator).
# Higher binds tighter.  All these operators are left-associative.
BINARY_PREC = {
    TokKind.OR_OR: (1, "||"),
    TokKind.AND_AND: (2, "&&"),
    TokKind.PIPE: (3, "|"),
    TokKind.CARET: (4, "^"),
    TokKind.AMP: (5, "&"),
    TokKind.EQ: (6, "=="),
    TokKind.NE: (6, "!="),
    TokKind.LT: (7, "<"),
    TokKind.LE: (7, "<="),
    TokKind.GT: (7, ">"),
    TokKind.GE: (7, ">="),
    TokKind.SHL: (8, "<<"),
    TokKind.SHR: (8, ">>"),
    TokKind.PLUS: (9, "+"),
    TokKind.MINUS: (9, "-"),
    TokKind.STAR: (10, "*"),
    TokKind.SLASH: (10, "/"),
    TokKind.PERCENT: (10, "%"),
}

TYPE_KEYWORDS = {
    TokKind.KW_INT: "int",
    TokKind.KW_LONG: "long",
    TokKind.KW_FLOAT: "float",
    TokKind.KW_BOOL: "bool",
    TokKind.KW_VOID: "void",
}


class ParseError(Exception):
    def __init__(self, message: str, tok: Token) -> None:
        super().__init__(f"{tok.line}:{tok.col}: syntax error: {message}")
        self.message = message
        self.line = tok.line
        self.col = tok.col


class Parser:
    def __init__(self, src: str, filename: str = "<input>") -> None:
        self.toks: List[Token] = list(Lexer(src, filename).tokens())
        self.pos = 0
        self.filename = filename

    # -- token helpers -----------------------------------------------------
    @property
    def cur(self) -> Token:
        return self.toks[self.pos]

    def _at(self, kind: TokKind) -> bool:
        return self.cur.kind is kind

    def _accept(self, kind: TokKind) -> Optional[Token]:
        if self._at(kind):
            tok = self.cur
            self.pos += 1
            return tok
        return None

    def _expect(self, kind: TokKind, what: str) -> Token:
        tok = self._accept(kind)
        if tok is None:
            raise ParseError(f"expected {what}, found {self.cur.text or 'end of file'!r}", self.cur)
        return tok

    # -- entry point -------------------------------------------------------
    def parse_program(self) -> Program:
        prog = Program()
        while not self._at(TokKind.EOF):
            if self._at(TokKind.KW_FN):
                prog.decls.append(self.parse_fn())
            elif self._at(TokKind.KW_GLOBAL):
                prog.decls.append(self.parse_global())
            else:
                raise ParseError("expected 'fn' or 'global' at top level", self.cur)
        return prog

    # -- declarations ------------------------------------------------------
    def parse_fn(self) -> FnDecl:
        kw = self._expect(TokKind.KW_FN, "'fn'")
        name = self._expect(TokKind.IDENT, "function name")
        self._expect(TokKind.LPAREN, "'('")
        params: List[Param] = []
        if not self._at(TokKind.RPAREN):
            while True:
                pname = self._expect(TokKind.IDENT, "parameter name")
                self._expect(TokKind.COLON, "':'")
                params.append(Param(pname.text, self.parse_type()))
                if not self._accept(TokKind.COMMA):
                    break
        self._expect(TokKind.RPAREN, "')'")
        ret = TypeNode("void")
        if self._accept(TokKind.ARROW):
            ret = self.parse_type()
        body = self.parse_block()
        return FnDecl(name.text, params, ret, body, kw.line, kw.col)

    def parse_global(self) -> GlobalDecl:
        kw = self._expect(TokKind.KW_GLOBAL, "'global'")
        name = self._expect(TokKind.IDENT, "global name")
        self._expect(TokKind.COLON, "':'")
        ty = self.parse_type()
        init = None
        if self._accept(TokKind.ASSIGN):
            init = self.parse_expr()
        self._expect(TokKind.SEMI, "';'")
        return GlobalDecl(name.text, ty, init, kw.line, kw.col)

    def parse_type(self) -> TypeNode:
        tok = self.cur
        if tok.kind not in TYPE_KEYWORDS:
            raise ParseError("expected a type name", tok)
        self.pos += 1
        name = TYPE_KEYWORDS[tok.kind]
        if self._accept(TokKind.LBRACKET):
            n = self._expect(TokKind.INT_LIT, "array length")
            self._expect(TokKind.RBRACKET, "']'")
            return TypeNode(name, int(n.text.replace("_", ""), 0))
        return TypeNode(name)

    # -- statements --------------------------------------------------------
    def parse_block(self) -> Block:
        lb = self._expect(TokKind.LBRACE, "'{'")
        blk = Block(line=lb.line, col=lb.col)
        while not self._at(TokKind.RBRACE):
            if self._at(TokKind.EOF):
                raise ParseError("unterminated block: expected '}'", self.cur)
            blk.stmts.append(self.parse_stmt())
        self._expect(TokKind.RBRACE, "'}'")
        return blk

    def parse_stmt(self) -> Stmt:
        tok = self.cur
        if self._at(TokKind.KW_LET):
            return self.parse_let()
        if self._at(TokKind.KW_IF):
            return self.parse_if()
        if self._at(TokKind.KW_WHILE):
            return self.parse_while()
        if self._at(TokKind.KW_FOR):
            return self.parse_for()
        if self._at(TokKind.LBRACE):
            return self.parse_block()
        if self._accept(TokKind.KW_BREAK):
            self._expect(TokKind.SEMI, "';'")
            return Break(line=tok.line, col=tok.col)
        if self._accept(TokKind.KW_CONTINUE):
            self._expect(TokKind.SEMI, "';'")
            return Continue(line=tok.line, col=tok.col)
        if self._accept(TokKind.KW_RETURN):
            value = None if self._at(TokKind.SEMI) else self.parse_expr()
            self._expect(TokKind.SEMI, "';'")
            return Return(value, line=tok.line, col=tok.col)
        stmt = self.parse_simple_stmt()
        self._expect(TokKind.SEMI, "';'")
        return stmt

    def parse_simple_stmt(self) -> Stmt:
        """An assignment or a bare expression, without the trailing ';'.

        Used both for ordinary statements and for the init/step clauses of a
        for-loop, which is why the semicolon is the caller's responsibility.
        """
        tok = self.cur
        if self._at(TokKind.KW_LET):
            return self.parse_let(consume_semi=False)
        expr = self.parse_expr()
        if self._accept(TokKind.ASSIGN):
            value = self.parse_expr()
            if not isinstance(expr, (VarRef, Index)):
                raise ParseError("left-hand side of assignment is not assignable", tok)
            return Assign(expr, value, line=tok.line, col=tok.col)
        return ExprStmt(expr, line=tok.line, col=tok.col)

    def parse_let(self, consume_semi: bool = True) -> Let:
        kw = self._expect(TokKind.KW_LET, "'let'")
        name = self._expect(TokKind.IDENT, "variable name")
        self._expect(TokKind.COLON, "':'")
        ty = self.parse_type()
        init = self.parse_expr() if self._accept(TokKind.ASSIGN) else None
        if consume_semi:
            self._expect(TokKind.SEMI, "';'")
        return Let(name.text, ty, init, line=kw.line, col=kw.col)

    def parse_if(self) -> If:
        kw = self._expect(TokKind.KW_IF, "'if'")
        self._expect(TokKind.LPAREN, "'('")
        cond = self.parse_expr()
        self._expect(TokKind.RPAREN, "')'")
        then_blk = self.parse_block()
        else_blk: Optional[Stmt] = None
        if self._accept(TokKind.KW_ELSE):
            # 'else if' chains without needing a dangling-else rule, because
            # every branch body is a braced block.
            else_blk = self.parse_if() if self._at(TokKind.KW_IF) else self.parse_block()
        return If(cond, then_blk, else_blk, line=kw.line, col=kw.col)

    def parse_while(self) -> While:
        kw = self._expect(TokKind.KW_WHILE, "'while'")
        self._expect(TokKind.LPAREN, "'('")
        cond = self.parse_expr()
        self._expect(TokKind.RPAREN, "')'")
        return While(cond, self.parse_block(), line=kw.line, col=kw.col)

    def parse_for(self) -> For:
        kw = self._expect(TokKind.KW_FOR, "'for'")
        self._expect(TokKind.LPAREN, "'('")
        init = None if self._at(TokKind.SEMI) else self.parse_simple_stmt()
        self._expect(TokKind.SEMI, "';'")
        cond = None if self._at(TokKind.SEMI) else self.parse_expr()
        self._expect(TokKind.SEMI, "';'")
        step = None if self._at(TokKind.RPAREN) else self.parse_simple_stmt()
        self._expect(TokKind.RPAREN, "')'")
        return For(init, cond, step, self.parse_block(), line=kw.line, col=kw.col)

    # -- expressions (precedence climbing) ---------------------------------
    def parse_expr(self, min_prec: int = 1) -> Expr:
        lhs = self.parse_unary()
        while True:
            entry = BINARY_PREC.get(self.cur.kind)
            if entry is None or entry[0] < min_prec:
                return lhs
            prec, op = entry
            tok = self.cur
            self.pos += 1
            rhs = self.parse_expr(prec + 1)   # left-associative
            lhs = Binary(op, lhs, rhs, line=tok.line, col=tok.col)

    def parse_unary(self) -> Expr:
        tok = self.cur
        for kind, op in ((TokKind.MINUS, "-"), (TokKind.BANG, "!"), (TokKind.TILDE, "~")):
            if self._accept(kind):
                return Unary(op, self.parse_unary(), line=tok.line, col=tok.col)
        return self.parse_postfix()

    def parse_postfix(self) -> Expr:
        expr = self.parse_primary()
        while self._at(TokKind.LBRACKET):
            tok = self._expect(TokKind.LBRACKET, "'['")
            idx = self.parse_expr()
            self._expect(TokKind.RBRACKET, "']'")
            expr = Index(expr, idx, line=tok.line, col=tok.col)
        return expr

    def parse_primary(self) -> Expr:
        tok = self.cur
        if self._accept(TokKind.LPAREN):
            expr = self.parse_expr()
            self._expect(TokKind.RPAREN, "')'")
            return expr
        if self._accept(TokKind.INT_LIT):
            return IntLit(int(tok.text.replace("_", ""), 0), line=tok.line, col=tok.col)
        if self._accept(TokKind.FLOAT_LIT):
            return FloatLit(float(tok.text.replace("_", "")), line=tok.line, col=tok.col)
        if self._accept(TokKind.KW_TRUE):
            return BoolLit(True, line=tok.line, col=tok.col)
        if self._accept(TokKind.KW_FALSE):
            return BoolLit(False, line=tok.line, col=tok.col)
        if self._accept(TokKind.IDENT):
            if self._accept(TokKind.LPAREN):
                args: List[Expr] = []
                if not self._at(TokKind.RPAREN):
                    while True:
                        args.append(self.parse_expr())
                        if not self._accept(TokKind.COMMA):
                            break
                self._expect(TokKind.RPAREN, "')'")
                return Call(tok.text, args, line=tok.line, col=tok.col)
            return VarRef(tok.text, line=tok.line, col=tok.col)
        raise ParseError(f"expected an expression, found {tok.text or 'end of file'!r}", tok)


def parse(src: str, filename: str = "<input>") -> Program:
    return Parser(src, filename).parse_program()
