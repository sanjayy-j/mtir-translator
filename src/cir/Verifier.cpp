#include "mtir/cir/Verifier.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mtir/cir/CFG.h"

namespace mtir::cir {
namespace {

using support::Diagnostic;
using support::Diagnostics;
using support::Location;

Location at(const Function &fn) {
  Location l;
  l.function = fn.name();
  return l;
}

Location at(const Function &fn, const BasicBlock &block, int index = -1) {
  Location l;
  l.function = fn.name();
  l.block = block.label();
  l.instrIndex = index;
  return l;
}

void report(Diagnostics &out, std::string code, std::string message, Location loc) {
  out.push_back(support::error(std::move(code), std::move(message), std::move(loc)));
}

std::string tyName(Ty t) { return std::string(toString(t)); }

// --------------------------------------------------------------------------
// A fixed-width bitset over registers, used by the rule 4 dataflow.  Meet is
// a word-wise AND, which is why the registers are numbered densely first.
// --------------------------------------------------------------------------
class BitSet {
public:
  BitSet() = default;
  BitSet(std::size_t bits, bool value)
      : words_((bits + 63) / 64, value ? ~std::uint64_t{0} : std::uint64_t{0}),
        bits_(bits) {
    trim();
  }

  void set(std::size_t i) { words_[i / 64] |= (std::uint64_t{1} << (i % 64)); }
  bool test(std::size_t i) const {
    return (words_[i / 64] >> (i % 64)) & std::uint64_t{1};
  }

  void intersectWith(const BitSet &other) {
    for (std::size_t i = 0; i < words_.size(); ++i)
      words_[i] &= other.words_[i];
  }

  bool operator==(const BitSet &other) const { return words_ == other.words_; }
  bool operator!=(const BitSet &other) const { return !(*this == other); }

private:
  // Bits past the end must stay zero or equality comparison misbehaves.
  void trim() {
    const std::size_t rem = bits_ % 64;
    if (rem != 0 && !words_.empty())
      words_.back() &= (std::uint64_t{1} << rem) - 1;
  }

