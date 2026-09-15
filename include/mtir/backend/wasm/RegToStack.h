// RegToStack.h -- register-based CIR -> stack-machine instruction sequences.
//
// Module M6a.  Shared by the WebAssembly back end (M6c) and the stack
// bytecode back end (M7), which is why the third target costs so little.
//
// Why this pass exists
// --------------------
// A three-address instruction assumes its operands are addressable at any
// time.  A stack machine can only reach the top of the stack.  Every CIR
// value must therefore either be consumed immediately by the next
// instruction, or be parked in a local.  Parking everything is always correct
// but produces one set/get pair per temporary; deciding what can stay on the
// stack is a liveness question over the block.
//
// Naive schema (always correct, for any three-address sequence):
//
//     for each instruction I in the block, in order:
//         for each operand O of I, left to right:
//             if O is a constant:  emit CONST(type(O), value(O))
//             else:                emit LOCAL_GET(localOf[O])
//         emit OPCODE(I)
//         if I defines a register R:
//             emit LOCAL_SET(localOf[R])
//
// Peephole rule: a value defined and then immediately consumed exactly once,
// in the same block, never needs to leave the operand stack:
//
//     for each adjacent (LOCAL_SET l, LOCAL_GET l):
//         if useCount(l) == 1 and l is not live-out of the block:
//             delete both
//
// Measured on the abs example (docs/examples/abs.cir): the naive schema emits
// 14 stack instructions and the peephole reduces this to 10, a 28.6%
// reduction.  That figure is Member 4's measurement and is reproduced by
// tests/StackTests.cpp.
#ifndef MTIR_BACKEND_WASM_REGTOSTACK_H
#define MTIR_BACKEND_WASM_REGTOSTACK_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mtir/cir/Function.h"

namespace mtir::backend::wasm {

/// One stack-machine instruction, target-neutral.  The WebAssembly emitter
/// and the bytecode emitter both consume this.
struct StackOp {
  std::string op;          ///< "local.get", "i32.add", "call", ...
  std::string text;        ///< textual operand, e.g. "$x" or "$abs"
  std::int64_t intArg = 0; ///< for i32.const / i64.const
  double floatArg = 0.0;   ///< for f64.const
  bool hasArg = false;

  std::string toString() const;
};

bool operator==(const StackOp &a, const StackOp &b);
inline bool operator!=(const StackOp &a, const StackOp &b) { return !(a == b); }

/// Register name -> local name, for every parameter and every defined
/// register in the function.  std::map, not unordered_map: the emitter walks
/// it to declare locals, and the output must not depend on hash order.
using LocalTable = std::map<std::string, std::string>;

LocalTable localTable(const cir::Function &fn);

/// The instruction that pushes one CIR operand: a constant, a local read, or
/// a global read.  Exposed because the WebAssembly emitter needs to push a
/// terminator's operand without lowering a whole block.
StackOp pushValue(const cir::Value &value, const LocalTable &locals);

/// The stack opcode for one CIR instruction.  `instr.ty` is the operand type
/// for comparisons and the result type otherwise, which is why one prefix
/// lookup serves both.
///
/// Only meaningful for instructions whose stack form is a single opcode
/// applied to operands already pushed in CIR order.  `store`, `gep`, integer
/// `neg`, `not` and `alloca` are not of that shape; lowerInstruction handles
/// them and is what the lowering actually calls.
StackOp opcodeFor(const cir::Instruction &instr);

/// Size in bytes of a value of type `t` in linear memory.  i1 occupies a full
/// i32 slot, matching the i32 representation of booleans (divergence row 5).
std::int64_t sizeOf(cir::Ty t);

/// One CIR instruction as a complete stack sequence, operand pushes included.
///
/// Most instructions are "push operands left to right, then apply one
/// opcode", but several are not, and getting them wrong produces wasm that
/// either underflows the operand stack or silently computes the wrong thing:
///
///   store   CIR is `store ty value, ptr`; wasm wants the address pushed
///           first, so the two operands are swapped here.
///   gep     an index is scaled by the element size; CIR's index is in
///           elements, linear memory is addressed in bytes.
///   neg     integer negation has no wasm opcode and becomes `0 - x`, which
///           needs the zero pushed before the operand.
///   not     unary in CIR, `xor` with an all-ones (or, for i1, one) mask here.
///   alloca  needs a stack frame, which is a property of the function rather
///           than of the instruction; the WebAssembly emitter rewrites allocas
///           into frame-relative address arithmetic before lowering, so an
///           alloca reaching this function is a bug and lowers to
///           `unreachable`.
std::vector<StackOp> lowerInstruction(const cir::Instruction &instr,
                                      const LocalTable &locals);

/// The naive schema, kept as its own function so the peephole can be measured
/// against it.
std::vector<StackOp> lowerBlockNaive(const cir::BasicBlock &block,
                                     const LocalTable &locals);

/// How many times each local is read inside this block.
std::map<std::string, int> blockUseCounts(const cir::BasicBlock &block,
                                          const LocalTable &locals);

/// Locals read by some block other than this one.  A deliberately
/// conservative over-approximation: a real liveness analysis over the CFG
/// would shrink this set and expose more peephole opportunities.
/// Over-approximating is safe -- it only ever suppresses an optimisation.
std::set<std::string> blockLiveOut(const cir::Function &fn,
                                   const cir::BasicBlock &block,
                                   const LocalTable &locals);

std::vector<StackOp> peephole(const std::vector<StackOp> &seq,
                              const std::map<std::string, int> &useCount,
                              const std::set<std::string> &liveOut);

/// One block, lowered and peepholed.
std::vector<StackOp> lowerBlock(const cir::BasicBlock &block, const LocalTable &locals,
                                const std::map<std::string, int> &useCount,
                                const std::set<std::string> &liveOut);

/// Where each alloca'd pointer lives in its function's stack frame.
///
/// A stack machine has no addressable locals, so a MiniLang variable whose
/// address is taken -- which, before optimisation, is every variable -- needs
/// a slot in linear memory rather than a local.  Both stack back ends
/// therefore give each function a frame: the WebAssembly emitter carves it
/// out of linear memory behind a shadow stack pointer, and the bytecode VM
/// allocates one per call.  The layout itself is the same, so it lives here.
struct FrameLayout {
  /// Total frame size in bytes, rounded up so the next frame stays 8-byte
  /// aligned and an i64 or f64 slot is naturally aligned.
  std::int64_t size = 0;

  /// Byte offset of each alloca'd pointer register from the frame base.
  std::map<std::string, std::int64_t> offset;
};

FrameLayout layoutFrame(const cir::Function &fn);

/// `block` with every `%p = alloca T` replaced by `%p = add i32 %frameReg,
/// offset`, so that ordinary three-address lowering -- and the peephole with
/// it -- handles what is really just address arithmetic.  `frameReg` names a
/// register holding the frame base, which each back end supplies itself.
cir::BasicBlock resolveAllocas(const cir::BasicBlock &block, const FrameLayout &frame,
                               const std::string &frameReg);

/// Every block of a function, in order.  Parallel to fn.blocks().
std::vector<std::vector<StackOp>> lowerFunction(const cir::Function &fn);

/// (naive count, peepholed count) for a function.
std::pair<std::size_t, std::size_t> countFunction(const cir::Function &fn);

} // namespace mtir::backend::wasm

#endif // MTIR_BACKEND_WASM_REGTOSTACK_H
