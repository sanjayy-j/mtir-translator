#include "mtir/opt/Passes.h"

#include <cstddef>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mtir/cir/CFG.h"

namespace mtir::opt {
namespace {

using namespace mtir::cir;

std::unordered_set<std::string> usedRegisters(const Function &fn) {
  std::unordered_set<std::string> used;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions())
      for (const Value &a : i.args)
        if (const Reg *r = asRegister(a))
          used.insert(r->name);
  return used;
}

bool removeUnreachableBlocks(Function &fn) {
  if (fn.blocks().empty())
    return false;

  const CFG cfg(fn);
  std::vector<BasicBlock> live;
  live.reserve(fn.blocks().size());
  for (std::size_t i = 0; i < fn.blocks().size(); ++i)
    if (cfg.isReachable(static_cast<BlockId>(i)))
      live.push_back(fn.blocks()[i]);

  if (live.size() == fn.blocks().size())
    return false;
  fn.setBlocks(std::move(live));
  return true;
}

bool removeDeadDefinitions(Function &fn) {
  const std::unordered_set<std::string> used = usedRegisters(fn);
  bool changed = false;

  for (BasicBlock &block : fn.blocks()) {
    std::vector<Instruction> kept;
    kept.reserve(block.instructions().size());
    for (Instruction &instr : block.instructions()) {
      const bool dead = instr.dest.has_value() &&
                        used.count(instr.dest->name) == 0 &&
                        !instr.hasSideEffect() && !instr.isTerminator();
      if (dead) {
        changed = true;
        continue;
      }
      kept.push_back(std::move(instr));
    }
    block.instructions() = std::move(kept);
  }
  return changed;
}

} // namespace

bool DeadCodeEliminationPass::runOnFunction(Function &fn) {
  bool changed = false;
  for (;;) {
    // Removing a definition can kill another, and removing a block can make
    // another unreachable, so both run to a fixed point together.
    bool round = removeUnreachableBlocks(fn);
    round = removeDeadDefinitions(fn) || round;
    changed = changed || round;
    if (!round)
      return changed;
  }
}

} // namespace mtir::opt
