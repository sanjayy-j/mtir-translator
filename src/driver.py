"""Command-line driver for the Multi-Target IR Translator.

Module M9.  Owner: Member 1.

Usage
-----
    python -m src.driver --emit=tokens docs/examples/abs.mini
    python -m src.driver --emit=ast    docs/examples/abs.mini
    python -m src.driver --emit=cir    docs/examples/abs.mini
    python -m src.driver --emit=ll     docs/examples/abs.mini
    python -m src.driver --emit=ll     docs/examples/abs.cir   (.cir input)
    python -m src.driver --opt=1 --emit=cir docs/examples/abs.mini
    python -m src.driver --demo-cir                             (hand-built CIR)
    python -m src.driver --demo-stack                           (reg-to-stack)

Stages that are not yet implemented exit with status 3 and a message naming
the module, the owner and the week they are scheduled for, so that the state
of the project is visible from the tool itself rather than only from the plan.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .cir.builder import BuildError, build
from .cir.parser import CirParseError, parse_cir
from .frontend import ast_nodes, parser as mini_parser
from .frontend.lexer import LexError, tokenize
from .frontend.parser import ParseError

EMIT_CHOICES = ["tokens", "ast", "cir", "ll", "wat", "sbc"]


def _demo_cir_module():
    """The abs() module of Figure 2, built by hand.

    It is the fixture for tests/test_cir_printer.py and the golden module
    behind docs/examples/abs.cir.  Now that the builder (M3a) exists,
    --emit=cir on docs/examples/abs.mini produces the same module up to the
    choice of block labels; tests/test_cir_builder.py pins that agreement.
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


def _to_cir(src: str, path: Path, opt_level: int):
    """The CIR module for a source file, optimised to the requested level.

    A ``.cir`` file is read straight through the textual parser rather than
    the front end, which is the contract docs/cir-spec.md section 4 describes:
    a back end can be driven from a checked-in .cir file with no MiniLang
    involved at all.
    """
    from .opt import run as run_opt

    if path.suffix == ".cir":
        module = parse_cir(src, path.stem)
    else:
        module = build(mini_parser.parse(src, str(path)), path.stem)
    return run_opt(module, opt_level)


def _not_yet(stage: str, module: str, owner: str, week: str) -> int:
    print(
        f"error: --emit={stage} is not implemented yet.\n"
        f"       {module} is owned by {owner} and is scheduled for {week}.\n"
        f"       Implemented today: --emit=tokens, --emit=ast, --emit=cir, "
        f"--emit=ll, --demo-cir, --demo-stack.",
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
                    help="optimisation level: 0 raw, 1 constant folding + "
                         "copy propagation + dead-code elimination")
    ap.add_argument("--run", action="store_true",
                    help="execute the emitted target (Week 9)")
    ap.add_argument("--demo-cir", action="store_true",
                    help="print the hand-built abs() CIR module")
    ap.add_argument("--demo-stack", action="store_true",
                    help="lower the hand-built abs() module to stack form and "
                         "report the naive vs peepholed instruction counts")
    args = ap.parse_args(argv)

    if args.demo_cir:
        from .cir.printer import print_module
        sys.stdout.write(print_module(_demo_cir_module()))
        return 0

    if args.demo_stack:
        from .backend.wasm.reg2stack import count_function, lower_function
        fn = _demo_cir_module().function("abs")
        for label, seq in lower_function(fn).items():
            print(f"{label}:")
            for op in seq:
                print(f"    {op}")
        naive, tuned = count_function(fn)
        saved = 100.0 * (naive - tuned) / naive
        print(f"\nnaive lowering:     {naive} stack instructions")
        print(f"after peephole:     {tuned} stack instructions")
        print(f"reduction:          {saved:.1f}%")
        return 0

    if not args.source:
        ap.error("a source file is required unless --demo-cir or "
                 "--demo-stack is given")

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

        if args.emit in ("cir", "ll"):
            module = _to_cir(src, path, args.opt)
            if args.emit == "cir":
                from .cir.printer import print_module
                sys.stdout.write(print_module(module))
            else:
                from .backend.llvm.emit_ll import emit as emit_ll
                sys.stdout.write(emit_ll(module))
            return 0

    except LexError as e:
        print(f"{path}:{e}", file=sys.stderr)
        return 1
    except ParseError as e:
        print(f"{path}:{e}", file=sys.stderr)
        return 1
    except (BuildError, CirParseError) as e:
        print(f"{path}:{e}", file=sys.stderr)
        return 1

    pending = {
        "wat": ("WebAssembly back end (M6c)", "Member 4", "Weeks 7-8"),
        "sbc": ("stack bytecode back end (M7)", "Member 4", "Week 9"),
    }
    return _not_yet(args.emit, *pending[args.emit])


if __name__ == "__main__":
    raise SystemExit(main())
