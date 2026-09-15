#include "mtir/opt/Passes.h"

#include <cmath>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mtir/cir/Arith.h"

namespace mtir::opt {
namespace {

using namespace mtir::cir;

bool allConstant(const std::vector<Value> &args) {
  for (const Value &a : args)
    if (!isConstant(a))
      return false;
  return true;
}

bool anyNaN(const std::vector<Value> &args) {
  for (const Value &a : args)
    if (const ConstFloat *f = asConstFloat(a))
      if (std::isnan(f->value))
        return true;
  return false;
}

std::int64_t intOf(const Value &v) {
  if (const ConstInt *c = asConstInt(v))
    return c->value;
  if (const ConstFloat *f = asConstFloat(v))
    return static_cast<std::int64_t>(f->value);
  return 0;
}

double floatOf(const Value &v) {
  if (const ConstFloat *f = asConstFloat(v))
    return f->value;
  if (const ConstInt *c = asConstInt(v))
    return static_cast<double>(c->value);
  return 0.0;
}

Value boolValue(bool b) { return Value{ConstInt{b ? 1 : 0, Ty::I1}}; }

std::optional<Value> foldIntCompare(Opcode op, std::int64_t raw_a,
                                    std::int64_t raw_b, Ty t) {
  // Compare the values as the type sees them, not as raw 64-bit integers.
  const std::int64_t a = wrapInt(raw_a, t);
  const std::int64_t b = wrapInt(raw_b, t);
  const std::uint64_t ua = toUnsigned(a, t);
  const std::uint64_t ub = toUnsigned(b, t);
  switch (op) {
  case Opcode::ICmpEq: return boolValue(a == b);
  case Opcode::ICmpNe: return boolValue(a != b);
  case Opcode::ICmpSlt: return boolValue(a < b);
  case Opcode::ICmpSle: return boolValue(a <= b);
  case Opcode::ICmpSgt: return boolValue(a > b);
  case Opcode::ICmpSge: return boolValue(a >= b);
  // The unsigned predicates are the signed ones applied to the operands
  // reinterpreted as unsigned, which is what the hardware does.
  case Opcode::ICmpUlt: return boolValue(ua < ub);
  case Opcode::ICmpUle: return boolValue(ua <= ub);
  case Opcode::ICmpUgt: return boolValue(ua > ub);
  case Opcode::ICmpUge: return boolValue(ua >= ub);
  default: return std::nullopt;
  }
}

std::optional<Value> foldFloatCompare(Opcode op, double a, double b) {
  switch (op) {
  case Opcode::FCmpOeq: return boolValue(a == b);
  case Opcode::FCmpOne: return boolValue(a != b);
  case Opcode::FCmpOlt: return boolValue(a < b);
  case Opcode::FCmpOle: return boolValue(a <= b);
  case Opcode::FCmpOgt: return boolValue(a > b);
  case Opcode::FCmpOge: return boolValue(a >= b);
  default: return std::nullopt;
  }
}

/// All integer arithmetic runs in the unsigned domain: signed overflow is
/// undefined behaviour in C++, so computing `a + b` on int64_t and hoping is
/// not an option for a compiler that defines overflow as wraparound.
std::int64_t addWrapped(std::int64_t a, std::int64_t b, Ty t) {
  return wrapInt(static_cast<std::int64_t>(static_cast<std::uint64_t>(a) +
                                           static_cast<std::uint64_t>(b)),
                 t);
}
std::int64_t subWrapped(std::int64_t a, std::int64_t b, Ty t) {
  return wrapInt(static_cast<std::int64_t>(static_cast<std::uint64_t>(a) -
                                           static_cast<std::uint64_t>(b)),
                 t);
}
std::int64_t mulWrapped(std::int64_t a, std::int64_t b, Ty t) {
  return wrapInt(static_cast<std::int64_t>(static_cast<std::uint64_t>(a) *
                                           static_cast<std::uint64_t>(b)),
                 t);
}

} // namespace

std::optional<Value> foldInstruction(const Instruction &instr) {
  const std::vector<Value> &args = instr.args;
  if (args.empty() || !allConstant(args) || anyNaN(args))
    return std::nullopt;

  const Ty t = instr.ty;
  const Opcode op = instr.op;

  if (isCompare(op) && args.size() == 2) {
    if (op >= Opcode::FCmpOeq && op <= Opcode::FCmpOge)
      return foldFloatCompare(op, floatOf(args[0]), floatOf(args[1]));
    return foldIntCompare(op, intOf(args[0]), intOf(args[1]), t);
  }

  if (args.size() == 2 && isInteger(t)) {
    const std::int64_t a = intOf(args[0]);
    const std::int64_t b = intOf(args[1]);
    switch (op) {
    case Opcode::Add: return Value{ConstInt{addWrapped(a, b, t), t}};
    case Opcode::Sub: return Value{ConstInt{subWrapped(a, b, t), t}};
    case Opcode::Mul: return Value{ConstInt{mulWrapped(a, b, t), t}};
    case Opcode::And: return Value{ConstInt{wrapInt(a & b, t), t}};
    case Opcode::Or: return Value{ConstInt{wrapInt(a | b, t), t}};
    case Opcode::Xor: return Value{ConstInt{wrapInt(a ^ b, t), t}};
    case Opcode::Shl: {
      const unsigned n = maskShift(b, t); // row 3
      const auto shifted = static_cast<std::int64_t>(toUnsigned(a, t) << n);
      return Value{ConstInt{wrapInt(shifted, t), t}};
    }
    case Opcode::AShr: {
      const unsigned n = maskShift(b, t);
      // Arithmetic shift, spelled out rather than relying on the
      // implementation-defined behaviour of >> on a negative value.
      const std::int64_t wide = wrapInt(a, t);
      const std::int64_t result = wide < 0 ? ~((~wide) >> n) : (wide >> n);
      return Value{ConstInt{wrapInt(result, t), t}};
    }
    case Opcode::LShr: {
      const unsigned n = maskShift(b, t);
      return Value{ConstInt{wrapInt(static_cast<std::int64_t>(toUnsigned(a, t) >> n), t), t}};
    }
    case Opcode::SDiv:
    case Opcode::SRem:
    case Opcode::UDiv:
    case Opcode::URem: {
      const bool isSigned = op == Opcode::SDiv || op == Opcode::SRem;
      // rows 1 and 2: leave a trapping division alone so the back end's guard
      // still fires.
      if (divTraps(isSigned, a, b, t))
        return std::nullopt;
      const bool isRem = op == Opcode::SRem || op == Opcode::URem;
      return Value{ConstInt{isRem ? remTrunc(isSigned, a, b, t)
                                  : divTrunc(isSigned, a, b, t),
                            t}};
    }
    default:
      break;
    }
  }

  if (args.size() == 2 && t == Ty::F64) {
    const double a = floatOf(args[0]);
    const double b = floatOf(args[1]);
    switch (op) {
    case Opcode::FAdd: return Value{ConstFloat{a + b}};
    case Opcode::FSub: return Value{ConstFloat{a - b}};
    case Opcode::FMul: return Value{ConstFloat{a * b}};
    // FDiv is deliberately absent: CIR does not fix what float division by
    // zero means, so folding it would invent semantics.
    default: break;
    }
  }

  if (args.size() == 1) {
    switch (op) {
    case Opcode::Not:
      if (isInteger(t))
        return Value{ConstInt{wrapInt(~intOf(args[0]), t), t}};
      break;
    case Opcode::Neg:
      if (t == Ty::F64)
        return Value{ConstFloat{-floatOf(args[0])}};
      if (isInteger(t))
        return Value{ConstInt{subWrapped(0, intOf(args[0]), t), t}};
      break;
    case Opcode::SExt:
    case Opcode::Trunc:
      return Value{ConstInt{wrapInt(intOf(args[0]), t), t}};
    case Opcode::ZExt:
      return Value{ConstInt{
          wrapInt(static_cast<std::int64_t>(toUnsigned(intOf(args[0]), typeOf(args[0]))), t), t}};
    case Opcode::SIToFP:
      return Value{ConstFloat{static_cast<double>(intOf(args[0]))}};
    // FPToSI is absent: it traps out of range (row 6), so folding it would
    // need the same range analysis the back end's guard performs.
    default:
      break;
    }
  }

  return std::nullopt;
}

namespace {

/// Registers defined exactly once in the function.
std::unordered_map<std::string, int> definitionCounts(const Function &fn) {
  std::unordered_map<std::string, int> counts;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions())
      if (i.dest.has_value())
        ++counts[i.dest->name];
  return counts;
}

bool singleDefinition(const std::unordered_map<std::string, int> &counts,
                      const std::string &name) {
  const auto it = counts.find(name);
  return it != counts.end() && it->second == 1;
}

} // namespace

