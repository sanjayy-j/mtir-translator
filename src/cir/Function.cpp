#include "mtir/cir/Function.h"

#include <cstddef>
#include <utility>

namespace mtir::cir {

const Instruction *BasicBlock::terminator() const {
  if (instrs_.empty())
    return nullptr;
  const Instruction &last = instrs_.back();
  return last.isTerminator() ? &last : nullptr;
}

std::vector<std::string> BasicBlock::successors() const {
  const Instruction *term = terminator();
  if (term == nullptr)
    return {};
  return term->labels;
}

bool operator==(const BasicBlock &a, const BasicBlock &b) {
  return a.label() == b.label() && a.instructions() == b.instructions();
}

BasicBlock &Function::addBlock(std::string label) {
  index_.emplace(label, blocks_.size());
  blocks_.emplace_back(std::move(label));
  return blocks_.back();
}

void Function::addBlock(BasicBlock block) {
  index_.emplace(block.label(), blocks_.size());
  blocks_.push_back(std::move(block));
}

void Function::setBlocks(std::vector<BasicBlock> blocks) {
  blocks_ = std::move(blocks);
  rebuildIndex();
}

void Function::rebuildIndex() {
  index_.clear();
  index_.reserve(blocks_.size());
  for (std::size_t i = 0; i < blocks_.size(); ++i) {
    // emplace keeps the first occurrence, so a duplicate label resolves to
    // the earlier block.  The verifier reports the duplicate separately.
    index_.emplace(blocks_[i].label(), i);
  }
}

BasicBlock *Function::block(std::string_view label) {
  const auto it = index_.find(std::string(label));
  if (it == index_.end())
    return nullptr;
  return &blocks_[it->second];
}

const BasicBlock *Function::block(std::string_view label) const {
  const auto it = index_.find(std::string(label));
  if (it == index_.end())
    return nullptr;
  return &blocks_[it->second];
}

bool operator==(const Function &a, const Function &b) {
  return a.name() == b.name() && a.params() == b.params() &&
         a.returnType() == b.returnType() && a.blocks() == b.blocks();
}

} // namespace mtir::cir
