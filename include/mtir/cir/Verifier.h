// Verifier.h -- CIR well-formedness checking.
//
// Implements the eight rules of docs/cir-spec.md section 6, which Objective
// O1 requires to be *detectable* classes of malformed IR:
//
//   CIR01  every basic block ends in exactly one terminator
//   CIR02  no instruction follows a terminator
//   CIR03  every branch target names an existing block in the same function
//   CIR04  every register is defined before it is used, on every path
//   CIR05  the entry block has no predecessors
//   CIR06  operand types match the opcode signature
//   CIR07  the result of an icmp/fcmp is i1
//   CIR08  a non-void function ends every path in `ret <ty>`
//
// plus two structural errors the rules assume away: a function with no blocks
// (CIR00) and duplicate block labels (CIR09).
//
// Ownership note for the team: docs/cir-spec.md assigns rules 1-5 to the CFG
// layer (M3) and rules 6-8 to the verifier (M2).  During the C++ migration M3
// implemented all eight in one place, because the LLVM back end cannot safely
// emit from IR whose operand types have not been checked.  M2 should review
// and take ownership of the rule 6-8 section of Verifier.cpp.  The *source*
// diagnostics E001-E012 of docs/minilang-spec.md section 7 remain entirely
// M2's and are not touched here.
#ifndef MTIR_CIR_VERIFIER_H
#define MTIR_CIR_VERIFIER_H

#include "mtir/cir/Module.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::cir {

/// Rules 1, 2, 3, 5 and label uniqueness -- everything that is a property of
/// the control-flow graph alone.
support::Diagnostics checkCFG(const Function &fn);

/// Rule 4.  A forward dataflow whose meet is intersection, so a register
/// defined in only one arm of a branch is rejected at the join.  Unreachable
/// blocks are skipped: no path reaches them, so the property is vacuous
/// there, and reporting them would duplicate what dead-code elimination
/// removes.
support::Diagnostics checkDefinitions(const Function &fn);

/// Rules 6, 7 and 8.  `module` is optional; when given, calls are checked
/// against the callee's signature.
support::Diagnostics checkTypes(const Function &fn, const Module *module = nullptr);

/// All of the above for one function.  A broken graph short-circuits the rest:
/// the dataflow is meaningless once a branch target is missing, and running
/// it anyway produces a cascade of derived errors instead of one real one.
support::Diagnostics verifyFunction(const Function &fn, const Module *module = nullptr);

/// All of the above for every function in a module.
support::Diagnostics verify(const Module &module);

} // namespace mtir::cir

#endif // MTIR_CIR_VERIFIER_H
