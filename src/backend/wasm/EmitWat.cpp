#include "mtir/backend/wasm/EmitWat.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mtir/backend/wasm/RegToStack.h"
#include "mtir/cir/Arith.h"
#include "mtir/cir/CFG.h"
#include "mtir/cir/Printer.h"

namespace mtir::backend::wasm {
namespace {

using namespace mtir::cir;

std::string watType(Ty t) {
  switch (t) {
  case Ty::I64:
    return "i64";
  case Ty::F64:
    return "f64";
  default:
    // i1 has no WebAssembly counterpart and is normalised to i32 (row 5);
    // ptr is an i32 offset into linear memory.
    return "i32";
  }
}

std::string indent(int depth) { return std::string(static_cast<std::size_t>(depth) * 2, ' '); }

class FunctionEmitter {
public:
  explicit FunctionEmitter(support::Diagnostics &diags) : diags_(diags) {}

  std::string emit(const Function &fn);

private:
  void line(int depth, const std::string &text) {
    out_ += indent(depth);
    out_ += text;
    out_ += '\n';
  }

  /// Emit a stack op and remember any local it touches, so that only locals
  /// which survived the peephole get declared.
  void emitOp(int depth, const StackOp &op) {
    if (op.op == "local.get" || op.op == "local.set")
      usedLocals_.insert(op.text);
    line(depth, op.toString());
  }

  /// The dispatch tower, without the function header or local declarations.
  void emitTower(const Function &fn, const LocalTable &locals);

  /// One CIR block: its straight-line instructions in stack form, followed by
  /// the control-flow tail that re-enters the dispatch tower.
  void emitBlock(const Function &fn, const BasicBlock &block,
                 const LocalTable &locals, int depth);

  void error(const Function &fn, const BasicBlock &block, std::string message) {
    support::Location loc;
    loc.function = fn.name();
    loc.block = block.label();
    diags_.push_back(support::error("WASM01", std::move(message), loc));
  }

  std::size_t indexOf(const Function &fn, const std::string &label) const {
    for (std::size_t i = 0; i < fn.blocks().size(); ++i)
      if (fn.blocks()[i].label() == label)
        return i;
    return 0;
  }

  /// Byte offset of each alloca'd pointer register within the frame, and the
  /// total frame size.  Both are recomputed per function.
  std::map<std::string, std::int64_t> frameOffset_;
  std::int64_t frameSize_ = 0;

  /// Lay out the function's allocas in its stack frame.
  void computeFrame(const Function &fn);

  /// The block with every alloca replaced by frame-relative address
  /// arithmetic, so the ordinary lowering can handle it.
  BasicBlock withAllocasResolved(const Function &fn, const BasicBlock &block);

