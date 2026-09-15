#include "mtir/backend/llvm/EmitLL.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mtir/backend/llvm/Guards.h"
#include "mtir/backend/llvm/TypeMap.h"
#include "mtir/cir/Arith.h"
#include "mtir/cir/Printer.h"

namespace mtir::backend::llvm {
namespace {

using namespace mtir::cir;
using support::Diagnostics;
using support::Location;

std::string ty(Ty t) { return std::string(llvmType(t)); }

/// The LLVM instruction for a CIR opcode that is a straight rename.
const char *binaryOpName(Opcode op) {
  switch (op) {
  case Opcode::Add: return "add";
  case Opcode::Sub: return "sub";
  case Opcode::Mul: return "mul";
  case Opcode::SDiv: return "sdiv";
  case Opcode::UDiv: return "udiv";
  case Opcode::SRem: return "srem";
  case Opcode::URem: return "urem";
  case Opcode::FAdd: return "fadd";
  case Opcode::FSub: return "fsub";
  case Opcode::FMul: return "fmul";
  case Opcode::FDiv: return "fdiv";
  case Opcode::And: return "and";
  case Opcode::Or: return "or";
  case Opcode::Xor: return "xor";
  case Opcode::Shl: return "shl";
  case Opcode::AShr: return "ashr";
  case Opcode::LShr: return "lshr";
  default: return nullptr;
  }
}

const char *conversionOpName(Opcode op) {
  switch (op) {
  case Opcode::SExt: return "sext";
  case Opcode::ZExt: return "zext";
  case Opcode::Trunc: return "trunc";
  case Opcode::SIToFP: return "sitofp";
  case Opcode::FPToSI: return "fptosi";
  default: return nullptr;
  }
}

bool isDivision(Opcode op) {
  return op == Opcode::SDiv || op == Opcode::UDiv || op == Opcode::SRem ||
         op == Opcode::URem;
}

bool isShift(Opcode op) {
  return op == Opcode::Shl || op == Opcode::AShr || op == Opcode::LShr;
}

bool isIntCompare(Opcode op) {
  return op >= Opcode::ICmpEq && op <= Opcode::ICmpUge;
}

/// A double in LLVM's spelling.
///
/// This is *not* cir::printDouble.  The two formats agree on finite values but
/// not on the rest: CIR text spells them `inf`, `-inf` and `nan`, which its own
/// parser reads back, whereas LLVM has no such literal and requires the raw
/// 64-bit pattern.  Emitting `double inf` produces a file llvm-as rejects, and
/// constant folding really can reach it -- `fmul f64 1.0e308, 10.0` overflows.
std::string llvmDouble(double value) {
  if (std::isnan(value) || std::isinf(value)) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double must be 64 bits");
    std::memcpy(&bits, &value, sizeof(bits));
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%016llX",
                  static_cast<unsigned long long>(bits));
    return buffer;
  }
  return printDouble(value);
}

/// One operand in LLVM spelling.  `context` is the type LLVM will read the
/// operand as, which is what turns an i1 constant into true/false and wraps
/// an out-of-range integer literal into its type (docs/divergence.md row 4).
std::string llvmConstant(const Value &v, Ty context) {
  struct Visitor {
    Ty context;
    std::string operator()(const Reg &r) const { return "%" + r.name; }
    std::string operator()(const GlobalRef &g) const { return "@" + g.name; }
    std::string operator()(const ConstFloat &c) const { return llvmDouble(c.value); }
    std::string operator()(const ConstInt &c) const {
      if (context == Ty::I1)
        return (c.value & 1) != 0 ? "true" : "false";
      if (context == Ty::F64)
        return llvmDouble(static_cast<double>(c.value));
      if (isInteger(context))
        return std::to_string(wrapInt(c.value, context));
      return std::to_string(c.value);
    }
  };
  return std::visit(Visitor{context}, v);
}

// --------------------------------------------------------------------------
// The emitter
// --------------------------------------------------------------------------
class FunctionEmitter final : public Emitter {
public:
  FunctionEmitter(const Module &module, std::set<std::string> &needs,
                  Diagnostics &diags)
      : module_(module), needs_(needs), diags_(diags) {}

  std::string emit(const Function &fn);

