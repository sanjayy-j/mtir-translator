#include "mtir/backend/wasm/RegToStack.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "mtir/cir/Printer.h" // printDouble, so a float literal is spelled the
                              // same way here as in CIR text

namespace mtir::backend::wasm {
namespace {

using namespace mtir::cir;

/// WebAssembly has no 1-bit type, so i1 is realised as i32 normalised to 0/1
/// (docs/divergence.md row 5), and a pointer is an i32 offset into linear
/// memory.
std::string prefixOf(Ty t) {
  switch (t) {
  case Ty::I64:
    return "i64";
  case Ty::F64:
    return "f64";
  default:
    return "i32";
  }
}

std::string comparePredicate(Opcode op) {
  switch (op) {
  case Opcode::ICmpEq: return "eq";
  case Opcode::ICmpNe: return "ne";
  case Opcode::ICmpSlt: return "lt_s";
  case Opcode::ICmpSle: return "le_s";
  case Opcode::ICmpSgt: return "gt_s";
  case Opcode::ICmpSge: return "ge_s";
  case Opcode::ICmpUlt: return "lt_u";
  case Opcode::ICmpUle: return "le_u";
  case Opcode::ICmpUgt: return "gt_u";
  case Opcode::ICmpUge: return "ge_u";
  case Opcode::FCmpOeq: return "eq";
  case Opcode::FCmpOne: return "ne";
  case Opcode::FCmpOlt: return "lt";
  case Opcode::FCmpOle: return "le";
  case Opcode::FCmpOgt: return "gt";
  case Opcode::FCmpOge: return "ge";
  default: return "";
  }
}

std::string binaryName(Opcode op) {
  switch (op) {
  case Opcode::Add: case Opcode::FAdd: return "add";
  case Opcode::Sub: case Opcode::FSub: return "sub";
  case Opcode::Mul: case Opcode::FMul: return "mul";
  case Opcode::SDiv: return "div_s";
  case Opcode::UDiv: return "div_u";
  case Opcode::SRem: return "rem_s";
  case Opcode::URem: return "rem_u";
  case Opcode::FDiv: return "div";
  case Opcode::And: return "and";
  case Opcode::Or: return "or";
  case Opcode::Xor: return "xor";
  case Opcode::Shl: return "shl";
  case Opcode::AShr: return "shr_s";
  case Opcode::LShr: return "shr_u";
  default: return "";
  }
}

StackOp simple(std::string op) { return StackOp{std::move(op), "", 0, 0.0, false}; }

StackOp withText(std::string op, std::string text) {
  return StackOp{std::move(op), std::move(text), 0, 0.0, false};
}

const GlobalRef *asGlobalRef(const Value &v) {
  return std::holds_alternative<GlobalRef>(v) ? &std::get<GlobalRef>(v) : nullptr;
}

} // namespace

StackOp pushValue(const Value &value, const LocalTable &locals) {
  if (const ConstInt *c = asConstInt(value)) {
    StackOp op;
    op.op = prefixOf(c->ty) + ".const";
    op.intArg = c->value;
    op.hasArg = true;
    return op;
  }
  if (const ConstFloat *c = asConstFloat(value)) {
    StackOp op;
    op.op = "f64.const";
    op.floatArg = c->value;
    op.hasArg = true;
    return op;
  }
  if (const Reg *r = asRegister(value)) {
    const auto it = locals.find(r->name);
    return withText("local.get", it == locals.end() ? "$" + r->name : it->second);
  }
  if (std::holds_alternative<GlobalRef>(value))
    return withText("global.get", "$" + std::get<GlobalRef>(value).name);
  return simple("unreachable");
}

std::string StackOp::toString() const {
  if (!hasArg && text.empty())
    return op;
  if (hasArg) {
    if (op == "f64.const")
      return op + " " + cir::printDouble(floatArg);
    return op + " " + std::to_string(intArg);
  }
  return op + " " + text;
}

bool operator==(const StackOp &a, const StackOp &b) {
  return a.op == b.op && a.text == b.text && a.hasArg == b.hasArg &&
         (!a.hasArg || (a.intArg == b.intArg && a.floatArg == b.floatArg));
}

LocalTable localTable(const cir::Function &fn) {
  LocalTable table;
  for (const Param &p : fn.params())
    table[p.name] = "$" + p.name;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions())
      if (i.dest.has_value())
        table.emplace(i.dest->name, "$" + i.dest->name);
  return table;
}

