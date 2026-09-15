#include "mtir/cir/Type.h"

#include <array>
#include <utility>

namespace mtir::cir {
namespace {

constexpr std::array<std::pair<Ty, std::string_view>, 6> kNames{{
    {Ty::I1, "i1"},
    {Ty::I32, "i32"},
    {Ty::I64, "i64"},
    {Ty::F64, "f64"},
    {Ty::Ptr, "ptr"},
    {Ty::Void, "void"},
}};

} // namespace

std::string_view toString(Ty t) {
  for (const auto &entry : kNames)
    if (entry.first == t)
      return entry.second;
  return "<invalid>";
}

std::optional<Ty> parseTy(std::string_view text) {
  for (const auto &entry : kNames)
    if (entry.second == text)
      return entry.first;
  return std::nullopt;
}

} // namespace mtir::cir
