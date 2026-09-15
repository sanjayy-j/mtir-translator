#include "mtir/cir/Value.h"

namespace mtir::cir {

bool operator==(const Reg &a, const Reg &b) {
  return a.name == b.name && a.ty == b.ty;
}
bool operator==(const ConstInt &a, const ConstInt &b) {
  return a.value == b.value && a.ty == b.ty;
}
bool operator==(const ConstFloat &a, const ConstFloat &b) {
  // Bitwise-ish equality is what golden tests want; NaN never compares equal,
  // which is correct for both IEEE and for round-trip testing.
  return a.value == b.value;
}
bool operator==(const GlobalRef &a, const GlobalRef &b) { return a.name == b.name; }

Ty typeOf(const Value &v) {
  struct Visitor {
    Ty operator()(const Reg &r) const { return r.ty; }
    Ty operator()(const ConstInt &c) const { return c.ty; }
    Ty operator()(const ConstFloat &) const { return Ty::F64; }
    Ty operator()(const GlobalRef &) const { return Ty::Ptr; }
  };
  return std::visit(Visitor{}, v);
}

bool isConstant(const Value &v) {
  return std::holds_alternative<ConstInt>(v) || std::holds_alternative<ConstFloat>(v);
}

const Reg *asRegister(const Value &v) { return std::get_if<Reg>(&v); }
const ConstInt *asConstInt(const Value &v) { return std::get_if<ConstInt>(&v); }
const ConstFloat *asConstFloat(const Value &v) { return std::get_if<ConstFloat>(&v); }

} // namespace mtir::cir