  // -- Emitter interface -------------------------------------------------
  std::string tmp() override { return "%g." + std::to_string(++counter_); }

  void line(std::string_view text) override {
    out_.push_back("  " + std::string(text));
  }

  void trapBranch(std::string_view cond, std::string_view kind) override {
    needs_.insert("trap");
    const std::string suffix = std::to_string(++counter_);
    const std::string trapLabel = "trap." + std::string(kind) + "." + suffix;
    const std::string contLabel = "cont." + std::string(kind) + "." + suffix;
    line("br i1 " + std::string(cond) + ", label %" + trapLabel + ", label %" +
         contLabel);
    label(trapLabel);
    line("call void @llvm.trap()");
    line("unreachable");
    label(contLabel);
  }

private:
  void label(const std::string &name) { out_.push_back(name + ":"); }

  void error(const Function &fn, const BasicBlock &block, std::string message) {
    Location loc;
    loc.function = fn.name();
    loc.block = block.label();
    diags_.push_back(support::error("LLVM01", std::move(message), loc));
  }

  std::string value(const Value &v, Ty context) const;
  void instruction(const Function &fn, const BasicBlock &block,
                   const Instruction &instr);
  void call(const Function &fn, const BasicBlock &block, const Instruction &instr);

  const Module &module_;
  std::set<std::string> &needs_;
  Diagnostics &diags_;
  std::vector<std::string> out_;
  std::unordered_map<std::string, std::int64_t> extents_;
  unsigned counter_ = 0;
};

std::string FunctionEmitter::value(const Value &v, Ty context) const {
  return llvmConstant(v, context);
}

std::string FunctionEmitter::emit(const Function &fn) {
  out_.clear();
  counter_ = 0;
  extents_.clear();

  // Array extents recovered from the allocas in this function, so a gep off a
  // sized alloca can be bounds-checked (docs/divergence.md row 7).
  for (const BasicBlock &block : fn.blocks())
    for (const Instruction &instr : block.instructions())
      if (instr.op == Opcode::Alloca && instr.dest.has_value() && !instr.args.empty())
        if (const ConstInt *c = asConstInt(instr.args[0]))
          extents_[instr.dest->name] = c->value;

  std::string params;
  for (std::size_t i = 0; i < fn.params().size(); ++i) {
    if (i != 0)
      params += ", ";
    params += ty(fn.params()[i].ty) + " %" + fn.params()[i].name;
  }
  const std::string head =
      "define " + ty(fn.returnType()) + " @" + fn.name() + "(" + params + ") {";

  for (const BasicBlock &block : fn.blocks()) {
    label(block.label());
    for (const Instruction &instr : block.instructions())
      instruction(fn, block, instr);
  }

  std::string body = head;
  for (const std::string &l : out_) {
    body += "\n";
    body += l;
  }
  body += "\n}";
  return body;
}

void FunctionEmitter::call(const Function &fn, const BasicBlock &block,
                           const Instruction &instr) {
  const Function *callee = module_.function(instr.callee);
  std::vector<Ty> paramTys;
  if (callee != nullptr) {
    for (const Param &p : callee->params())
      paramTys.push_back(p.ty);
  } else {
    for (const Value &a : instr.args)
      paramTys.push_back(typeOf(a));
  }

  if (paramTys.size() != instr.args.size()) {
    error(fn, block,
          "@" + instr.callee + " takes " + std::to_string(paramTys.size()) +
              " argument(s), " + std::to_string(instr.args.size()) + " given");
    return;
  }

  std::string args;
  for (std::size_t i = 0; i < instr.args.size(); ++i) {
    if (i != 0)
      args += ", ";
    args += ty(paramTys[i]) + " " + value(instr.args[i], paramTys[i]);
  }

  const std::string text =
      "call " + ty(instr.ty) + " @" + instr.callee + "(" + args + ")";
  line(instr.dest.has_value() ? "%" + instr.dest->name + " = " + text : text);
}

