// EmitLL.h -- CIR -> textual LLVM IR.
//
// Why textual IR and not the LLVM C++ API: the artefact under review is the
// translation, not the bindings.  Text is diffable, is what the report's
// figures show, and is checked by llvm-as, which is a real verifier rather
// than a smoke test.  Linking LLVM would add a multi-gigabyte dependency that
// every team member must install, for a project whose deliverable is exactly
// the mapping the API would hide.
//
// CIR's CFG maps one-to-one onto LLVM's -- both allow a branch to any label
// in the function -- so no restructuring is needed and this back end is close
// to a printer.  That is the contrast the report draws against the
// WebAssembly back end, which must rebuild the control flow as nested
// regions.
//
// What stops it being *only* a printer is Guards.h.  Four CIR operations mean
// something LLVM leaves undefined, so the emitter splits the current block
// and branches to a trap block instead of emitting the bare instruction.  A
// block split is why the emitter works on a flat line buffer with its own
// label counter rather than one LLVM block per CIR block.
#ifndef MTIR_BACKEND_LLVM_EMITLL_H
#define MTIR_BACKEND_LLVM_EMITLL_H

#include <string>

#include "mtir/cir/Module.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::backend::llvm {

struct EmitResult {
  std::string ir;
  support::Diagnostics diagnostics;

  bool ok() const { return !support::hasErrors(diagnostics); }
};

EmitResult emitModule(const cir::Module &module);

} // namespace mtir::backend::llvm

#endif // MTIR_BACKEND_LLVM_EMITLL_H