  std::vector<std::uint64_t> words_;
  std::size_t bits_ = 0;
};

} // namespace

// ==========================================================================
// Rules 1, 2, 3, 5 -- the control-flow graph
// ==========================================================================
Diagnostics checkCFG(const Function &fn) {
  Diagnostics out;

  if (fn.blocks().empty()) {
    report(out, "CIR00", "function has no blocks", at(fn));
    return out;
  }

  std::unordered_set<std::string> seen;
  for (const BasicBlock &block : fn.blocks()) {
    if (!seen.insert(block.label()).second)
      report(out, "CIR09", "duplicate block label '" + block.label() + "'",
             at(fn, block));
  }

  for (const BasicBlock &block : fn.blocks()) {
    // Rule 1: exactly one terminator, and it is last.
    if (block.terminator() == nullptr)
      report(out, "CIR01", "block does not end in a terminator", at(fn, block));

    // Rule 2: nothing follows a terminator.
    const std::vector<Instruction> &instrs = block.instructions();
    for (std::size_t i = 0; i + 1 < instrs.size(); ++i) {
      if (instrs[i].isTerminator()) {
        report(out, "CIR02",
               "instruction '" + std::string(mnemonic(instrs[i].op)) +
                   "' appears after a terminator",
               at(fn, block, static_cast<int>(i)));
      }
    }

    // Rule 3: every branch target names a block in this function.
    for (const std::string &target : block.successors()) {
      if (seen.find(target) == seen.end())
        report(out, "CIR03", "branch to undefined block '" + target + "'",
               at(fn, block));
    }
  }

  // Rule 5: the entry block has no predecessors, so it cannot be re-entered.
  const CFG cfg(fn);
  const BlockId entry = cfg.entry();
  for (BlockId pred : cfg.predecessors(entry)) {
    report(out, "CIR05",
           "entry block '" + cfg.label(entry) + "' has a predecessor '" +
               cfg.label(pred) + "'",
           at(fn));
  }

  return out;
}

// ==========================================================================
// Rule 4 -- every register is defined on every path that reaches its use
// ==========================================================================
Diagnostics checkDefinitions(const Function &fn) {
  Diagnostics out;
  if (fn.blocks().empty())
    return out;

  // Number every register densely: parameters first, then every destination.
  std::unordered_map<std::string, std::size_t> regId;
  for (const Param &p : fn.params())
    regId.emplace(p.name, regId.size());
  for (const BasicBlock &block : fn.blocks())
    for (const Instruction &i : block.instructions())
      if (i.dest.has_value())
        regId.emplace(i.dest->name, regId.size());

  const std::size_t nregs = regId.size();
  const CFG cfg(fn);
  const std::size_t nblocks = cfg.size();

  // in[b] = registers defined on *every* path into b.  The entry block starts
  // from the parameters; every other reachable block starts at the top of the
  // lattice (all registers), which is what makes a loop back edge converge to
  // the right answer instead of a false positive.
  BitSet params(nregs, false);
  for (const Param &p : fn.params())
    params.set(regId[p.name]);

  std::vector<BitSet> in(nblocks, BitSet(nregs, true));
  in[cfg.entry()] = params;

  const auto transfer = [&](BlockId id, const BitSet &entrySet) {
    BitSet outSet = entrySet;
    for (const Instruction &i : fn.blocks()[id].instructions())
      if (i.dest.has_value())
        outSet.set(regId[i.dest->name]);
    return outSet;
  };

  // Reverse postorder converges in far fewer rounds than definition order.
  bool changed = true;
  while (changed) {
    changed = false;
    for (BlockId id : cfg.reversePostorder()) {
      if (id == cfg.entry())
        continue;
      bool first = true;
      BitSet meet(nregs, true);
      for (BlockId pred : cfg.predecessors(id)) {
        if (!cfg.isReachable(pred))
          continue;
        BitSet contribution = transfer(pred, in[pred]);
        if (first) {
          meet = contribution;
          first = false;
        } else {
          meet.intersectWith(contribution);
        }
      }
      if (first)
        meet = BitSet(nregs, false); // reachable but with no reachable pred
      if (meet != in[id]) {
        in[id] = meet;
        changed = true;
      }
    }
  }

  for (BlockId id : cfg.reversePostorder()) {
    const BasicBlock &block = fn.blocks()[id];
    BitSet available = in[id];
    for (std::size_t k = 0; k < block.instructions().size(); ++k) {
      const Instruction &instr = block.instructions()[k];
      for (const Value &arg : instr.args) {
        const Reg *r = asRegister(arg);
        if (r == nullptr)
          continue;
        const auto it = regId.find(r->name);
        if (it == regId.end() || !available.test(it->second)) {
          report(out, "CIR04",
                 "use of %" + r->name + " before it is defined on every path",
                 at(fn, block, static_cast<int>(k)));
        }
      }
      if (instr.dest.has_value())
        available.set(regId[instr.dest->name]);
    }
  }

  return out;
}

// ==========================================================================
// Rules 6, 7, 8 -- operand and result types
// ==========================================================================
namespace {

void checkInstruction(const Function &fn, const BasicBlock &block, std::size_t index,
                      const Instruction &instr, const Module *module,
                      Diagnostics &out) {
  const Location loc = at(fn, block, static_cast<int>(index));
  const OpInfo &meta = info(instr.op);
  const std::string name(meta.mnemonic);

  // Arity, where the opcode fixes it.
  if (meta.arity != kVariadic && instr.args.size() != meta.arity) {
    report(out, "CIR06",
           name + " takes " + std::to_string(meta.arity) + " operand(s), " +
               std::to_string(instr.args.size()) + " given",
           loc);
    return;
  }

  // Rule 7 as a special case of "the destination has the type the opcode
  // says it has".
  if (instr.dest.has_value()) {
    const Ty expected = instr.resultType();
    if (instr.dest->ty != expected) {
      const char *code = isCompare(instr.op) ? "CIR07" : "CIR06";
      report(out, code,
             name + " defines %" + instr.dest->name + " as " +
                 tyName(instr.dest->ty) + ", expected " + tyName(expected),
             loc);
    }
  } else if (meta.resultRule != ResultRule::None && instr.op != Opcode::Call) {
    report(out, "CIR06", name + " must define a register", loc);
  }

  const auto argTy = [&](std::size_t i) { return typeOf(instr.args[i]); };
  const auto requireArg = [&](std::size_t i, Ty want) {
    if (argTy(i) != want)
      report(out, "CIR06",
             name + " operand " + std::to_string(i) + " is " + tyName(argTy(i)) +
                 ", expected " + tyName(want),
             loc);
  };
  const auto requireIntArg = [&](std::size_t i) {
    if (!isInteger(argTy(i)))
      report(out, "CIR06",
             name + " operand " + std::to_string(i) + " is " + tyName(argTy(i)) +
                 ", expected an integer",
             loc);
  };

  switch (instr.op) {
  case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
  case Opcode::SDiv: case Opcode::UDiv: case Opcode::SRem: case Opcode::URem:
  case Opcode::And: case Opcode::Or: case Opcode::Xor:
  case Opcode::Shl: case Opcode::AShr: case Opcode::LShr:
    if (!isInteger(instr.ty))
      report(out, "CIR06", name + " requires an integer type, found " + tyName(instr.ty),
             loc);
    requireArg(0, instr.ty);
    requireArg(1, instr.ty);
    break;

  case Opcode::FAdd: case Opcode::FSub: case Opcode::FMul: case Opcode::FDiv:
    if (instr.ty != Ty::F64)
      report(out, "CIR06", name + " requires f64, found " + tyName(instr.ty), loc);
    requireArg(0, instr.ty);
    requireArg(1, instr.ty);
    break;

  case Opcode::Neg:
    if (!isInteger(instr.ty) && !isFloat(instr.ty))
      report(out, "CIR06", name + " requires a numeric type, found " + tyName(instr.ty),
             loc);
    requireArg(0, instr.ty);
    break;

  case Opcode::Not:
    if (!isInteger(instr.ty))
      report(out, "CIR06", name + " requires an integer type, found " + tyName(instr.ty),
             loc);
    requireArg(0, instr.ty);
    break;

  case Opcode::ICmpEq: case Opcode::ICmpNe:
  case Opcode::ICmpSlt: case Opcode::ICmpSle:
  case Opcode::ICmpSgt: case Opcode::ICmpSge:
  case Opcode::ICmpUlt: case Opcode::ICmpUle:
  case Opcode::ICmpUgt: case Opcode::ICmpUge:
    if (!isInteger(instr.ty) && instr.ty != Ty::Ptr)
      report(out, "CIR06",
             name + " compares integers or pointers, found " + tyName(instr.ty), loc);
    requireArg(0, instr.ty);
    requireArg(1, instr.ty);
    break;

  case Opcode::FCmpOeq: case Opcode::FCmpOne: case Opcode::FCmpOlt:
  case Opcode::FCmpOle: case Opcode::FCmpOgt: case Opcode::FCmpOge:
    if (instr.ty != Ty::F64)
      report(out, "CIR06", name + " compares f64, found " + tyName(instr.ty), loc);
    requireArg(0, Ty::F64);
    requireArg(1, Ty::F64);
    break;

  case Opcode::SExt: case Opcode::ZExt: case Opcode::Trunc:
    if (!isInteger(instr.ty))
      report(out, "CIR06", name + " produces an integer, found " + tyName(instr.ty), loc);
    requireIntArg(0);
    break;

  case Opcode::SIToFP:
    if (instr.ty != Ty::F64)
      report(out, "CIR06", name + " produces f64, found " + tyName(instr.ty), loc);
    requireIntArg(0);
    break;

  case Opcode::FPToSI:
    if (!isInteger(instr.ty))
      report(out, "CIR06", name + " produces an integer, found " + tyName(instr.ty), loc);
    requireArg(0, Ty::F64);
    break;

  case Opcode::Alloca:
    if (instr.args.size() > 1)
      report(out, "CIR06", "alloca takes at most one element count", loc);
    else if (instr.args.size() == 1)
      requireIntArg(0);
    if (instr.ty == Ty::Void)
      report(out, "CIR06", "alloca requires an element type", loc);
    break;

  case Opcode::Load:
    requireArg(0, Ty::Ptr);
    if (instr.ty == Ty::Void)
      report(out, "CIR06", "load requires a value type", loc);
    break;

  case Opcode::Store:
    requireArg(0, instr.ty);
    requireArg(1, Ty::Ptr);
    break;

  case Opcode::GEP:
    requireArg(0, Ty::Ptr);
    requireIntArg(1);
    break;

  case Opcode::Call: {
    if (instr.callee.empty()) {
      report(out, "CIR06", "call has no callee", loc);
      break;
    }
    if ((instr.ty == Ty::Void) == instr.dest.has_value()) {
      report(out, "CIR06",
             instr.ty == Ty::Void
                 ? "a call to a void function must not define a register"
                 : "a call returning a value must define a register",
             loc);
    }
    if (module != nullptr) {
      const Function *callee = module->function(instr.callee);
      if (callee == nullptr) {
        report(out, "CIR06", "call to undefined function @" + instr.callee, loc);
      } else {
        if (callee->params().size() != instr.args.size()) {
          report(out, "CIR06",
                 "@" + instr.callee + " takes " +
                     std::to_string(callee->params().size()) + " argument(s), " +
                     std::to_string(instr.args.size()) + " given",
                 loc);
        }
        if (callee->returnType() != instr.ty) {
          report(out, "CIR06",
                 "@" + instr.callee + " returns " + tyName(callee->returnType()) +
                     ", call says " + tyName(instr.ty),
                 loc);
        }
      }
    }
    break;
  }

  case Opcode::PrintI32:
    requireArg(0, Ty::I32);
    break;
  case Opcode::PrintF64:
    requireArg(0, Ty::F64);
    break;
  case Opcode::Trap:
    break;

  case Opcode::Br:
    if (instr.labels.size() != 1)
      report(out, "CIR06", "br takes exactly one target label", loc);
    break;

  case Opcode::BrCond:
    requireArg(0, Ty::I1);
    if (instr.labels.size() != 2)
      report(out, "CIR06", "br.cond takes exactly two target labels", loc);
    break;

  case Opcode::Ret:
    // Rule 8.  Every block ends in a terminator (rule 1) and the only
    // terminators are br, br.cond and ret, so every exit from the function is
    // a ret -- which makes "returns on every path" equivalent to "every ret
    // carries the declared type".
    if (fn.returnType() == Ty::Void) {
      if (!instr.args.empty())
        report(out, "CIR08", "ret with a value in a function returning void", loc);
    } else if (instr.args.empty()) {
      report(out, "CIR08",
             "ret without a value in a function returning " + tyName(fn.returnType()),
             loc);
    } else if (instr.ty != fn.returnType()) {
      report(out, "CIR08",
             "ret " + tyName(instr.ty) + " in a function returning " +
                 tyName(fn.returnType()),
             loc);
    } else {
      requireArg(0, fn.returnType());
    }
    break;

  case Opcode::Count:
    report(out, "CIR06", "invalid opcode", loc);
    break;
  }
}

} // namespace

Diagnostics checkTypes(const Function &fn, const Module *module) {
  Diagnostics out;
  if (fn.blocks().empty() && fn.returnType() != Ty::Void)
    report(out, "CIR08", "function has no blocks and cannot return a value", at(fn));

  for (const BasicBlock &block : fn.blocks())
    for (std::size_t i = 0; i < block.instructions().size(); ++i)
      checkInstruction(fn, block, i, block.instructions()[i], module, out);

  return out;
}

Diagnostics verifyFunction(const Function &fn, const Module *module) {
  Diagnostics out = checkCFG(fn);
  if (!out.empty())
    return out;

  Diagnostics defs = checkDefinitions(fn);
  Diagnostics types = checkTypes(fn, module);
  out.insert(out.end(), defs.begin(), defs.end());
  out.insert(out.end(), types.begin(), types.end());
  return out;
}

Diagnostics verify(const Module &module) {
  Diagnostics out;
  for (const Function &fn : module.functions()) {
    Diagnostics fnDiags = verifyFunction(fn, &module);
    out.insert(out.end(), fnDiags.begin(), fnDiags.end());
  }
  return out;
}

} // namespace mtir::cir
