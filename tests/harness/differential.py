"""Four-way differential test harness.

Module M8b.  Owner: Member 4.

Run one program through every execution path and require agreement:

  1. compile to CIR, verify
  2. execute in the CIR reference interpreter          -> oracle
  3. emit .ll,  compile with clang, run natively
  4. emit .wat, assemble and run under wasmtime
  5. emit .sbc, run under the reference VM
  6. compare (return value, stdout, trapped) four ways

Two comparison rules follow from what each target can observe:
  * a native process only reports its exit status, i.e. main's return value
    mod 256, so the LLVM value is compared mod 256;
  * once a program traps, its return value is meaningless and is ignored.

Objective O4: >= 98% four-way agreement over the 100-program corpus, with
every remaining disagreement documented in docs/divergence.md.

Usage:
    python -m tests.harness.differential                    # fixed corpus
    python -m tests.harness.differential --random 100       # + 100 generated
    python -m tests.harness.differential prog.mini --opt 1
"""
from __future__ import annotations

import argparse
import glob
import sys
from dataclasses import dataclass, field
from pathlib import Path

from src.cir.builder import build
from src.cir.verifier import verify
from src.frontend.parser import parse
from src.opt import run as run_opt
from src.sema.typecheck import check
from tests.harness.runners import TARGETS, Unavailable

ROOT = Path(__file__).parents[2]
CORPUS_GLOBS = ["tests/corpus/valid/*.mini", "tests/corpus/boundary/*.mini",
                "docs/examples/*.mini"]


@dataclass
class Outcome:
    name: str
    results: dict = field(default_factory=dict)     # target -> triple or error str
    skipped: list = field(default_factory=list)     # targets whose tool is missing
    error: str | None = None                        # front end / verifier failure

    @property
    def agrees(self) -> bool:
        if self.error: return False
        oracle = self.results.get("interp")
        return all(_same(t, r, oracle) for t, r in self.results.items())

    def disagreeing(self) -> list:
        oracle = self.results.get("interp")
        return [t for t, r in self.results.items() if not _same(t, r, oracle)]


def _same(target, got, want) -> bool:
    if isinstance(got, str) or isinstance(want, str): return False
    gv, gout, gtrap = got
    wv, wout, wtrap = want
    if (gout, gtrap) != (wout, wtrap): return False
    if gtrap: return True
    if target == "llvm": return gv & 0xFF == wv & 0xFF
    return gv == wv


def compile_source(src: str, name: str, opt: int = 0):
    program = parse(src, name)
    module = build(program, Path(name).stem)
    check(program)
    verify(module)
    return run_opt(module, opt)


def run_all(path=None, *, source=None, name=None, opt=0, targets=None) -> Outcome:
    """Run one program (a file path, or ``source`` text) on every target."""
    if source is None:
        source = Path(path).read_text()
        name = name or str(path)
    out = Outcome(name or "<source>")
    try:
        module = compile_source(source, out.name, opt)
    except Exception as e:                       # noqa: BLE001 - reported, not raised
        out.error = f"{type(e).__name__}: {e}"
        return out
    if opt:
        # Every target runs the same optimised CIR, so also check the
        # optimiser itself against the unoptimised program.
        out.results["interp.O0"] = TARGETS["interp"](compile_source(source, out.name, 0))
    for target in ["interp"] + [t for t in (targets or TARGETS) if t != "interp"]:
        try:
            out.results[target] = TARGETS[target](module)
        except Unavailable:
            out.skipped.append(target)
        except Exception as e:                   # noqa: BLE001
            out.results[target] = f"{type(e).__name__}: {str(e)[:500]}"
    return out


def corpus_paths():
    return sorted(p for g in CORPUS_GLOBS for p in glob.glob(str(ROOT / g)))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="differential")
    ap.add_argument("paths", nargs="*", help="programs to run (default: the fixed corpus)")
    ap.add_argument("--random", type=int, default=0, metavar="N",
                    help="also run N programs from gen_random")
    ap.add_argument("--seed", type=int, default=0, help="first random seed")
    ap.add_argument("--opt", type=int, default=0, choices=[0, 1])
    ap.add_argument("--targets", default=",".join(TARGETS))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    targets = args.targets.split(",")

    jobs = [(p, None) for p in (args.paths or corpus_paths())]
    if args.random:
        from tests.harness.gen_random import generate
        jobs += [(f"random/seed{s}", generate(s))
                 for s in range(args.seed, args.seed + args.random)]

    outcomes, skipped = [], set()
    for path, src in jobs:
        o = run_all(path, source=src, name=path, opt=args.opt, targets=targets)
        outcomes.append(o)
        skipped.update(o.skipped)
        rel = Path(path).relative_to(ROOT) if Path(path).is_absolute() else path
        if o.error:
            print(f"REJECT  {rel}: {o.error.splitlines()[0]}")
        elif not o.agrees:
            print(f"DIFF    {rel}: {', '.join(o.disagreeing())}")
            for t, r in o.results.items():
                print(f"          {t:<7} {r!r}")
        elif args.verbose:
            print(f"ok      {rel}")

    ran = [o for o in outcomes if not o.error]
    agreed = sum(o.agrees for o in ran)
    pct = 100.0 * agreed / len(ran) if ran else 0.0
    used = ["interp"] + [t for t in targets if t != "interp" and t not in skipped]
    print(f"\n{agreed}/{len(ran)} programs agree on {', '.join(used)} ({pct:.1f}%)")
    rejected = len(outcomes) - len(ran)
    if rejected: print(f"{rejected} program(s) rejected by the front end (not counted)")
    if skipped: print(f"skipped (tool not installed): {', '.join(sorted(skipped))}")
    return 0 if agreed == len(ran) else 1


if __name__ == "__main__":
    sys.exit(main())
