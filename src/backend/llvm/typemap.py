"""CIR type -> LLVM type mapping.

Module M5.  Owner: Member 3.
Status: COMPLETE.

Every CIR type has an exact LLVM counterpart, which is the pay-off of the
"typed on every instruction" design decision: the back end never infers and
never widens.  Two rows are worth pointing at in the viva:

  * ``i1`` maps to LLVM's native ``i1``.  WebAssembly has no 1-bit type and
    must normalise to ``i32`` (docs/divergence.md row 5), so this is one of
    the places the two back ends genuinely differ rather than differing only
    in syntax.
  * ``ptr`` maps to LLVM's opaque pointer.  The checked-in
    docs/examples/abs.ll already uses it, so LLVM 15+ is assumed.
"""

from __future__ import annotations

from ...cir.ir import Ty

CIR_TO_LLVM = {
    Ty.I1: "i1",
    Ty.I32: "i32",
    Ty.I64: "i64",
    Ty.F64: "double",
    Ty.PTR: "ptr",
    Ty.VOID: "void",
}


def llvm_ty(ty: Ty) -> str:
    """The LLVM spelling of a CIR type."""
    try:
        return CIR_TO_LLVM[ty]
    except KeyError:                                     # pragma: no cover
        raise KeyError(f"no LLVM type for CIR type {ty!r}") from None
