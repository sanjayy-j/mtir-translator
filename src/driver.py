"""Command-line driver for the Multi-Target IR Translator.

Module M9.  Owner: Member 1.

Usage
-----
    python -m src.driver --emit=tokens docs/examples/abs.mini
    python -m src.driver --emit=ast    docs/examples/abs.mini
    python -m src.driver --emit=cir    docs/examples/abs.mini   (Week 5)
    python -m src.driver --demo-cir                             (hand-built CIR)

Stages that are not yet implemented exit with status 3 and a message naming
the module, the owner and the week they are scheduled for, so that the state
of the project is visible from the tool itself rather than only from the plan.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .frontend import ast_nodes, parser as mini_parser
from .frontend.lexer import LexError, tokenize
from .frontend.parser import ParseError

EMIT_CHOICES = ["tokens", "ast", "cir", "ll", "wat", "sbc"]


def _demo_cir_module():
    """The abs() module of Figure 2, built by hand.

    Until the builder (M3a) lands in Week 5 this is how the CIR data
    structures and printer are demonstrated end to end.  It is also the
    fixture for tests/test_cir_printer.py.
    """
    from .cir.ir import BasicBlock, ConstInt, Function, Instr, Module, Param, Reg, Ty

    x = Reg("x", Ty.I32)
    t0 = Reg("t0", Ty.I1)
    t1 = Reg("t1", Ty.I32)

    entry = BasicBlock("entry", [
        Instr("icmp.slt", Ty.I32, t0, [x, ConstInt(0)]),
        Instr("br.cond", Ty.I1, None, [t0], labels=["then", "exit"]),
    ])
    then = BasicBlock("then", [
        Instr("sub", Ty.I32, t1, [ConstInt(0), x]),
        Instr("ret", Ty.I32, None, [t1]),
    ])
    exit_ = BasicBlock("exit", [
        Instr("ret", Ty.I32, None, [x]),
    ])

    fn = Function("abs", [Param("x", Ty.I32)], Ty.I32, [entry, then, exit_])
    return Module("abs", [], [fn])


def _not_yet(stage: str, module: str, owner: str, week: str) -> int:
    print(
        f"error: --emit={stage} is not implemented yet.\n"
        f"       {module} is owned by {owner} and is scheduled for {week}.\n"
        f"       Implemented today: --emit=tokens, --emit=ast, --demo-cir.",
        file=sys.stderr,
    )
    return 3


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="mtir", description="Multi-Target Intermediate Representation Translator")
    ap.add_argument("source", nargs="?", help="MiniLang source file (.mini)")
    ap.add_argument("--emit", choices=EMIT_CHOICES, default="ast",
                    help="which stage of the pipeline to print")
    ap.add_argument("--opt", type=int, default=0, choices=[0, 1],
                    help="optimisation level (Week 10)")
    ap.add_argument("--run", action="store_true",
                    help="execute the emitted target (Week 9)")
    ap.add_argument("--demo-cir", action="store_true",
                    help="print the hand-built abs() CIR module")
    args = ap.parse_args(argv)

    if args.demo_cir:
        from .cir.printer import print_module
        sys.stdout.write(print_module(_demo_cir_module()))
        return 0

    if not args.source:
        ap.error("a source file is required unless --demo-cir is given")

    path = Path(args.source)
    if not path.exists():
        print(f"error: no such file: {path}", file=sys.stderr)
        return 2
    src = path.read_text()

    try:
        if args.emit == "tokens":
            for tok in tokenize(src, str(path)):
                print(f"{tok.line:>4}:{tok.col:<4} {tok.kind.name:<12} {tok.text!r}")
            return 0

        if args.emit == "ast":
            program = mini_parser.parse(src, str(path))
            print(ast_nodes.dump(program))
            return 0

    except LexError as e:
        print(f"{path}:{e}", file=sys.stderr)
        return 1
    except ParseError as e:
        print(f"{path}:{e}", file=sys.stderr)
        return 1

    pending = {
        "cir": ("CIR builder (M3a)", "Member 3", "Week 5"),
        "ll": ("LLVM back end (M5)", "Member 3", "Weeks 6-7"),
        "wat": ("WebAssembly back end (M6c)", "Member 4", "Weeks 7-8"),
        "sbc": ("stack bytecode back end (M7)", "Member 4", "Week 9"),
    }
    return _not_yet(args.emit, *pending[args.emit])


if __name__ == "__main__":
    raise SystemExit(main())
