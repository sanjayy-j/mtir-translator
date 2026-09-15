// EmitWat.h -- CIR -> WebAssembly text format.
//
// Module M6c.  This is the back end the project exists to contrast with the
// LLVM one.  LLVM accepts a branch to any label, so the LLVM emitter is close
// to a printer over the CIR CFG.  WebAssembly has no such branch: control
// flow is only expressible as nested `block`, `loop` and `if` regions with
// *relative* branch depths, so an arbitrary CFG must be re-expressed.
//
// How the control flow is structured
// ----------------------------------
// This uses the dispatch-tower form, which is correct for **any** CFG,
// reducible or not:
//
//     (local $__block i32)
//     (block $exit
//       (loop $dispatch
//         (block $case2
//           (block $case1
//             (block $case0
//               (br_table $case0 $case1 $case2 (local.get $__block)))
//             ;; CIR block 0 -- ends by setting $__block and (br $dispatch),
//             ;;                or by (return)
//           )
//           ;; CIR block 1
//         )
//         ;; CIR block 2
//       )
//     )
//
// Falling out of `(block $caseN)` lands exactly at CIR block N's body, so the
// br_table selects a block and the loop re-dispatches after each one.
//
// It is deliberately not the prettiest possible output.  A Relooper- or
// Stackifier-style structural analysis would recover real `if`/`loop` nesting
// for the reducible CFGs MiniLang produces, and docs/cir-spec.md section 5
// explains why every CFG the builder emits *is* reducible.  That is a
// worthwhile improvement and is recorded as future work; what matters first
// is that the translation is correct for every CFG the middle end can hand
// it, including after optimisation has rewritten the block structure.
//
// docs/examples/abs.wat shows the structured form a relooper would produce
// for the abs example, and remains the hand-written reference.
#ifndef MTIR_BACKEND_WASM_EMITWAT_H
#define MTIR_BACKEND_WASM_EMITWAT_H

#include <string>

#include "mtir/cir/Module.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::backend::wasm {

struct EmitResult {
  std::string wat;
  support::Diagnostics diagnostics;

  bool ok() const { return !support::hasErrors(diagnostics); }
};

EmitResult emitModule(const cir::Module &module);

} // namespace mtir::backend::wasm

#endif // MTIR_BACKEND_WASM_EMITWAT_H
