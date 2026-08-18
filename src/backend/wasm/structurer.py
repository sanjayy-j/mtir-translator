"""CFG -> nested block/loop/if regions.

Module M6b.  Owner: Member 4.
Status: PLANNED -- Week 8.
"""
"""WebAssembly has no branch to an arbitrary label: only relative branches
out of enclosing block/loop/if constructs.  A general CFG must therefore be
re-expressed as nested regions.

Two-stage plan, per risk R1 in the Review 1 report:
  Stage 1 (fallback, always correct): emit structured control flow directly
    from the AST.  MiniLang has no goto, so every unoptimised CFG derives from
    structured statements and is reducible.  No structuring pass needed.
  Stage 2 (Week 8): Relooper/Stackifier-style structural analysis over a
    general reducible CFG, computing relative branch depths.  Needed only once
    optimisation passes start merging and threading blocks.

Irreducible CFGs cannot arise from MiniLang and are out of scope.
"""


def structure(function):
    raise NotImplementedError(
        "CFG structuring is scheduled for Week 8 (M6b, Member 4)")
