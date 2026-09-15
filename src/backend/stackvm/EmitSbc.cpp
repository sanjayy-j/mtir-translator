#include "mtir/backend/stackvm/EmitSbc.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mtir/backend/wasm/RegToStack.h"
#include "mtir/cir/Arith.h"
#include "mtir/cir/Printer.h"

namespace mtir::backend::stackvm {
namespace {

// No `using namespace mtir::cir` here: this namespace has its own
// Instruction and Function, and the two sets must stay visibly distinct.
using backend::wasm::FrameLayout;
using backend::wasm::LocalTable;
using backend::wasm::StackOp;

/// The register the rewritten allocas read.  The VM keeps the frame base of
/// the active call, so this resolves to a local the prologue never has to
/// compute.
const char *const kFrameReg = "__frame";

Instruction simple(std::string mnemonic) {
  Instruction i;
  i.mnemonic = std::move(mnemonic);
  return i;
}

Instruction withTarget(std::string mnemonic, std::int64_t target) {
  Instruction i;
  i.mnemonic = std::move(mnemonic);
  i.imm = target;
  i.hasImm = true;
  return i;
}

/// A StackOp carried over unchanged from the register-to-stack pass.
Instruction fromStackOp(const StackOp &op) {
  Instruction i;
  i.mnemonic = op.op;
  i.text = op.text;
  i.imm = op.intArg;
  i.fimm = op.floatArg;
  i.hasImm = op.hasArg;
  return i;
}

class FunctionEmitter {
public:
  FunctionEmitter(const cir::Function &fn, support::Diagnostics &diags)
      : fn_(fn), diags_(diags) {}

  Function emit();

private:
  void error(const std::string &block, std::string message) {
    support::Location loc;
    loc.function = fn_.name();
    loc.block = block;
    diags_.push_back(support::error("SBC01", std::move(message), loc));
  }

  /// Index of `label` in fn_.blocks(), or blocks().size() if absent.
  std::size_t blockIndex(const std::string &label) const {
    for (std::size_t i = 0; i < fn_.blocks().size(); ++i)
      if (fn_.blocks()[i].label() == label)
        return i;
    return fn_.blocks().size();
  }

