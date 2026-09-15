// TypeMap.h -- CIR type -> LLVM type.
//
// Every CIR type has an exact LLVM counterpart, which is the pay-off of the
// "typed on every instruction" decision in docs/cir-spec.md section 1: the
// back end never infers and never widens.  Two rows are worth pointing at:
//
//   i1   maps to LLVM's native i1.  WebAssembly has no 1-bit type and must
//        normalise to i32 (docs/divergence.md row 5), so this is one of the
//        places the two back ends genuinely differ rather than differing only
//        in syntax.
//   ptr  maps to LLVM's opaque pointer.  docs/examples/abs.ll already uses
//        it, so LLVM 15 or newer is assumed.
#ifndef MTIR_BACKEND_LLVM_TYPEMAP_H
#define MTIR_BACKEND_LLVM_TYPEMAP_H

#include <string_view>

#include "mtir/cir/Type.h"

namespace mtir::backend::llvm {

std::string_view llvmType(cir::Ty t);

} // namespace mtir::backend::llvm

#endif // MTIR_BACKEND_LLVM_TYPEMAP_H
