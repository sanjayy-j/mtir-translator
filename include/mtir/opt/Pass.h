// Pass.h -- the optimisation pass interface and pipeline.
//
// The passes are ordered so that each creates work for the next: constant
// folding turns subexpressions into constants, which turns the operations
// around them into identity copies, which copy propagation removes, which
// leaves definitions nobody reads for dead-code elimination.  Folding a
// br.cond into a br likewise only pays off once DCE drops the block it no
// longer reaches, so the sequence runs to a fixed point rather than once.
//
// Every pass preserves CIR semantics as fixed in docs/divergence.md.  In
// particular **no pass may remove a trap**; each pass documents what it
// refuses to transform and why.
#ifndef MTIR_OPT_PASS_H
#define MTIR_OPT_PASS_H

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "mtir/cir/Module.h"

namespace mtir::opt {

class Pass {
public:
  virtual ~Pass() = default;
  virtual std::string_view name() const = 0;

  /// Transform one function.  Returns true if anything changed.
  virtual bool runOnFunction(cir::Function &fn) = 0;
};

class PassManager {
public:
  /// This is the one place unique_ptr is warranted in the project: passes are
  /// genuinely polymorphic and the manager genuinely owns them.
  void add(std::unique_ptr<Pass> pass);

  /// Run every pass over every function to a fixed point.  Returns true if
  /// anything changed.
  bool run(cir::Module &module);

  void setMaxRounds(unsigned rounds) { maxRounds_ = rounds; }

private:
  std::vector<std::unique_ptr<Pass>> passes_;
  /// A safety net.  Every pass is monotone -- it only ever removes
  /// instructions -- so the loop terminates on its own; the cap just bounds
  /// the work on a pathological input.
  unsigned maxRounds_ = 8;
};

/// Constant folding, then copy propagation, then dead-code elimination.
/// `level` 0 returns an empty pipeline, so the driver needs no special case.
PassManager defaultPipeline(int level);

/// Total instruction count, for before/after measurements.
std::size_t countInstructions(const cir::Module &module);

} // namespace mtir::opt

#endif // MTIR_OPT_PASS_H
