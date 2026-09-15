// Value.h -- CIR operands.
//
// A CIR operand is one of exactly four things, so it is a closed sum type.
// std::variant plus a fully covered std::visit means an unhandled kind is a
// compile error; the Python prototype's `getattr(value, "ty", default)`
// fallback silently produced a wrong type for an unexpected operand, and that
// mistake is not expressible here.
#ifndef MTIR_CIR_VALUE_H
#define MTIR_CIR_VALUE_H

#include <cstdint>
#include <string>
#include <variant>

#include "mtir/cir/Type.h"

namespace mtir::cir {

/// A virtual register.  Unbounded in number; CIR is not in SSA form, so a
/// register may be assigned more than once.
struct Reg {
  std::string name;
  Ty ty = Ty::I32;
};

struct ConstInt {
  std::int64_t value = 0;
  Ty ty = Ty::I32;
};

/// CIR has only one floating-point type, so a float constant is always f64.
struct ConstFloat {
  double value = 0.0;
};

/// A reference to a module-level global.  Always a pointer.
struct GlobalRef {
  std::string name;
};

using Value = std::variant<Reg, ConstInt, ConstFloat, GlobalRef>;

bool operator==(const Reg &a, const Reg &b);
bool operator==(const ConstInt &a, const ConstInt &b);
bool operator==(const ConstFloat &a, const ConstFloat &b);
bool operator==(const GlobalRef &a, const GlobalRef &b);

inline bool operator!=(const Reg &a, const Reg &b) { return !(a == b); }
inline bool operator!=(const ConstInt &a, const ConstInt &b) { return !(a == b); }
inline bool operator!=(const ConstFloat &a, const ConstFloat &b) { return !(a == b); }
inline bool operator!=(const GlobalRef &a, const GlobalRef &b) { return !(a == b); }

Ty typeOf(const Value &v);

bool isConstant(const Value &v);

/// The register, if this operand is one; nullptr otherwise.
const Reg *asRegister(const Value &v);
const ConstInt *asConstInt(const Value &v);
const ConstFloat *asConstFloat(const Value &v);

} // namespace mtir::cir

#endif // MTIR_CIR_VALUE_H
