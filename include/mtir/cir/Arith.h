// Arith.h -- the single definition of what CIR integer arithmetic means.
//
// This header is load-bearing and should be read before any pass or back end.
// docs/divergence.md fixes CIR's arithmetic, and it does not match C++'s:
//
//   row 3  shift count >= width  -> CIR takes it modulo the width;
//                                   C++ leaves an over-wide shift undefined.
//   row 4  signed overflow       -> CIR wraps two's complement;
//                                   C++ leaves signed overflow undefined.
//   rows 1-2  division by zero and INT_MIN / -1 -> CIR traps;
//                                   C++ leaves both undefined.
//
// A compiler that defines integer semantics must not have undefined integer
// semantics itself, so every computation here runs in the unsigned domain and
// converts back at the end.  Nothing in this project may compute a CIR
// integer result any other way.
#ifndef MTIR_CIR_ARITH_H
#define MTIR_CIR_ARITH_H

#include <cstdint>

#include "mtir/cir/Type.h"

namespace mtir::cir {

/// Bit pattern of `v` in `t`, zero-extended into a uint64_t.
std::uint64_t toUnsigned(std::int64_t v, Ty t);

/// Reduce `v` to the two's-complement range of `t` (docs/divergence.md row 4).
std::int64_t wrapInt(std::int64_t v, Ty t);

/// The shift amount CIR actually applies (docs/divergence.md row 3).
unsigned maskShift(std::int64_t count, Ty t);

/// The most negative value of a signed integer type.
std::int64_t intMin(Ty t);

/// Does this division or remainder trap under CIR semantics?
/// Rows 1 and 2: a zero divisor always traps, and INT_MIN / -1 traps for the
/// signed forms.  Taking `isSigned` rather than an Opcode keeps this header
/// independent of Opcode.h.
bool divTraps(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t);

/// Truncating division and remainder, defined only where divTraps() is false.
/// Both round the quotient toward zero, which is what LLVM, WebAssembly and C
/// all do -- and which C++'s own `/` and `%` already give, so no sign
/// correction is needed.
std::int64_t divTrunc(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t);
std::int64_t remTrunc(bool isSigned, std::int64_t lhs, std::int64_t rhs, Ty t);

} // namespace mtir::cir

#endif // MTIR_CIR_ARITH_H
