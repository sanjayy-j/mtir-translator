// CFG.h -- control-flow graph queries over a CIR function.
//
// The edges are *explicit*: a block's successors are exactly the labels of
// its terminator, so building the graph is a scan rather than an analysis.
// That is the point of the "explicit control flow" design decision in
// docs/cir-spec.md section 1 -- the LLVM back end consumes these edges
// unchanged, and the WebAssembly back end has a real graph to restructure.
//
// Inside the CFG, blocks are dense integer ids rather than label strings, so
// edge traversal is vector indexing and the dataflow in StructuralChecks can
// use bitsets.  Labels reappear only at the boundary.
//
// A CFG is a read-only view: it is built from a Function and must be rebuilt
// after the function is mutated.
#ifndef MTIR_CIR_CFG_H
#define MTIR_CIR_CFG_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "mtir/cir/Function.h"

namespace mtir::cir {

using BlockId = std::uint32_t;

class CFG {
public:
  explicit CFG(const Function &fn);

  std::size_t size() const { return labels_.size(); }
  bool empty() const { return labels_.empty(); }

  /// Always 0 when the function has at least one block; the entry block is
  /// the first one, per docs/cir-spec.md.
  BlockId entry() const { return 0; }

  const std::string &label(BlockId id) const { return labels_[id]; }
  std::optional<BlockId> find(std::string_view label) const;

  /// In terminator order, so the true target of a br.cond comes first.
  /// Branch targets that name no existing block are omitted here and are
  /// reported by the verifier as rule 3.
  const std::vector<BlockId> &successors(BlockId id) const { return succs_[id]; }
  const std::vector<BlockId> &predecessors(BlockId id) const { return preds_[id]; }

  bool isReachable(BlockId id) const { return reachable_[id]; }

  /// Reverse postorder over the reachable blocks.  Visiting a dataflow in
  /// this order converges in far fewer rounds than definition order.
  const std::vector<BlockId> &reversePostorder() const { return rpo_; }

  /// True when some terminator names a label this function does not define.
  bool hasDanglingEdge() const { return dangling_; }

private:
  std::vector<std::string> labels_;
  std::unordered_map<std::string, BlockId> index_;
  std::vector<std::vector<BlockId>> succs_;
  std::vector<std::vector<BlockId>> preds_;
  std::vector<bool> reachable_;
  std::vector<BlockId> rpo_;
  bool dangling_ = false;

  void computeReachability();
  void computeReversePostorder();
};

} // namespace mtir::cir

#endif // MTIR_CIR_CFG_H
