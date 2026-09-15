// Builder.h -- AST + semantic analysis -> CIR.
//
// Module M3a.  The interface is docs/frontend-cir-contract.md section 1.
//
// One Function per FnDecl.  Expressions are flattened to three-address form
// with a fresh-register allocator, statements are split into basic blocks,
// and every block is closed with exactly one terminator.  The control-flow
// shapes are the ones fixed in docs/cir-spec.md section 5.
//
// Storage model (cir-spec.md, "Not in SSA form"): mutable locals live in
// memory -- an alloca in the entry block plus load/store at each use.  That
// is what keeps the IR out of SSA without phi nodes, and LLVM's mem2reg
// recovers SSA downstream.
//
// One deliberate exception: a parameter the function never assigns stays in
// its incoming register.  It has no address, nothing can observe it in
// memory, and the saving is a store and a load per use.  It is also why
// abs() comes out as the three blocks drawn in docs/examples/abs.cir.
//
// Precondition: `build` is only called on a program semantic analysis has
// accepted.  It is not a second line of defence against ill-typed input and
// it does not re-report E001-E012.
#ifndef MTIR_CIR_BUILDER_H
#define MTIR_CIR_BUILDER_H

#include <optional>
#include <string>

#include "mtir/ast/AST.h"
#include "mtir/cir/Module.h"
#include "mtir/sema/TypeInfo.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::cir {

struct BuildResult {
  std::optional<Module> module;
  support::Diagnostics diagnostics;

  bool ok() const { return module.has_value(); }
};

BuildResult build(const ast::Program &program, const sema::TypeInfo &types,
                  std::string moduleName = "module");

} // namespace mtir::cir

#endif // MTIR_CIR_BUILDER_H
