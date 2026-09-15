// Function.h -- basic blocks and functions.
//
// Ownership: blocks and instructions are stored by value.  Nothing in CIR is
// polymorphic and nothing points at an instruction, so vectors of values are
// both correct and cheaper than a graph of unique_ptr.
//
// The cost, stated plainly: mutating a Function invalidates every
// BasicBlock*.  Blocks are therefore addressed by *label*, never by a pointer
// held across a mutation, and any pass that adds or removes blocks goes
// through setBlocks() so the label index is rebuilt.
#ifndef MTIR_CIR_FUNCTION_H
#define MTIR_CIR_FUNCTION_H

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mtir/cir/Instruction.h"

namespace mtir::cir {

class BasicBlock {
public:
  BasicBlock() = default;
  explicit BasicBlock(std::string label) : label_(std::move(label)) {}
  BasicBlock(std::string label, std::vector<Instruction> instrs)
      : label_(std::move(label)), instrs_(std::move(instrs)) {}

  /// A block's label is its identity: Function indexes by it and every
  /// terminator refers to it by name, so there is deliberately no setter.
  const std::string &label() const { return label_; }

  std::vector<Instruction> &instructions() { return instrs_; }
  const std::vector<Instruction> &instructions() const { return instrs_; }

  bool empty() const { return instrs_.empty(); }
  std::size_t size() const { return instrs_.size(); }

  void add(Instruction i) { instrs_.push_back(std::move(i)); }

  /// The block's terminator, or nullptr when the block is empty or its last
  /// instruction is not a terminator -- which is malformed IR that the
  /// verifier reports as rule 1.
  const Instruction *terminator() const;

  /// Labels this block can branch to, in terminator order, so `then` comes
  /// before `else`.  Empty when the block has no terminator.
  std::vector<std::string> successors() const;

private:
  std::string label_;
  std::vector<Instruction> instrs_;
};

bool operator==(const BasicBlock &a, const BasicBlock &b);
inline bool operator!=(const BasicBlock &a, const BasicBlock &b) { return !(a == b); }

struct Param {
  std::string name;
  Ty ty = Ty::I32;
};

inline bool operator==(const Param &a, const Param &b) {
  return a.name == b.name && a.ty == b.ty;
}

class Function {
public:
  Function() = default;
  Function(std::string name, std::vector<Param> params, Ty returnType)
      : name_(std::move(name)), params_(std::move(params)), returnType_(returnType) {}

  const std::string &name() const { return name_; }
  void setName(std::string name) { name_ = std::move(name); }

  const std::vector<Param> &params() const { return params_; }
  std::vector<Param> &params() { return params_; }

  Ty returnType() const { return returnType_; }
  void setReturnType(Ty ty) { returnType_ = ty; }

  const std::vector<BasicBlock> &blocks() const { return blocks_; }
  std::vector<BasicBlock> &blocks() { return blocks_; }

  /// Append a block and return it.  The reference is valid only until the
  /// next call that adds or removes a block.
  BasicBlock &addBlock(std::string label);
  void addBlock(BasicBlock block);

  /// Replace the whole block list, e.g. after dead-code elimination.
  void setBlocks(std::vector<BasicBlock> blocks);

  BasicBlock *block(std::string_view label);
  const BasicBlock *block(std::string_view label) const;

  BasicBlock *entry() { return blocks_.empty() ? nullptr : &blocks_.front(); }
  const BasicBlock *entry() const {
    return blocks_.empty() ? nullptr : &blocks_.front();
  }

  /// Rebuild the label index.  Call after mutating blocks() directly.
  void rebuildIndex();

private:
  std::string name_;
  std::vector<Param> params_;
  Ty returnType_ = Ty::Void;
  std::vector<BasicBlock> blocks_;
  std::unordered_map<std::string, std::size_t> index_;
};

bool operator==(const Function &a, const Function &b);
inline bool operator!=(const Function &a, const Function &b) { return !(a == b); }

} // namespace mtir::cir

#endif // MTIR_CIR_FUNCTION_H