StackOp opcodeFor(const Instruction &instr) {
  const std::string p = prefixOf(instr.ty);

  switch (instr.op) {
  case Opcode::Ret:
    return simple("return");
  case Opcode::Br:
    return withText("br", instr.labels.empty() ? "" : instr.labels[0]);
  case Opcode::BrCond:
    return withText("br_if", instr.labels.empty() ? "" : instr.labels[0]);
  case Opcode::Call:
    return withText("call", "$" + instr.callee);
  case Opcode::PrintI32:
    return withText("call", "$print_i32");
  case Opcode::PrintF64:
    return withText("call", "$print_f64");
  case Opcode::Trap:
    return simple("unreachable");
  case Opcode::Load:
    return simple(p + ".load");
  case Opcode::Store:
    return simple(p + ".store");
  case Opcode::Neg:
    return simple(instr.ty == Ty::F64 ? "f64.neg" : "i32.sub");
  case Opcode::SExt:
    return simple("i64.extend_i32_s");
  case Opcode::ZExt:
    return simple(instr.ty == Ty::I64 ? "i64.extend_i32_u" : "nop");
  case Opcode::Trunc:
    return simple("i32.wrap_i64");
  case Opcode::SIToFP:
    return simple("f64.convert_i32_s");
  case Opcode::FPToSI:
    // Deliberately trunc_f64_s, not trunc_sat: divergence.md row 6 says CIR
    // traps out of range, and trunc_sat would silently saturate instead.
    return simple(instr.ty == Ty::I64 ? "i64.trunc_f64_s" : "i32.trunc_f64_s");
  case Opcode::Alloca:
    // Needs a frame; see lowerInstruction.
    return simple("unreachable");
  case Opcode::GEP:
    return simple("i32.add");
  case Opcode::Not:
    return simple(p + ".xor");
  default:
    break;
  }

  if (isCompare(instr.op))
    return simple(p + "." + comparePredicate(instr.op));

  const std::string binary = binaryName(instr.op);
  if (!binary.empty())
    return simple(p + "." + binary);

  return simple("unreachable");
}

std::int64_t sizeOf(Ty t) {
  switch (t) {
  case Ty::I64:
  case Ty::F64:
    return 8;
  default:
    // i32, ptr, and i1 -- which occupies a full slot because it travels as an
    // i32 (docs/divergence.md row 5).
    return 4;
  }
}

std::vector<StackOp> lowerInstruction(const Instruction &instr,
                                      const LocalTable &locals) {
  std::vector<StackOp> out;
  const std::string p = prefixOf(instr.ty);

  switch (instr.op) {
  case Opcode::Alloca:
    // The WebAssembly emitter rewrites these away before lowering; reaching
    // here means it did not, and there is no honest single opcode to emit.
    out.push_back(simple("unreachable"));
    break;

  case Opcode::Load:
    // A CIR global is a WebAssembly global, not a region of linear memory, so
    // reading one is global.get rather than a load through an address.
    if (instr.args.size() == 1) {
      if (const GlobalRef *g = asGlobalRef(instr.args[0])) {
        out.push_back(withText("global.get", "$" + g->name));
        break;
      }
      out.push_back(pushValue(instr.args[0], locals));
    }
    out.push_back(simple(p + ".load"));
    break;

  case Opcode::Store:
    // CIR: `store ty value, ptr`.  WebAssembly: address, then value -- the
    // opposite order, so the two operands are swapped rather than pushed as
    // they appear.
    if (instr.args.size() == 2) {
      if (const GlobalRef *g = asGlobalRef(instr.args[1])) {
        out.push_back(pushValue(instr.args[0], locals));
        out.push_back(withText("global.set", "$" + g->name));
        break;
      }
      out.push_back(pushValue(instr.args[1], locals));
      out.push_back(pushValue(instr.args[0], locals));
    }
    out.push_back(simple(p + ".store"));
    break;

  case Opcode::GEP:
    // address = base + index * sizeof(element)
    if (instr.args.size() == 2) {
      out.push_back(pushValue(instr.args[0], locals));
      out.push_back(pushValue(instr.args[1], locals));
      StackOp scale;
      scale.op = "i32.const";
      scale.intArg = sizeOf(instr.ty);
      scale.hasArg = true;
      out.push_back(scale);
      out.push_back(simple("i32.mul"));
    }
    out.push_back(simple("i32.add"));
    break;

  case Opcode::Neg:
    if (instr.ty == Ty::F64) {
      for (const Value &operand : instr.args)
        out.push_back(pushValue(operand, locals));
      out.push_back(simple("f64.neg"));
    } else {
      // No integer negate in WebAssembly; `0 - x` needs the zero underneath.
      StackOp zero;
      zero.op = p + ".const";
      zero.intArg = 0;
      zero.hasArg = true;
      out.push_back(zero);
      for (const Value &operand : instr.args)
        out.push_back(pushValue(operand, locals));
      out.push_back(simple(p + ".sub"));
    }
    break;

  case Opcode::Not: {
    // CIR's `not` is unary; the LLVM back end spells it `xor x, -1` (`xor x,
    // true` for i1), and the same mask keeps an i1 normalised to 0/1 here.
    for (const Value &operand : instr.args)
      out.push_back(pushValue(operand, locals));
    StackOp mask;
    mask.op = p + ".const";
    mask.intArg = instr.ty == Ty::I1 ? 1 : -1;
    mask.hasArg = true;
    out.push_back(mask);
    out.push_back(simple(p + ".xor"));
    break;
  }

  default:
    for (const Value &operand : instr.args)
      out.push_back(pushValue(operand, locals));
    out.push_back(opcodeFor(instr));
    break;
  }

  if (instr.dest.has_value()) {
    const auto it = locals.find(instr.dest->name);
    out.push_back(withText("local.set", it == locals.end() ? "$" + instr.dest->name
                                                           : it->second));
  }
  return out;
}

