#include "mtir/opt/Passes.h"

#include <unordered_map>
#include <utility>
#include <vector>

#include "mtir/cir/Arith.h"

namespace mtir::opt {
namespace {

using namespace mtir::cir;

/// Which operand may be the identity, and what value it must have.
struct Identity {
  std::int64_t unit;
  bool commutative; ///< true when the identity may be on either side
};

std::optional<Identity> identityFor(Opcode op) {
  switch (op) {
  case Opcode::Add: return Identity{0, true};
  case Opcode::Or: return Identity{0, true};
  case Opcode::Xor: return Identity{0, true};
  case Opcode::Sub: return Identity{0, false};
  case Opcode::Shl: return Identity{0, false};
  case Opcode::AShr: return Identity{0, false};
  case Opcode::LShr: return Identity{0, false};
  case Opcode::Mul: return Identity{1, true};
  case Opcode::SDiv: return Identity{1, false};
  case Opcode::UDiv: return Identity{1, false};
  case Opcode::And: return Identity{-1, true};
  default: return std::nullopt;
  }
}

std::unordered_map<std::string, int> definitionCounts(const Function &fn) {
  std::unordered_map<std::string, int> counts;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions())
      if (i.dest.has_value())
        ++counts[i.dest->name];
  return counts;
}

} // namespace

std::optional<Value> copySource(const Instruction &instr) {
  if (!instr.dest.has_value() || instr.args.size() != 2)
    return std::nullopt;
  // Float identities are not exact: fadd %x, 0.0 turns -0.0 into +0.0.
  if (!isInteger(instr.ty))
    return std::nullopt;

  const std::optional<Identity> identity = identityFor(instr.op);
  if (!identity)
    return std::nullopt;

  const std::int64_t unit = wrapInt(identity->unit, instr.ty);
  const Value &lhs = instr.args[0];
  const Value &rhs = instr.args[1];

  if (const ConstInt *c = asConstInt(rhs))
    if (wrapInt(c->value, instr.ty) == unit)
      return lhs;

  if (identity->commutative)
    if (const ConstInt *c = asConstInt(lhs))
      if (wrapInt(c->value, instr.ty) == unit)
        return rhs;

  return std::nullopt;
}

bool CopyPropagationPass::runOnFunction(Function &fn) {
  const auto counts = definitionCounts(fn);
  bool changed = false;

  for (;;) {
    std::unordered_map<std::string, Value> aliases;
    for (const BasicBlock &block : fn.blocks()) {
      for (const Instruction &instr : block.instructions()) {
        if (!instr.dest.has_value())
          continue;
        const auto it = counts.find(instr.dest->name);
        if (it == counts.end() || it->second != 1)
          continue; // not single-assignment: substituting would be unsound
        std::optional<Value> source = copySource(instr);
        if (!source)
          continue;
        // Chase through an alias already found, so a chain of identities
        // collapses in one round.
        while (const Reg *r = asRegister(*source)) {
          const auto alias = aliases.find(r->name);
          if (alias == aliases.end())
            break;
          source = alias->second;
        }
        if (const Reg *r = asRegister(*source))
          if (r->name == instr.dest->name)
            continue; // self-alias: nothing to gain, and a cycle to avoid
        aliases.emplace(instr.dest->name, *source);
      }
    }

    if (aliases.empty())
      return changed;

    bool round = false;
    for (BasicBlock &block : fn.blocks()) {
      std::vector<Instruction> kept;
      kept.reserve(block.instructions().size());
      for (Instruction &instr : block.instructions()) {
        for (Value &arg : instr.args) {
          if (const Reg *r = asRegister(arg)) {
            const auto it = aliases.find(r->name);
            if (it != aliases.end()) {
              arg = it->second;
              round = true;
            }
          }
        }
        if (instr.dest.has_value() && aliases.count(instr.dest->name) != 0) {
          // Every use has been rewritten and none of these opcodes has a side
          // effect, so the definition can go now rather than waiting for DCE.
          round = true;
          continue;
        }
        kept.push_back(std::move(instr));
      }
      block.instructions() = std::move(kept);
    }

    changed = changed || round;
    if (!round)
      return changed;
  }
}

} // namespace mtir::opt