bool ConstantFoldingPass::runOnFunction(Function &fn) {
  const auto counts = definitionCounts(fn);
  bool changed = false;

  for (;;) {
    // -- phase A: analysis only.  Nothing is deleted while uses may remain.
    std::unordered_map<std::string, Value> known;
    for (const BasicBlock &block : fn.blocks()) {
      for (const Instruction &instr : block.instructions()) {
        if (!instr.dest.has_value() || !singleDefinition(counts, instr.dest->name))
          continue;
        Instruction probe = instr;
        for (Value &arg : probe.args)
          if (const Reg *r = asRegister(arg)) {
            const auto it = known.find(r->name);
            if (it != known.end())
              arg = it->second;
          }
        if (std::optional<Value> folded = foldInstruction(probe))
          known.emplace(instr.dest->name, *folded);
      }
    }

    // Phase B runs even when nothing folded: a br.cond may already have a
    // constant condition (a literal in the source, or a substitution from an
    // earlier round), and rewriting it is the transformation that lets
    // dead-code elimination drop the arm it no longer reaches.
    //
    // -- phase B: rewrite every use, then drop the definitions.
    bool round = false;
    for (BasicBlock &block : fn.blocks()) {
      std::vector<Instruction> kept;
      kept.reserve(block.instructions().size());
      for (Instruction &instr : block.instructions()) {
        for (Value &arg : instr.args) {
          if (const Reg *r = asRegister(arg)) {
            const auto it = known.find(r->name);
            if (it != known.end()) {
              arg = it->second;
              round = true;
            }
          }
        }
        if (instr.dest.has_value() && known.count(instr.dest->name) != 0) {
          round = true;
          continue;
        }
        kept.push_back(std::move(instr));
      }
      block.instructions() = std::move(kept);

      // A br.cond on a constant becomes an unconditional br.  This is what
      // removes the dead arm of `while (false)`; DCE then drops the block the
      // branch no longer reaches.
      const Instruction *term = block.terminator();
      if (term != nullptr && term->op == Opcode::BrCond) {
        if (const ConstInt *c = asConstInt(term->args[0])) {
          const std::string taken =
              (c->value & 1) != 0 ? term->labels[0] : term->labels[1];
          block.instructions().back() = Instruction::br(taken);
          round = true;
        }
      }
    }

    changed = changed || round;
    if (!round)
      return changed;
  }
}

} // namespace mtir::opt