std::vector<StackOp> lowerBlockNaive(const BasicBlock &block, const LocalTable &locals) {
  std::vector<StackOp> out;
  for (const Instruction &instr : block.instructions()) {
    const std::vector<StackOp> seq = lowerInstruction(instr, locals);
    out.insert(out.end(), seq.begin(), seq.end());
  }
  return out;
}

std::map<std::string, int> blockUseCounts(const BasicBlock &block,
                                          const LocalTable &locals) {
  std::map<std::string, int> counts;
  for (const Instruction &instr : block.instructions())
    for (const Value &operand : instr.args)
      if (const Reg *r = asRegister(operand)) {
        const auto it = locals.find(r->name);
        if (it != locals.end())
          ++counts[it->second];
      }
  return counts;
}

std::set<std::string> blockLiveOut(const Function &fn, const BasicBlock &block,
                                   const LocalTable &locals) {
  std::set<std::string> live;
  for (const BasicBlock &other : fn.blocks()) {
    if (&other == &block)
      continue;
    for (const Instruction &instr : other.instructions())
      for (const Value &operand : instr.args)
        if (const Reg *r = asRegister(operand)) {
          const auto it = locals.find(r->name);
          if (it != locals.end())
            live.insert(it->second);
        }
  }
  return live;
}

std::vector<StackOp> peephole(const std::vector<StackOp> &seq,
                              const std::map<std::string, int> &useCount,
                              const std::set<std::string> &liveOut) {
  std::vector<StackOp> out;
  std::size_t i = 0;
  while (i < seq.size()) {
    const StackOp &cur = seq[i];
    const StackOp *next = i + 1 < seq.size() ? &seq[i + 1] : nullptr;

    // Correctness condition: the local is read exactly once in the whole
    // block and is not live on exit, so nothing else can observe the value in
    // the local rather than on the stack.
    const auto count = useCount.find(cur.text);
    if (next != nullptr && cur.op == "local.set" && next->op == "local.get" &&
        cur.text == next->text && count != useCount.end() && count->second == 1 &&
        liveOut.count(cur.text) == 0) {
      i += 2; // drop both; the value simply stays on the stack
      continue;
    }
    out.push_back(cur);
    ++i;
  }
  return out;
}

std::vector<StackOp> lowerBlock(const BasicBlock &block, const LocalTable &locals,
                                const std::map<std::string, int> &useCount,
                                const std::set<std::string> &liveOut) {
  return peephole(lowerBlockNaive(block, locals), useCount, liveOut);
}

std::vector<std::vector<StackOp>> lowerFunction(const Function &fn) {
  const LocalTable locals = localTable(fn);
  std::vector<std::vector<StackOp>> out;
  out.reserve(fn.blocks().size());
  for (const BasicBlock &b : fn.blocks())
    out.push_back(lowerBlock(b, locals, blockUseCounts(b, locals),
                             blockLiveOut(fn, b, locals)));
  return out;
}

std::pair<std::size_t, std::size_t> countFunction(const Function &fn) {
  const LocalTable locals = localTable(fn);
  std::size_t naive = 0;
  for (const BasicBlock &b : fn.blocks())
    naive += lowerBlockNaive(b, locals).size();

  std::size_t tuned = 0;
  for (const std::vector<StackOp> &seq : lowerFunction(fn))
    tuned += seq.size();
  return {naive, tuned};
}

} // namespace mtir::backend::wasm