  support::Diagnostics &diags_;
  std::string out_;
  std::set<std::string> usedLocals_;
};

/// Top of linear memory, and so the initial shadow stack pointer.  One page.
constexpr std::int64_t kStackTop = 65536;

/// The shadow stack grows down from kStackTop; frames are kept 8-byte aligned
/// so that an i64 or f64 slot is naturally aligned.
constexpr std::int64_t kFrameAlign = 8;

std::int64_t roundUp(std::int64_t n, std::int64_t multiple) {
  return ((n + multiple - 1) / multiple) * multiple;
}

void FunctionEmitter::computeFrame(const Function &fn) {
  frameOffset_.clear();
  frameSize_ = 0;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions()) {
      if (i.op != Opcode::Alloca || !i.dest.has_value())
        continue;
      const std::int64_t elem = sizeOf(i.ty);
      std::int64_t count = 1;
      if (!i.args.empty())
        if (const ConstInt *c = asConstInt(i.args[0]))
          count = c->value > 0 ? c->value : 1;
      frameSize_ = roundUp(frameSize_, elem);
      frameOffset_[i.dest->name] = frameSize_;
      frameSize_ += elem * count;
    }
  frameSize_ = roundUp(frameSize_, kFrameAlign);
}

BasicBlock FunctionEmitter::withAllocasResolved(const Function &fn,
                                                const BasicBlock &block) {
  BasicBlock out(block.label());
  for (const Instruction &i : block.instructions()) {
    if (i.op != Opcode::Alloca || !i.dest.has_value()) {
      // A gep off a global would need the global to live in linear memory,
      // and a CIR global is a WebAssembly global.  Refuse rather than emit
      // an address that is really a value.
      if (i.op == Opcode::GEP && !i.args.empty() &&
          std::holds_alternative<GlobalRef>(i.args[0]))
        error(fn, block,
              "cannot index a global array: a CIR global becomes a WebAssembly "
              "global, which has no address in linear memory");
      out.add(i);
      continue;
    }
    // `%p = alloca T` becomes `%p = add i32 $__frame, offset`, which the
    // ordinary lowering, and the peephole with it, already understands.
    const auto it = frameOffset_.find(i.dest->name);
    const std::int64_t offset = it == frameOffset_.end() ? 0 : it->second;
    out.add(Instruction::binary(Opcode::Add, Ty::I32, *i.dest,
                                Reg{"__frame", Ty::Ptr},
                                ConstInt{offset, Ty::I32}));
  }
  return out;
}

void FunctionEmitter::emitBlock(const Function &fn, const BasicBlock &block,
                                const LocalTable &locals, int depth) {
  const Instruction *term = block.terminator();
  if (term == nullptr) {
    error(fn, block, "block has no terminator");
    line(depth, "unreachable");
    return;
  }

  // `ret` is the one terminator whose stack form is already right: opcodeFor
  // maps it to `return`, which is what WebAssembly wants.  `br` and `br_cond`
  // name CIR labels, which do not exist here, so the block is lowered without
  // its terminator and the tail is written by hand below.
  const bool terminatorLowersDirectly = term->op == Opcode::Ret;

  const BasicBlock resolved = withAllocasResolved(fn, block);
  BasicBlock lowerable(block.label());
  const std::vector<Instruction> &instrs = resolved.instructions();
  for (std::size_t i = 0; i < instrs.size(); ++i) {
    if (!terminatorLowersDirectly && i + 1 == instrs.size() && instrs[i].isTerminator())
      break;
    lowerable.add(instrs[i]);
  }

  std::vector<StackOp> seq = lowerBlockNaive(lowerable, locals);

  // A conditional branch still has to push its condition, and that push must
  // be part of the sequence the peephole sees: otherwise the register holding
  // the condition looks unused here and is needlessly parked in a local.
  if (term->op == Opcode::BrCond && !term->args.empty())
    seq.push_back(pushValue(term->args[0], locals));

  // Use counts are taken over the whole block, terminator included, so the
  // pushes appended above are accounted for.  Live-out is over the rest of the
  // function: a value another block reads can never stay on the operand stack
  // across the edge.
  for (const StackOp &op :
       peephole(seq, blockUseCounts(resolved, locals), blockLiveOut(fn, block, locals))) {
    // Every exit restores the caller's shadow stack pointer.  The pair is
    // stack-neutral, so inserting it under an already-pushed result is safe.
    if (op.op == "return" && frameSize_ > 0) {
      line(depth, "local.get $__fp");
      line(depth, "global.set $__sp");
    }
    emitOp(depth, op);
  }

  switch (term->op) {
  case Opcode::Ret:
    return; // already emitted, `return` and all

  case Opcode::Br:
    line(depth, "i32.const " + std::to_string(indexOf(fn, term->labels[0])));
    line(depth, "local.set $__block");
    line(depth, "br $dispatch");
    return;

  case Opcode::BrCond:
    // The condition is on the stack; select a block index with it, park that
    // in $__block and re-dispatch.
    line(depth, "if (result i32)");
    line(depth + 1, "i32.const " + std::to_string(indexOf(fn, term->labels[0])));
    line(depth, "else");
    line(depth + 1, "i32.const " + std::to_string(indexOf(fn, term->labels[1])));
    line(depth, "end");
    line(depth, "local.set $__block");
    line(depth, "br $dispatch");
    return;

  default:
    error(fn, block, "unexpected terminator");
    line(depth, "unreachable");
    return;
  }
}

void FunctionEmitter::emitTower(const Function &fn, const LocalTable &locals) {
  const std::size_t n = fn.blocks().size();

  if (frameSize_ > 0) {
    // Open a stack frame: remember the caller's shadow stack pointer, drop
    // the frame below it, and keep the frame base in a local so that each
    // alloca is one add away and a call in between cannot disturb it.
    line(2, "global.get $__sp");
    line(2, "local.set $__fp");
    line(2, "local.get $__fp");
    line(2, "i32.const " + std::to_string(frameSize_));
    line(2, "i32.sub");
    line(2, "local.set $__frame");
    line(2, "local.get $__frame");
    line(2, "global.set $__sp");
  }

  line(2, "i32.const 0");
  line(2, "local.set $__block");
  line(2, "(block $exit");
  line(3, "(loop $dispatch");

  // Outermost first: $case{n-1} down to $case0, so that falling out of
  // $caseK lands exactly at CIR block K.
  for (std::size_t i = 0; i < n; ++i)
    line(4 + static_cast<int>(i), "(block $case" + std::to_string(n - 1 - i));

  // br_table's last label is its default target, so listing $case0..$case{n-1}
  // gives index i -> $case_i for every i < n-1 and sends n-1 (and anything
  // out of range, which cannot occur) to $case{n-1}.  That is exactly the
  // mapping wanted, with no separate default entry needed.
  std::string table = "br_table";
  for (std::size_t i = 0; i < n; ++i)
    table += " $case" + std::to_string(i);
  line(4 + static_cast<int>(n), "local.get $__block");
  line(4 + static_cast<int>(n), table);

  // Closing (block $caseK) lands at CIR block K's body.
  for (std::size_t i = 0; i < n; ++i) {
    const int depth = 4 + static_cast<int>(n - 1 - i);
    line(depth, ")");
    line(depth, ";; --- " + fn.blocks()[i].label() + " ---");
    emitBlock(fn, fn.blocks()[i], locals, depth);
  }

  line(3, ")");
  line(2, ")");
  // A function that falls out of the dispatch tower without returning is
  // unreachable by construction -- every CIR block ends in `return` or
  // `br $dispatch` -- but WebAssembly still has to see the body produce a
  // result, so say so explicitly.
  if (fn.returnType() != Ty::Void)
    line(2, "unreachable");
}

std::string FunctionEmitter::emit(const Function &fn) {
  out_.clear();
  usedLocals_.clear();
  computeFrame(fn);

  LocalTable locals = localTable(fn);
  // $__frame is not a CIR register, but the rewritten allocas read it like
  // one, so it needs an entry for the operand pushes to resolve.
  locals["__frame"] = "$__frame";

  // The tower is emitted first, into out_, because which locals need
  // declaring is only known after the peephole has run: a temporary that
  // stayed on the operand stack is never named at all.
  std::string tower;
  if (!fn.blocks().empty()) {
    emitTower(fn, locals);
    tower.swap(out_);
    out_.clear();
  }

  std::string header = "(func $" + fn.name();
  for (const Param &p : fn.params())
    header += " (param $" + p.name + " " + watType(p.ty) + ")";
  if (fn.returnType() != Ty::Void)
    header += " (result " + watType(fn.returnType()) + ")";
  line(1, header);

  if (tower.empty()) {
    line(1, ")");
    return out_;
  }

  // Parameters are already locals and must not be redeclared.
  std::set<std::string> paramNames;
  for (const Param &p : fn.params())
    paramNames.insert("$" + p.name);

  std::map<std::string, Ty> localTypes;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions()) {
      if (!i.dest.has_value())
        continue;
      const std::string name = "$" + i.dest->name;
      if (paramNames.count(name) == 0 && usedLocals_.count(name) != 0)
        localTypes.emplace(name, i.dest->ty);
    }