void FunctionEmitter::instruction(const Function &fn, const BasicBlock &block,
                                  const Instruction &instr) {
  const std::string dest =
      instr.dest.has_value() ? "%" + instr.dest->name : std::string();

  std::vector<std::string> args;
  args.reserve(instr.args.size());
  for (const Value &a : instr.args)
    args.push_back(value(a, instr.ty));

  switch (instr.op) {
  case Opcode::Br:
    line("br label %" + instr.labels[0]);
    return;

  case Opcode::BrCond:
    line("br i1 " + args[0] + ", label %" + instr.labels[0] + ", label %" +
         instr.labels[1]);
    return;

  case Opcode::Ret:
    line(instr.args.empty() ? "ret void" : "ret " + ty(instr.ty) + " " + args[0]);
    return;

  case Opcode::Trap:
    // 'trap' is not a CIR terminator, so no 'unreachable' here: the block's
    // own terminator still follows it.  llvm.trap is noreturn, so control
    // never actually reaches past it -- but emitting a terminator mid-block
    // would produce IR that llvm-as rejects.
    needs_.insert("trap");
    line("call void @llvm.trap()");
    return;

  case Opcode::PrintI32:
    needs_.insert("printf");
    line("call i32 (ptr, ...) @printf(ptr @.fmt.i32, i32 " + args[0] + ")");
    return;

  case Opcode::PrintF64:
    needs_.insert("printf");
    line("call i32 (ptr, ...) @printf(ptr @.fmt.f64, double " + args[0] + ")");
    return;

  case Opcode::Call:
    call(fn, block, instr);
    return;

  case Opcode::Alloca:
    line(dest + " = alloca " + ty(instr.ty) +
         (instr.args.empty() ? "" : ", i32 " + args[0]));
    return;

  case Opcode::Load:
    line(dest + " = load " + ty(instr.ty) + ", ptr " + args[0]);
    return;

  case Opcode::Store:
    line("store " + ty(instr.ty) + " " + args[0] + ", ptr " + args[1]);
    return;

  case Opcode::GEP: {
    const std::string indexText = value(instr.args[1], Ty::I32);
    if (const Reg *base = asRegister(instr.args[0])) {
      const auto it = extents_.find(base->name);
      if (it != extents_.end())
        guardBounds(*this, it->second, instr.args[1], indexText);
    }
    line(dest + " = getelementptr " + ty(instr.ty) + ", ptr " + args[0] + ", i32 " +
         indexText);
    return;
  }

  case Opcode::Neg:
    if (instr.ty == Ty::F64)
      line(dest + " = fneg double " + args[0]);
    else
      // LLVM has no integer negate; CIR's 'neg' becomes 0 - x, which is what
      // the builder emits directly for integers anyway.
      line(dest + " = sub " + ty(instr.ty) + " 0, " + args[0]);
    return;

  case Opcode::Not:
    line(dest + " = xor " + ty(instr.ty) + " " + args[0] + ", " +
         (instr.ty == Ty::I1 ? "true" : "-1"));
    return;

  default:
    break;
  }

  if (isCompare(instr.op)) {
    // CIR prints the *operand* type on a comparison; the result is i1.
    const char *kind = isIntCompare(instr.op) ? "icmp" : "fcmp";
    line(dest + " = " + kind + " " + std::string(info(instr.op).predicate) + " " +
         ty(instr.ty) + " " + args[0] + ", " + args[1]);
    return;
  }

  if (const char *conv = conversionOpName(instr.op)) {
    const Ty sourceTy = typeOf(instr.args[0]);
    // Refuse rather than emit: a conversion with the wrong result type would
    // produce text like `fptosi double %x to double`, which llvm-as rejects,
    // and would drive the fptosi guard's range computation with a width of
    // zero.  Better one diagnostic than an invalid .ll file.
    const bool wantsIntResult =
        instr.op == Opcode::FPToSI || instr.op == Opcode::SExt ||
        instr.op == Opcode::ZExt || instr.op == Opcode::Trunc;
    if (wantsIntResult && !isInteger(instr.ty)) {
      error(fn, block, std::string(mnemonic(instr.op)) +
                           " must produce an integer, found " +
                           std::string(toString(instr.ty)));
      return;
    }
    if (instr.op == Opcode::SIToFP && instr.ty != Ty::F64) {
      error(fn, block, "sitofp must produce f64, found " +
                           std::string(toString(instr.ty)));
      return;
    }
    if (instr.op == Opcode::FPToSI && sourceTy != Ty::F64) {
      error(fn, block, "fptosi takes an f64 operand, found " +
                           std::string(toString(sourceTy)));
      return;
    }
    if (instr.op == Opcode::FPToSI)
      guardFPToSI(*this, instr.ty, args[0]);
    line(dest + " = " + conv + " " + ty(sourceTy) + " " + args[0] + " to " +
         ty(instr.ty));
    return;
  }

  if (const char *bin = binaryOpName(instr.op)) {
    // Same reasoning: a shift or an integer division on a non-integer type is
    // malformed CIR, and emitting it would produce invalid LLVM and a bogus
    // shift mask.
    if ((isShift(instr.op) || isDivision(instr.op)) && !isInteger(instr.ty)) {
      error(fn, block, std::string(mnemonic(instr.op)) +
                           " requires an integer type, found " +
                           std::string(toString(instr.ty)));
      return;
    }
    std::string rhs = args[1];
    if (isDivision(instr.op))
      guardDivision(*this, instr.op, instr.ty, args[0], instr.args[1], rhs);
    else if (isShift(instr.op))
      rhs = maskShiftCount(*this, instr.ty, instr.args[1], rhs);
    // Deliberately no 'nsw'/'nuw': CIR defines signed overflow as wraparound
    // (docs/divergence.md row 4), and those flags would make it poison.
    line(dest + " = " + bin + " " + ty(instr.ty) + " " + args[0] + ", " + rhs);
    return;
  }

  error(fn, block,
        "no LLVM lowering for CIR opcode '" + std::string(mnemonic(instr.op)) + "'");
}

