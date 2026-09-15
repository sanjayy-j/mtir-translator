"""CIR textual parser (.cir -> Module).

Module M3c.  Owner: Member 3.
Status: COMPLETE for every form printer.py can emit.

Round-trip property (docs/cir-spec.md section 4):

    print_module(parse_cir(t)) == t     for every t the printer produces

This is what lets a back end be developed against a checked-in .cir file
without the front end being finished -- the textual format, not the Python
object graph, is the contract between the middle end and the back ends.

Recovering types
----------------
The textual form carries one type per instruction and none on a register use,
so the parser rebuilds every register's type from its definition:

  * parameters are typed by the function header;
  * ``%d = <op> <ty> ...`` defines ``%d`` with ``result_ty(op, ty)``, which is
    where comparisons become ``i1`` and ``alloca``/``gep`` become ``ptr``;
  * a use is then looked up in that table.

Definitions are collected over the whole function before uses are resolved, so
a register defined in a loop body may legitimately be used by a block that
appears earlier in the text.

Known limit, deliberately not worked around: an integer literal takes the
instruction's printed type, so ``call i32 @f(3)`` types the argument ``3`` as
``i32`` even if ``@f`` takes an ``i64``.  The printed text is identical either
way, so the round-trip property above still holds, and the back ends recover
argument types from the callee's signature, which they have.
"""

from __future__ import annotations

import re
from typing import List, Optional, Tuple

from .ir import (ALL_OPS, BasicBlock, ConstFloat, ConstInt, Function, Global,
                 GlobalRef, Instr, Module, Param, Reg, Ty, UNARY_OPS,
                 result_ty)


class CirParseError(Exception):
    """A malformed .cir file, reported against the line it was found on."""

    def __init__(self, message: str, lineno: int) -> None:
        super().__init__(f"{lineno}: cir parse error: {message}")
        self.message = message
        self.lineno = lineno


_TY = {t.value: t for t in Ty}

_FUNC_RE = re.compile(r"^func\s+@([A-Za-z_][\w.]*)\s*\((.*?)\)\s*->\s*(\w+)\s*\{$")
_GLOBAL_RE = re.compile(
    r"^global\s+@([A-Za-z_][\w.]*)\s*:\s*(\w+)(?:\[(\d+)\])?\s*(?:=\s*(.+))?$")
_LABEL_RE = re.compile(r"^([A-Za-z_][\w.]*):$")
_PARAM_RE = re.compile(r"^(\w+)\s+%([A-Za-z_][\w.]*)$")
_NUM_RE = re.compile(r"^[+-]?(\d+\.\d*|\.\d+|\d+)([eE][+-]?\d+)?$")
_COND_RE = re.compile(r"^(.+?)\s*\?\s*([\w.]+)\s*:\s*([\w.]+)$")
_CALL_RE = re.compile(r"^(\w+)\s+@([A-Za-z_][\w.]*)\s*\((.*)\)$")


def _ty(name: str, lineno: int) -> Ty:
    if name not in _TY:
        raise CirParseError(f"unknown type {name!r}", lineno)
    return _TY[name]


def _split_args(text: str) -> List[str]:
    text = text.strip()
    return [a.strip() for a in text.split(",") if a.strip()] if text else []