  const cir::Function &fn_;
  support::Diagnostics &diags_;
};

/// Append a branch, unless it is an unconditional jump to the block that
/// comes next anyway -- control reaches that block by falling through, so the
/// jump is pure overhead.  Targets are still block indices at this point;
/// pass 2 turns them into addresses.
void emitJump(const char *mnemonic, std::size_t target, std::size_t currentBlock,
              Function &out, std::vector<std::size_t> &branchAt) {
  const bool unconditional = std::string(mnemonic) == "jmp";
  if (unconditional && target == currentBlock + 1)
    return;
  branchAt.push_back(out.code.size());
  out.code.push_back(withTarget(mnemonic, static_cast<std::int64_t>(target)));
}

Function FunctionEmitter::emit() {
  Function out;
  out.name = fn_.name();
  out.returnType = fn_.returnType();
  for (const cir::Param &p : fn_.params())
    out.paramTypes.push_back(p.ty);

  const FrameLayout frame = backend::wasm::layoutFrame(fn_);
  out.frameSize = frame.size;

  LocalTable locals = backend::wasm::localTable(fn_);
  if (frame.size > 0)
    locals[kFrameReg] = std::string("$") + kFrameReg;

  // Pass 1: lower each block, leaving branch targets as block indices.  The
  // instruction address of each block is only known once every earlier block
  // has been laid out, so the targets are patched in pass 2.
  std::vector<std::size_t> blockStart(fn_.blocks().size(), 0);
  std::vector<std::size_t> branchAt; // positions holding a block index

  for (std::size_t b = 0; b < fn_.blocks().size(); ++b) {
    const cir::BasicBlock &block = fn_.blocks()[b];
    blockStart[b] = out.code.size();

    const cir::Instruction *term = block.terminator();
    if (term == nullptr) {
      error(block.label(), "block has no terminator");
      out.code.push_back(simple("trap"));
      continue;
    }

    // `ret` lowers to `return` through the ordinary schema and needs no
    // rewriting; `br` and `br.cond` become absolute jumps, so they are
    // stripped and re-emitted below.
    const bool keepTerminator = term->op == cir::Opcode::Ret;

    const cir::BasicBlock resolved =
        frame.size > 0 ? backend::wasm::resolveAllocas(block, frame, kFrameReg) : block;

    cir::BasicBlock lowerable(block.label());
    const std::vector<cir::Instruction> &instrs = resolved.instructions();
    for (std::size_t i = 0; i < instrs.size(); ++i) {
      if (!keepTerminator && i + 1 == instrs.size() && instrs[i].isTerminator())
        break;
      lowerable.add(instrs[i]);
    }

    std::vector<StackOp> seq = backend::wasm::lowerBlockNaive(lowerable, locals);

    // A conditional branch pushes its condition, and that push has to be in
    // the sequence the peephole sees or the condition register is needlessly
    // parked in a local.
    if (term->op == cir::Opcode::BrCond && !term->args.empty())
      seq.push_back(backend::wasm::pushValue(term->args[0], locals));

    seq = backend::wasm::peephole(seq, backend::wasm::blockUseCounts(resolved, locals),
                                  backend::wasm::blockLiveOut(fn_, block, locals));

    for (const StackOp &op : seq)
      out.code.push_back(fromStackOp(op));

    switch (term->op) {
    case cir::Opcode::Ret:
      break; // already emitted as `return`

    case cir::Opcode::Br:
      emitJump("jmp", blockIndex(term->labels[0]), b, out, branchAt);
      break;

    case cir::Opcode::BrCond:
      // The condition is on the stack.  `jz` takes the false edge; falling
      // through takes the true edge, which is then an unconditional jump.
      // Two instructions where WebAssembly needs an if/else and a
      // re-dispatch: this is the structuring cost, made visible -- so the
      // fall-through jump is elided where it can be, to keep the comparison
      // about structuring rather than about jumps nobody had to emit.
      emitJump("jz", blockIndex(term->labels[1]), b, out, branchAt);
      emitJump("jmp", blockIndex(term->labels[0]), b, out, branchAt);
      break;

    default:
      error(block.label(), "unexpected terminator");
      out.code.push_back(simple("trap"));
      break;
    }
  }

  // Pass 2: block index -> absolute instruction address.
  for (const std::size_t at : branchAt) {
    const std::size_t index = static_cast<std::size_t>(out.code[at].imm);
    if (index >= blockStart.size()) {
      error(fn_.name(), "branch to an unknown label");
      out.code[at] = simple("trap");
      continue;
    }
    out.code[at].imm = static_cast<std::int64_t>(blockStart[index]);
  }

  // Locals: parameters first and in order, so a call binds arguments by
  // position, then whatever else the code actually reads or writes.  Anything
  // the peephole kept on the stack never appears and needs no slot.
  std::set<std::string> used;
  for (const Instruction &i : out.code)
    if (i.mnemonic == "local.get" || i.mnemonic == "local.set")
      used.insert(i.text);

  for (const cir::Param &p : fn_.params()) {
    out.locals.push_back("$" + p.name);
    used.erase("$" + p.name);
  }
  if (frame.size > 0) {
    out.locals.push_back(std::string("$") + kFrameReg);
    used.erase(std::string("$") + kFrameReg);
  }
  for (const std::string &name : used)
    out.locals.push_back(name);

  // Resolve each local reference to its slot, keeping the name for the
  // listing.  std::set iterates in order, so the numbering is deterministic.
  std::map<std::string, std::int64_t> slotOf;
  for (std::size_t i = 0; i < out.locals.size(); ++i)
    slotOf[out.locals[i]] = static_cast<std::int64_t>(i);

  for (Instruction &i : out.code) {
    if (i.mnemonic != "local.get" && i.mnemonic != "local.set")
      continue;
    const auto it = slotOf.find(i.text);
    if (it == slotOf.end()) {
      error(fn_.name(), "reference to a local that was never declared: " + i.text);
      continue;
    }
    i.imm = it->second;
    i.hasImm = true;
  }

  return out;
}

std::string tyName(cir::Ty t) { return std::string(cir::toString(t)); }

} // namespace

bool operator==(const Instruction &a, const Instruction &b) {
  return a.mnemonic == b.mnemonic && a.text == b.text && a.hasImm == b.hasImm &&
         (!a.hasImm || (a.imm == b.imm && a.fimm == b.fimm));
}

const Function *Program::function(const std::string &name) const {
  for (const Function &fn : functions)
    if (fn.name == name)
      return &fn;
  return nullptr;
}

EmitResult emitModule(const cir::Module &module) {
  EmitResult result;
  result.program.moduleName = module.name();
  result.program.globals = module.globals();

  for (const cir::Function &fn : module.functions()) {
    FunctionEmitter emitter(fn, result.diagnostics);
    result.program.functions.push_back(emitter.emit());
  }
  return result;
}

std::size_t instructionCount(const Program &program) {
  std::size_t n = 0;
  for (const Function &fn : program.functions)
    n += fn.code.size();
  return n;
}

std::string printProgram(const Program &program) {
  std::string out = ";; sbc v1 -- CIR module '" + program.moduleName +
                    "' lowered by the MTIR stack bytecode back end (M7)\n";

  for (const cir::Global &g : program.globals) {
    out += ".global " + g.name + " " + tyName(g.ty);
    if (g.arrayLen.has_value())
      out += "[" + std::to_string(*g.arrayLen) + "]";
    if (g.init.has_value())
      out += " = " + cir::printValue(*g.init);
    out += "\n";
  }
  if (!program.globals.empty())
    out += "\n";

  for (std::size_t f = 0; f < program.functions.size(); ++f) {
    const Function &fn = program.functions[f];
    if (f != 0)
      out += "\n";

    out += ".func " + fn.name + "(";
    for (std::size_t i = 0; i < fn.paramTypes.size(); ++i) {
      if (i != 0)
        out += ", ";
      out += tyName(fn.paramTypes[i]);
    }
    out += ") -> " + tyName(fn.returnType) + "\n";

    out += "  .frame " + std::to_string(fn.frameSize) + "\n";
    for (std::size_t i = 0; i < fn.locals.size(); ++i)
      out += "  .local " + std::to_string(i) + " " + fn.locals[i] + "\n";

    for (std::size_t i = 0; i < fn.code.size(); ++i) {
      const Instruction &instr = fn.code[i];

      // Addresses are printed because a branch target is an absolute index:
      // without them the listing cannot be followed.
      std::string address = std::to_string(i);
      while (address.size() < 4)
        address = "0" + address;
      out += "  " + address + "  " + instr.mnemonic;

      if (instr.mnemonic == "local.get" || instr.mnemonic == "local.set")
        out += " " + std::to_string(instr.imm) + "  ;; " + instr.text;
      else if (instr.mnemonic == "jmp" || instr.mnemonic == "jz")
        out += " " + std::to_string(instr.imm);
      else if (instr.mnemonic == "f64.const")
        out += " " + cir::printDouble(instr.fimm);
      else if (instr.hasImm)
        out += " " + std::to_string(instr.imm);
      else if (!instr.text.empty())
        out += " " + instr.text;

      out += "\n";
    }
    out += ".end\n";
  }
  return out;
}

} // namespace mtir::backend::stackvm
