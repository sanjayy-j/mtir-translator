// Passes.h -- the individual optimisation passes.
#ifndef MTIR_OPT_PASSES_H
#define MTIR_OPT_PASSES_H

#include <memory>
#include <optional>

#include "mtir/opt/Pass.h"

namespace mtir::opt {

// ==========================================================================
// Constant folding and propagation
// ==========================================================================
//
// Deliberately *not* folded, and why.  An optimisation may not turn a program
// that traps into one that does not, nor silently adopt the host's arithmetic
// where CIR has defined its own:
//
//   * sdiv/srem/udiv/urem fold only where the operands prove the operation
//     cannot trap -- a non-zero divisor, and not INT_MIN / -1
//     (docs/divergence.md rows 1 and 2).  Folding a trapping case would
//     delete a trap the program is defined to take.
//   * fdiv is never folded: CIR does not fix what float division by zero
//     means.
//   * Nothing folds when an operand is NaN -- the ordered/unordered
//     distinction is the back end's to realise (row 6).
//   * Every integer result goes through cir::wrapInt (row 4) and every shift
//     count through cir::maskShift (row 3).
//
// Why two phases.  CIR is not in SSA form, so a register may in principle be
// assigned more than once.  Only registers with exactly one definition in the
// function are propagated; for those, well-formedness rule 4 means every path
// to a use runs through that one definition, which is what makes substituting
// the constant sound.  Analysis completes before any rewriting, so the IR is
// never left with a deleted definition and a surviving use.
class ConstantFoldingPass final : public Pass {
public:
  std::string_view name() const override { return "constfold"; }
  bool runOnFunction(cir::Function &fn) override;
};

/// The constant an instruction computes, or nothing when it is not foldable.
/// Exposed for testing; the pass is the normal entry point.
std::optional<cir::Value> foldInstruction(const cir::Instruction &instr);

// ==========================================================================
// Copy propagation
// ==========================================================================
//
// CIR has no move instruction -- three-address code with a register file does
// not need one -- so there is nothing for a textbook copy-propagation pass to
// chase.  What *does* produce copies is arithmetic with an identity operand:
//
//     %t = add i32 %x, 0     %t = mul i32 %x, 1     %t = shl i32 %x, 0
//     %t = sub i32 %x, 0     %t = sdiv i32 %x, 1    %t = or  i32 %x, 0
//     %t = and i32 %x, -1    %t = xor i32 %x, 0
//
// Each defines a register that is just another name for %x.  The pass
// rewrites every use and then drops the instruction: after the rewrite
// nothing refers to %t, and none of these opcodes has a side effect.
//
// Such instructions come mostly from the constant folder -- once a
// subexpression folds to 0 or 1, what surrounded it becomes an identity -- so
// running copy propagation after folding is not arbitrary.
//
// Not included, deliberately: `srem %x, 1` is 0 rather than a copy (the
// folder's job), float identities are excluded because `fadd %x, 0.0` turns
// -0.0 into +0.0, and sdiv/udiv count only for the divisor 1, where neither
// the zero nor the INT_MIN / -1 trap can arise.
class CopyPropagationPass final : public Pass {
public:
  std::string_view name() const override { return "copyprop"; }
  bool runOnFunction(cir::Function &fn) override;
};

/// The value this instruction is a copy of, or nothing if it is not a copy.
std::optional<cir::Value> copySource(const cir::Instruction &instr);

// ==========================================================================
// Dead-code elimination
// ==========================================================================
//
// Two eliminations:
//
//  1. Unreachable blocks.  These are not pathological -- constant folding
//     creates one every time it turns a br.cond on a constant into a br,
//     which is what makes `while (false) { ... }` disappear.
//  2. Dead definitions.  An instruction whose destination is never read and
//     whose execution is not itself observable has no effect.  Observability
//     is the whole question: store, call, print.* and trap stay regardless.
//     A call is kept even when its result is unused because CIR has no purity
//     annotation, and assuming a call is pure would be exactly the unsound
//     shortcut this project exists to avoid.
class DeadCodeEliminationPass final : public Pass {
public:
  std::string_view name() const override { return "dce"; }
  bool runOnFunction(cir::Function &fn) override;
};

} // namespace mtir::opt

#endif // MTIR_OPT_PASSES_H
