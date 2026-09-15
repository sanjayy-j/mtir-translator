#include "mtir/backend/llvm/TypeMap.h"

namespace mtir::backend::llvm {

std::string_view llvmType(cir::Ty t) {
  switch (t) {
  case cir::Ty::I1:
    return "i1";
  case cir::Ty::I32:
    return "i32";
  case cir::Ty::I64:
    return "i64";
  case cir::Ty::F64:
    return "double";
  case cir::Ty::Ptr:
    return "ptr";
  case cir::Ty::Void:
    return "void";
  }
  return "void";
}

} // namespace mtir::backend::llvm
