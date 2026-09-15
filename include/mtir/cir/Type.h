// Type.h -- the CIR type system.
//
// docs/cir-spec.md section 2 fixes exactly six types and no aggregates: an
// array decays to a `ptr` plus an element type.  That is why this is a plain
// enum rather than a class hierarchy with a uniquing context -- there is
// nothing to unique, equality is enum comparison, and a Type is never
// allocated or pointed to.
#ifndef MTIR_CIR_TYPE_H
#define MTIR_CIR_TYPE_H

#include <cstdint>
#include <optional>
#include <string_view>

namespace mtir::cir {

enum class Ty : std::uint8_t { I1, I32, I64, F64, Ptr, Void };

constexpr bool isInteger(Ty t) {
  return t == Ty::I1 || t == Ty::I32 || t == Ty::I64;
}

constexpr bool isFloat(Ty t) { return t == Ty::F64; }

/// Width in bits of an integer type; 0 for anything else.
constexpr unsigned intWidth(Ty t) {
  switch (t) {
  case Ty::I1:
    return 1;
  case Ty::I32:
    return 32;
  case Ty::I64:
    return 64;
  default:
    return 0;
  }
}

/// The spelling used in .cir text -- "i1", "i32", "i64", "f64", "ptr", "void".
std::string_view toString(Ty t);

std::optional<Ty> parseTy(std::string_view text);

} // namespace mtir::cir

#endif // MTIR_CIR_TYPE_H
