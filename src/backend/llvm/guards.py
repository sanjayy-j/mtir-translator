"""Trap guards realising the CIR semantics on LLVM.

Module M5.  Owner: Member 3.
Status: IMPLEMENTED for rows 1, 2, 3 and 6 of docs/divergence.md; row 7
        (array bounds) is here too, driven by the extents emit_ll.py recovers
        from each ``alloca``.

LLVM leaves undefined what CIR defines, so each function here emits the code
that closes one row of the divergence table.  This is the asymmetry the report
turns on: WebAssembly traps natively, so almost all the guard code lands on
the LLVM side.  Without it, the same CIR would abort in a browser and run on
to a garbage result natively -- which is exactly the bug the project exists to
prevent.

  row 1  sdiv/srem/udiv/urem by zero  -> compare + branch to a trap block
  row 2  INT_MIN / -1                 -> second condition on the same branch
  row 3  shl/ashr/lshr count >= width -> mask the count to width-1
  row 6  fptosi out of range or NaN   -> range and NaN check before the cast
  row 7  array index out of bounds    -> unsigned compare against the extent

Emitter protocol
----------------
Each function takes the emit_ll.Emitter as its first argument and uses four
methods of it -- ``tmp()``, ``label()``, ``line()`` and ``trap_branch()`` --
so that the guards stay independent of how the emitter buffers its output.
"""

from __future__ import annotations

from ...cir.ir import ConstInt, INT_WIDTH, Ty, wrap_int
from .typemap import llvm_ty

# The most negative value of each signed integer type, whose division by -1
# overflows and is therefore undefined in LLVM (docs/divergence.md row 2).
INT_MIN = {ty: -(1 << (bits - 1)) for ty, bits in INT_WIDTH.items()}


def guard_div(em, op: str, ty: Ty, lhs: str, rhs, rhs_text: str) -> None:
    """Emit the divide/remainder guard in front of an sdiv/udiv/srem/urem.

    ``rhs`` is the CIR operand, so a constant divisor can be settled at
    compile time: any constant other than 0 or -1 provably cannot trap and
    needs no run-time test at all.  0 and -1 keep the full guard rather than
    being special-cased into an unconditional trap, because that keeps one
    code path to reason about.
    """
    if isinstance(rhs, ConstInt) and wrap_int(rhs.value, ty) not in (0, -1):
        return

    lty = llvm_ty(ty)
    signed = op in ("sdiv", "srem")

    zero = em.tmp()
    em.line(f"{zero} = icmp eq {lty} {rhs_text}, 0")
    bad = zero

    if signed:
        min_lhs = em.tmp()
        minus_one = em.tmp()
        overflow = em.tmp()
        combined = em.tmp()
        em.line(f"{min_lhs} = icmp eq {lty} {lhs}, {INT_MIN[ty]}")
        em.line(f"{minus_one} = icmp eq {lty} {rhs_text}, -1")
        em.line(f"{overflow} = and i1 {min_lhs}, {minus_one}")
        em.line(f"{combined} = or i1 {zero}, {overflow}")
        bad = combined

    em.trap_branch(bad, "div")


def mask_shift_count(em, ty: Ty, count, count_text: str) -> str:
    """Return the LLVM operand to shift by, with the count reduced mod width.

    CIR defines ``x << 33`` as ``x << 1`` (docs/divergence.md row 3) whereas
    LLVM makes an over-wide shift poison.  A constant count is masked here so
    the emitted IR stays readable; a dynamic count costs one ``and``, which is
    also exactly what a C compiler emits for the same reason.
    """
    mask = INT_WIDTH[ty] - 1
    if isinstance(count, ConstInt):
        return str(wrap_int(count.value & mask, ty))
    masked = em.tmp()
    em.line(f"{masked} = and {llvm_ty(ty)} {count_text}, {mask}")
    return masked


def guard_fptosi(em, ty: Ty, value_text: str) -> None:
    """Emit the NaN and range check in front of an fptosi.

    Truncation toward zero is defined for exactly those doubles strictly
    inside (INT_MIN-1, INT_MAX+1); anything else, NaN included, is undefined
    in LLVM and a trap in CIR (docs/divergence.md row 6).  Both bounds are
    powers of two plus one, so both are exact doubles and the comparison is
    not itself a rounding question.
    """
    bits = INT_WIDTH[ty]
    low = -(1 << (bits - 1)) - 1          # one below INT_MIN
    high = 1 << (bits - 1)                # one above INT_MAX

    is_nan = em.tmp()
    too_low = em.tmp()
    too_high = em.tmp()
    out_of_range = em.tmp()
    bad = em.tmp()
    em.line(f"{is_nan} = fcmp uno double {value_text}, {value_text}")
    em.line(f"{too_low} = fcmp ole double {value_text}, {float(low)}")
    em.line(f"{too_high} = fcmp oge double {value_text}, {float(high)}")
    em.line(f"{out_of_range} = or i1 {too_low}, {too_high}")
    em.line(f"{bad} = or i1 {is_nan}, {out_of_range}")
    em.trap_branch(bad, "fptosi")


def guard_bounds(em, extent: int, index, index_text: str) -> None:
    """Emit the array bounds check in front of a gep.

    An *unsigned* comparison against the extent rejects a negative index in
    the same instruction, because a negative i32 is a very large u32.  CIR
    traps on an out-of-bounds index (docs/divergence.md row 7); LLVM would
    happily compute the address and read whatever is there.
    """
    if isinstance(index, ConstInt) and 0 <= index.value < extent:
        return                                  # provably in bounds
    bad = em.tmp()
    em.line(f"{bad} = icmp uge i32 {index_text}, {extent}")
    em.trap_branch(bad, "bounds")