class _FnParser:
    """Parses one function body in two passes, so a use may precede its def."""

    def __init__(self, header: Tuple[str, str, str], lineno: int) -> None:
        name, params, ret = header
        self.name = name
        self.ret_ty = _ty(ret, lineno)
        self.params: List[Param] = []
        self.types: dict[str, Ty] = {}
        for raw in _split_args(params):
            m = _PARAM_RE.match(raw)
            if not m:
                raise CirParseError(f"malformed parameter {raw!r}", lineno)
            ty = _ty(m.group(1), lineno)
            self.params.append(Param(m.group(2), ty))
            self.types[m.group(2)] = ty
        self.lines: List[Tuple[int, str]] = []

    # -- shared between both passes ---------------------------------------
    def _dissect(self, text: str, lineno: int) -> Tuple[Optional[str], str, str]:
        """(dest name or None, opcode, remainder) for one instruction line."""
        dest = None
        if text.startswith("%"):
            lhs, sep, rhs = text.partition("=")
            if not sep:
                raise CirParseError(f"expected an '=' in {text!r}", lineno)
            dest = lhs.strip()[1:]
            text = rhs.strip()
        op, _, rest = text.partition(" ")
        rest = rest.strip()
        # The printer spells both terminators 'br' and distinguishes them by
        # the '? then : else' suffix, so the opcode is recovered here rather
        # than by changing the syntax.
        if op == "br" and "?" in rest:
            op = "br.cond"
        if op not in ALL_OPS:
            raise CirParseError(f"unknown opcode {op!r}", lineno)
        return dest, op, rest

    def _printed_ty(self, op: str, rest: str, lineno: int) -> Ty:
        """The type an instruction prints, or the one implied when it prints none."""
        if op in ("br", "trap"):
            return Ty.VOID
        if op == "br.cond":
            return Ty.I1
        if op == "print.i32":
            return Ty.I32
        if op == "print.f64":
            return Ty.F64
        head = rest.split(" ", 1)[0].split(",", 1)[0]
        if not head:
            raise CirParseError(f"instruction {op!r} is missing its type", lineno)
        return _ty(head, lineno)

    # -- pass 1: record what each line defines -----------------------------
    def collect(self, text: str, lineno: int) -> None:
        self.lines.append((lineno, text))
        dest, op, rest = self._dissect(text, lineno)
        if dest is not None:
            self.types[dest] = result_ty(op, self._printed_ty(op, rest, lineno))

    # -- pass 2: resolve operands and build instructions -------------------
    def value(self, text: str, ty: Ty, lineno: int):
        text = text.strip()
        if text.startswith("%"):
            name = text[1:]
            if name not in self.types:
                raise CirParseError(f"%{name} is used but never defined", lineno)
            return Reg(name, self.types[name])
        if text.startswith("@"):
            return GlobalRef(text[1:])
        if not _NUM_RE.match(text):
            raise CirParseError(f"not a value: {text!r}", lineno)
        if any(c in text for c in ".eE"):
            return ConstFloat(float(text))
        return ConstInt(int(text), ty if ty in (Ty.I1, Ty.I32, Ty.I64) else Ty.I32)

    def instr(self, text: str, lineno: int) -> Instr:
        dest_name, op, rest = self._dissect(text, lineno)
        ty = self._printed_ty(op, rest, lineno)
        dest = Reg(dest_name, self.types[dest_name]) if dest_name else None

        if op == "br":
            if not rest:
                raise CirParseError("br is missing its target label", lineno)
            return Instr("br", Ty.VOID, None, [], labels=[rest])

        if op == "br.cond":
            m = _COND_RE.match(rest)
            if not m:
                raise CirParseError(
                    "expected 'br <cond> ? <then> : <else>'", lineno)
            return Instr("br.cond", Ty.I1, None,
                         [self.value(m.group(1), Ty.I1, lineno)],
                         labels=[m.group(2), m.group(3)])

        if op == "trap":
            return Instr("trap")

        if op == "ret":
            if ty is Ty.VOID:
                return Instr("ret", Ty.VOID, None, [])
            operand = rest[len(ty.value):].strip()
            if not operand:
                raise CirParseError("ret is missing its value", lineno)
            return Instr("ret", ty, None, [self.value(operand, ty, lineno)])

        if op == "call":
            m = _CALL_RE.match(rest)
            if not m:
                raise CirParseError("expected 'call <ty> @callee(args)'", lineno)
            args = [self.value(a, Ty.I32, lineno) for a in _split_args(m.group(3))]
            return Instr("call", ty, dest, args, callee=m.group(2))

        if op in ("print.i32", "print.f64"):
            return Instr(op, ty, None, [self.value(rest, ty, lineno)])

        if op == "alloca":
            parts = _split_args(rest[len(ty.value):])
            count = [self.value(parts[0], Ty.I32, lineno)] if parts else []
            return Instr("alloca", ty, dest, count)

        # Everything else prints as:  [%dest =] op <ty> [arg {, arg}]
        args = [self.value(a, ty, lineno)
                for a in _split_args(rest[len(ty.value):])]
        if op in UNARY_OPS and len(args) != 1:
            raise CirParseError(
                f"{op} takes exactly one operand, found {len(args)}", lineno)
        return Instr(op, ty, dest, args)

    def finish(self) -> Function:
        blocks: List[BasicBlock] = []
        for lineno, text in self.lines:
            m = _LABEL_RE.match(text)
            if m:
                blocks.append(BasicBlock(m.group(1)))
                continue
            if not blocks:
                raise CirParseError(
                    "instruction before the first block label", lineno)
            blocks[-1].instrs.append(self.instr(text, lineno))
        return Function(self.name, self.params, self.ret_ty, blocks)


def parse_cir(text: str, name: str = "module") -> Module:
    """Parse textual CIR into a Module.  Raises CirParseError on bad input."""
    mod = Module(name)
    fn: Optional[_FnParser] = None
    lineno = 0

    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue

        if fn is not None:
            if line == "}":
                mod.functions.append(fn.finish())
                fn = None
            elif _LABEL_RE.match(line):
                fn.lines.append((lineno, line))
            else:
                fn.collect(line, lineno)
            continue

        m = _FUNC_RE.match(line)
        if m:
            fn = _FnParser(m.groups(), lineno)
            continue

        m = _GLOBAL_RE.match(line)
        if m:
            gname, gty, glen, ginit = m.groups()
            ty = _ty(gty, lineno)
            init = None
            if ginit is not None:
                init = (ConstFloat(float(ginit)) if ty is Ty.F64
                        else ConstInt(int(ginit), ty))
            mod.globals.append(Global(gname, ty, init, int(glen) if glen else None))
            continue

        raise CirParseError(f"expected 'func' or 'global', found {line!r}", lineno)

    if fn is not None:
        raise CirParseError("unterminated function: expected a '}'", lineno)
    return mod
