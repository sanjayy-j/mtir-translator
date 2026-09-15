#include "mtir/cir/Arith.h"

#include <limits>

namespace mtir::cir {

std::uint64_t toUnsigned(std::int64_t v, Ty t) {
  const unsigned bits = intWidth(t);
  const auto raw = static_cast<std::uint64_t>(v);
  if (bits == 0 || bits >= 64)
    return raw;
  return raw & ((std::uint64_t{1} << bits) - 1);
}

std::int64_t wrapInt(std::int64_t v, Ty t) {
  const unsigned bits = intWidth(t);
  if (bits == 0 || bits >= 64)
    return v;
  const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
  const std::uint64_t masked = static_cast<std::uint64_t>(v) & mask;
  const std::uint64_t signBit = std::uint64_t{1} << (bits - 1);
  if (masked & signBit) {
    // Sign-extend by subtracting 2^bits, done in the unsigned domain so no
    // step of the computation can overflow a signed type.
    return static_cast<std::int64_t>(masked | ~mask);
  }
  return static_cast<std::int64_t>(masked);
}

unsigned maskShift(std::int64_t count, Ty t) {
  const unsigned bits = intWidth(t);
  if (bits == 0)
    return 0;
  return static_cast<unsigned>(toUnsigned(count, Ty::I64) & (bits - 1));
}

std::int64_t intMin(Ty t) {
  const unsigned bits = intWidth(t);
  if (bits == 0)
    return 0;
  if (bits >= 64)
    return std::numeric_limits<std::int64_t>::min();
  return -(std::int64_t{1} << (bits - 1));
}

bool divTraps(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t) {
  if (isSigned) {
    if (rhs == 0)
      return true;
    return lhs == intMin(t) && rhs == -1;
  }
  return toUnsigned(rhs, t) == 0;
}

std::int64_t divTrunc(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t) {
  if (!isSigned) {
    const std::uint64_t a = toUnsigned(lhs, t);
    const std::uint64_t b = toUnsigned(rhs, t);
    return wrapInt(static_cast<std::int64_t>(a / b), t);
  }
  // divTraps() has already excluded rhs == 0 and INT_MIN / -1, which are the
  // only two signed divisions that are undefined in C++.
  return wrapInt(lhs / rhs, t);
}

std::int64_t remTrunc(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t) {
  if (!isSigned) {
    const std::uint64_t a = toUnsigned(lhs, t);
    const std::uint64_t b = toUnsigned(rhs, t);
    return wrapInt(static_cast<std::int64_t>(a % b), t);
  }
  return wrapInt(lhs % rhs, t);
}

} // namespace mtir::cir