// --------------------------------------------------------------------------
// Runtime declarations, emitted only when something used them
// --------------------------------------------------------------------------
void appendRuntime(const std::set<std::string> &needs, std::vector<std::string> &parts) {
  std::vector<std::string> lines;
  if (needs.count("printf") != 0) {
    lines.emplace_back("declare i32 @printf(ptr, ...)");
    lines.emplace_back(R"(@.fmt.i32 = private unnamed_addr constant [4 x i8] c"%d\0A\00")");
    lines.emplace_back(R"(@.fmt.f64 = private unnamed_addr constant [4 x i8] c"%f\0A\00")");
  }
  if (needs.count("trap") != 0)
    lines.emplace_back("declare void @llvm.trap()");

  if (lines.empty())
    return;
  parts.emplace_back();
  for (std::string &l : lines)
    parts.push_back(std::move(l));
}

} // namespace

EmitResult emitModule(const Module &module) {
  EmitResult result;
  // std::set, not unordered_set: the runtime block is emitted from it, and
  // output must not depend on hash order.
  std::set<std::string> needs;

  FunctionEmitter emitter(module, needs, result.diagnostics);

  std::vector<std::string> bodies;
  bodies.reserve(module.functions().size());
  for (const Function &fn : module.functions())
    bodies.push_back(emitter.emit(fn));

  std::vector<std::string> parts;
  // The header line is shared verbatim with the Python reference
  // implementation, so docs/examples/abs.gen.ll is a golden file for both and
  // any divergence between them shows up as a test failure.
  parts.push_back("; CIR module '" + module.name() +
                  "' lowered by the MTIR LLVM back end (M5)");

  for (const Global &g : module.globals()) {
    const std::string gty = std::string(llvmType(g.ty));
    if (g.arrayLen.has_value()) {
      parts.push_back("@" + g.name + " = global [" + std::to_string(*g.arrayLen) +
                      " x " + gty + "] zeroinitializer");
    } else if (g.init.has_value()) {
      parts.push_back("@" + g.name + " = global " + gty + " " +
                      llvmConstant(*g.init, g.ty));
    } else {
      parts.push_back("@" + g.name + " = global " + gty + " zeroinitializer");
    }
  }
  if (!module.globals().empty())
    parts.emplace_back();

  for (std::string &body : bodies)
    parts.push_back(std::move(body));

  appendRuntime(needs, parts);

  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0)
      result.ir += "\n";
    result.ir += parts[i];
  }
  result.ir += "\n";
  return result;
}

} // namespace mtir::backend::llvm
