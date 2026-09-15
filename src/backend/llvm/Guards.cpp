#include "mtir/backend/llvm/Guards.h"

#include <cstdint>
#include <string>

#include "mtir/backend/llvm/TypeMap.h"
#include "mtir/cir/Arith.h"
#include "mtir/cir/Printer.h"

namespace mtir::backend::llvm {
namespace {

using cir::ConstInt;
using cir::Opcode;
using cir::Ty;
using cir::Value;

std::string ty(Ty t) { return std::string(llvmType(t)); }

bool isSignedDivision(Opcode op) {
  return op == Opcode::SDiv || op == Opcode::SRem;
}

} // namespace

void guardDivision(Emitter &em, Opcode op, Ty t, std::string_view lhs,
                   const Value &rhs, std::string_view rhsText) {
  const bool isSigned = isSignedDivision(op);

  if (const ConstInt *c = cir::asConstInt(rhs)) {
    const std::int64_t divisor = cir::wrapInt(c->value, t);
    if (divisor != 0 && divisor != -1)
      return; // provably cannot trap
  }

  const std::string lty = ty(t);
  const std::string zero = em.tmp();
  em.line(zero + " = icmp eq " + lty + " " + std::string(rhsText) + ", 0");
  std::string bad = zero;

  if (isSigned) {
    // row 2: INT_MIN / -1 overflows, which LLVM also leaves undefined.
    const std::string minLhs = em.tmp();
    const std::string minusOne = em.tmp();
    const std::string overflow = em.tmp();
    const std::string combined = em.tmp();
    em.line(minLhs + " = icmp eq " + lty + " " + std::string(lhs) + ", " +
            std::to_string(cir::intMin(t)));
    em.line(minusOne + " = icmp eq " + lty + " " + std::string(rhsText) + ", -1");
    em.line(overflow + " = and i1 " + minLhs + ", " + minusOne);
    em.line(combined + " = or i1 " + zero + ", " + overflow);
    bad = combined;
  }

  em.trapBranch(bad, "div");
}

std::string maskShiftCount(Emitter &em, Ty t, const Value &count,
                           std::string_view countText) {
  // A shift on a non-integer type is malformed CIR, which emitModule rejects
  // before reaching here.  The check is kept anyway because the alternative is
  // `intWidth(t) - 1` underflowing to 0xFFFFFFFF and masking with garbage.
  const unsigned bits = cir::intWidth(t);
  if (bits == 0)
    return std::string(countText);

  const unsigned mask = bits - 1;
  if (const ConstInt *c = cir::asConstInt(count))
    return std::to_string(cir::wrapInt(cir::maskShift(c->value, t), t));

  const std::string masked = em.tmp();
  em.line(masked + " = and " + ty(t) + " " + std::string(countText) + ", " +
          std::to_string(mask));
  return masked;
}

void guardFPToSI(Emitter &em, Ty t, std::string_view valueText) {
  const unsigned bits = cir::intWidth(t);
  // An fptosi whose result type is not an integer is malformed CIR, which
  // emitModule rejects before reaching here.  Without this check, `bits - 1`
  // underflows and the shift below is undefined behaviour -- which is exactly
  // the class of bug docs/divergence.md exists to keep out of this compiler.
  if (bits == 0)
    return;

  // Truncation toward zero is defined exactly for
  //     -(2^(bits-1)) - 1  <  x  <  2^(bits-1)
  // Both 2^(bits-1) and -(2^(bits-1)) are exact doubles for every width CIR
  // has.  The *lower* bound is the subtle one: -(2^(bits-1)) - 1 is exact for
  // i32 (-2147483649) but NOT for i64, where it rounds back to -(2^63).  Using
  // `ole -(2^63)` there would trap on x = -2^63, which is a perfectly valid
  // conversion.  So when the exclusive bound is not representable, compare
  // strictly below the inclusive one instead.
  const double minValue = -static_cast<double>(std::uint64_t{1} << (bits - 1));
  const double high = static_cast<double>(std::uint64_t{1} << (bits - 1));
  const double lowExclusive = minValue - 1.0;
  const bool lowIsExact = lowExclusive != minValue;

  const std::string v(valueText);

  const std::string isNan = em.tmp();
  const std::string tooLow = em.tmp();
  const std::string tooHigh = em.tmp();
  const std::string outOfRange = em.tmp();
  const std::string bad = em.tmp();

  em.line(isNan + " = fcmp uno double " + v + ", " + v);
  em.line(tooLow + " = fcmp " + (lowIsExact ? "ole " : "olt ") + "double " + v +
          ", " + cir::printDouble(lowIsExact ? lowExclusive : minValue));
  em.line(tooHigh + " = fcmp oge double " + v + ", " + cir::printDouble(high));
  em.line(outOfRange + " = or i1 " + tooLow + ", " + tooHigh);
  em.line(bad + " = or i1 " + isNan + ", " + outOfRange);
  em.trapBranch(bad, "fptosi");
}

void guardBounds(Emitter &em, std::int64_t extent, const Value &index,
                 std::string_view indexText) {
  if (const ConstInt *c = cir::asConstInt(index))
    if (c->value >= 0 && c->value < extent)
      return; // provably in bounds

  const std::string bad = em.tmp();
  em.line(bad + " = icmp uge i32 " + std::string(indexText) + ", " +
          std::to_string(extent));
  em.trapBranch(bad, "bounds");
}

} // namespace mtir::backend::llvm