  for (const auto &entry : localTypes)
    line(2, "(local " + entry.first + " " + watType(entry.second) + ")");
  line(2, "(local $__block i32)");
  if (frameSize_ > 0) {
    line(2, "(local $__fp i32)");
    line(2, "(local $__frame i32)");
  }

  out_ += tower;
  line(1, ")");
  return out_;
}

} // namespace

EmitResult emitModule(const Module &module) {
  EmitResult result;
  FunctionEmitter emitter(result.diagnostics);

  std::string out = "(module\n";
  out += "  ;; CIR module '" + module.name() +
         "' lowered by the MTIR WebAssembly back end (M6c)\n";

  // print_int / print_float are the whole observable-output surface
  // (docs/minilang-spec.md section 8); the host supplies them.
  bool needsPrintI32 = false;
  bool needsPrintF64 = false;
  bool needsMemory = false;
  for (const Function &fn : module.functions())
    for (const BasicBlock &b : fn.blocks())
      for (const Instruction &i : b.instructions()) {
        if (i.op == Opcode::PrintI32)
          needsPrintI32 = true;
        if (i.op == Opcode::PrintF64)
          needsPrintF64 = true;
        if (i.op == Opcode::Alloca || i.op == Opcode::Load || i.op == Opcode::Store ||
            i.op == Opcode::GEP)
          needsMemory = true;
      }

  if (needsPrintI32)
    out += "  (import \"env\" \"print_i32\" (func $print_i32 (param i32)))\n";
  if (needsPrintF64)
    out += "  (import \"env\" \"print_f64\" (func $print_f64 (param f64)))\n";
  if (needsMemory) {
    out += "  (memory 1)\n";
    out += "  (global $__sp (mut i32) (i32.const " + std::to_string(kStackTop) +
           "))\n";
  }

  for (const Global &g : module.globals()) {
    std::string init = "0";
    if (g.init.has_value()) {
      if (const ConstInt *ci = asConstInt(*g.init))
        init = std::to_string(ci->value);
      else if (const ConstFloat *cf = asConstFloat(*g.init))
        init = cir::printDouble(cf->value);
    }
    out += "  (global $" + g.name + " (mut " + watType(g.ty) + ") (" + watType(g.ty) +
           ".const " + init + "))\n";
  }

  for (const Function &fn : module.functions())
    out += emitter.emit(fn);

  for (const Function &fn : module.functions())
    out += "  (export \"" + fn.name() + "\" (func $" + fn.name() + "))\n";

  out += ")\n";
  result.wat = std::move(out);
  return result;
}

} // namespace mtir::backend::wasm
