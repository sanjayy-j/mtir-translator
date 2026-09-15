#include "mtir/opt/Pass.h"

#include "mtir/opt/Passes.h"

#include <cstddef>
#include <utility>

namespace mtir::opt {

void PassManager::add(std::unique_ptr<Pass> pass) {
  passes_.push_back(std::move(pass));
}

bool PassManager::run(cir::Module &module) {
  bool changed = false;
  for (cir::Function &fn : module.functions()) {
    for (unsigned round = 0; round < maxRounds_; ++round) {
      bool roundChanged = false;
      for (const std::unique_ptr<Pass> &pass : passes_)
        roundChanged = pass->runOnFunction(fn) || roundChanged;
      changed = changed || roundChanged;
      if (!roundChanged)
        break;
    }
  }
  return changed;
}

PassManager defaultPipeline(int level) {
  PassManager pm;
  if (level <= 0)
    return pm;
  pm.add(std::make_unique<ConstantFoldingPass>());
  pm.add(std::make_unique<CopyPropagationPass>());
  pm.add(std::make_unique<DeadCodeEliminationPass>());
  return pm;
}

std::size_t countInstructions(const cir::Module &module) {
  std::size_t total = 0;
  for (const cir::Function &fn : module.functions())
    for (const cir::BasicBlock &block : fn.blocks())
      total += block.instructions().size();
  return total;
}

} // namespace mtir::opt
