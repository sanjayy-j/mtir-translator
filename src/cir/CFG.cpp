#include "mtir/cir/CFG.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace mtir::cir {

CFG::CFG(const Function &fn) {
  const std::vector<BasicBlock> &blocks = fn.blocks();
  labels_.reserve(blocks.size());
  for (const BasicBlock &b : blocks) {
    // A duplicate label resolves to the first block of that name, matching
    // Function::block().  The verifier reports the duplicate itself.
    index_.emplace(b.label(), static_cast<BlockId>(labels_.size()));
    labels_.push_back(b.label());
  }

  succs_.assign(labels_.size(), {});
  preds_.assign(labels_.size(), {});

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto from = static_cast<BlockId>(i);
    for (const std::string &target : blocks[i].successors()) {
      const auto it = index_.find(target);
      if (it == index_.end()) {
        dangling_ = true;
        continue;
      }
      const BlockId to = it->second;
      succs_[from].push_back(to);
      // Deduplicate predecessors: `br %c ? join : join` is one edge from the
      // graph's point of view, not two.
      if (std::find(preds_[to].begin(), preds_[to].end(), from) == preds_[to].end())
        preds_[to].push_back(from);
    }
  }

  computeReachability();
  computeReversePostorder();
}

std::optional<BlockId> CFG::find(std::string_view label) const {
  const auto it = index_.find(std::string(label));
  if (it == index_.end())
    return std::nullopt;
  return it->second;
}

void CFG::computeReachability() {
  reachable_.assign(labels_.size(), false);
  if (labels_.empty())
    return;

  std::vector<BlockId> stack{entry()};
  while (!stack.empty()) {
    const BlockId id = stack.back();
    stack.pop_back();
    if (reachable_[id])
      continue;
    reachable_[id] = true;
    for (BlockId s : succs_[id])
      if (!reachable_[s])
        stack.push_back(s);
  }
}

void CFG::computeReversePostorder() {
  rpo_.clear();
  if (labels_.empty())
    return;

  // Iterative postorder DFS: the recursive form would be shorter but a deeply
  // nested function is exactly the input that would blow the stack.
  enum class State : std::uint8_t { Fresh, Open };
  std::vector<State> state(labels_.size(), State::Fresh);
  std::vector<std::pair<BlockId, std::size_t>> stack;
  stack.emplace_back(entry(), 0);
  state[entry()] = State::Open;

  std::vector<BlockId> postorder;
  while (!stack.empty()) {
    auto &[id, next] = stack.back();
    if (next < succs_[id].size()) {
      const BlockId child = succs_[id][next++];
      if (state[child] == State::Fresh) {
        state[child] = State::Open;
        stack.emplace_back(child, 0);
      }
      continue;
    }
    postorder.push_back(id);
    stack.pop_back();
  }

  rpo_.assign(postorder.rbegin(), postorder.rend());
}

} // namespace mtir::cir
