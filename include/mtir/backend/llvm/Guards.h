// Guards.h -- trap guards that realise CIR semantics on LLVM.
//
// LLVM leaves undefined what CIR defines, so each function here emits the
// code that closes one row of docs/divergence.md.  This is the asymmetry the
// project turns on: WebAssembly traps natively, so almost all the guard code
// lands on the LLVM side.  Without it the same CIR would abort in a browser
// and run on to a garbage result natively -- exactly the bug the project
// exists to prevent.
//
//   rows 1-2  sdiv/srem/udiv/urem by zero, and INT_MIN / -1
//             -> compare and branch to a trap block
//   row 3     shl/ashr/lshr with a count >= the width
//             -> mask the count to width-1
//   row 6     fptosi out of range or NaN
//             -> range and NaN checks before the cast
//   row 7     array index out of bounds
//             -> unsigned compare against the extent
#ifndef MTIR_BACKEND_LLVM_GUARDS_H
#define MTIR_BACKEND_LLVM_GUARDS_H

#include <string>
#include <string_view>

#include "mtir/cir/Opcode.h"
#include "mtir/cir/Value.h"

namespace mtir::backend::llvm {

/// What a guard needs from the emitter.  An abstract interface rather than
/// the Python prototype's duck-typed protocol, so a guard cannot silently
/// depend on some other emitter detail.
class Emitter {
public:
  virtual ~Emitter() = default;

  /// A fresh temporary name, e.g. "%g.7".
  virtual std::string tmp() = 0;

  /// Emit one instruction line into the current block.
  virtual void line(std::string_view text) = 0;

  /// Branch to a fresh trap block when `cond` holds, then open the
  /// continuation block so the caller can keep emitting.
  virtual void trapBranch(std::string_view cond, std::string_view kind) = 0;
};

/// Guard an sdiv/udiv/srem/urem.  A constant divisor is settled at compile
/// time: any constant other than 0 or -1 provably cannot trap and needs no
/// run-time test at all.  0 and -1 keep the full guard rather than being
/// special-cased into an unconditional trap, so there is one code path to
/// reason about.
void guardDivision(Emitter &em, cir::Opcode op, cir::Ty ty, std::string_view lhs,
                   const cir::Value &rhs, std::string_view rhsText);

/// The LLVM operand to shift by, with the count reduced modulo the width.
/// A constant count is masked here so the emitted IR stays readable; a
/// dynamic count costs one `and`, which is what a C compiler emits for the
/// same reason.
std::string maskShiftCount(Emitter &em, cir::Ty ty, const cir::Value &count,
                           std::string_view countText);

/// Guard an fptosi.  Truncation toward zero is defined for exactly those
/// doubles strictly inside (INT_MIN-1, INT_MAX+1); anything else, NaN
/// included, is undefined in LLVM and a trap in CIR.  Both bounds are exact
/// doubles, so the comparison is not itself a rounding question.
void guardFPToSI(Emitter &em, cir::Ty ty, std::string_view valueText);

/// Guard an array index.  An *unsigned* comparison against the extent rejects
/// a negative index in the same instruction, because a negative i32 is a very
/// large u32.
void guardBounds(Emitter &em, std::int64_t extent, const cir::Value &index,
                 std::string_view indexText);

} // namespace mtir::backend::llvm

#endif // MTIR_BACKEND_LLVM_GUARDS_H
