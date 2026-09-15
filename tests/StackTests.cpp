// StackTests.cpp -- register-to-stack lowering (M6a).
//
// Ported from tests/test_reg2stack.py, which is the reference for everything
// up to and including the measured instruction counts.  The point is not that
// the pass produces one particular listing, but that the two properties the
// WebAssembly and stack-bytecode back ends rely on actually hold:
//
//   1. the naive schema is a faithful, operand-order-preserving translation,
//   2. the peephole only removes a set/get pair nothing could have observed.
//
// The memory and unary cases at the bottom have no Python counterpart: the
// prototype's opcode_for fell through to a generic "<prefix>.<mnemonic>" for
// alloca, gep, load and store, which produces text like `i32.alloca`.  It was
// never exercised on a program with memory operations.  Those cases are new
// work and are pinned here.
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "TestSupport.h"
#include "mtir/backend/wasm/RegToStack.h"
#include "mtir/cir/Function.h"
#include "mtir/cir/Instruction.h"
#include "mtir/cir/Module.h"
#include "mtir_test.h"

using namespace mtir;
using namespace mtir::cir;
using namespace mtir::backend::wasm;

namespace {

/// The abs() function of docs/examples/abs.cir -- the anchor the measured
/// counts are quoted against.
Function absFn() { return mtir::test::demoAbsModule().functions()[0]; }

std::vector<std::string> texts(const std::vector<StackOp> &seq) {
  std::vector<std::string> out;
  out.reserve(seq.size());
  for (const StackOp &op : seq)
    out.push_back(op.toString());
  return out;
}

const BasicBlock &blockNamed(const Function &fn, const std::string &label) {
  for (const BasicBlock &b : fn.blocks())
    if (b.label() == label)
      return b;
  return fn.blocks()[0];
}

StackOp op(std::string name, std::string text) {
  return StackOp{std::move(name), std::move(text), 0, 0.0, false};
}

StackOp constOp(std::string name, std::int64_t value) {
  StackOp o;
  o.op = std::move(name);
  o.intArg = value;
  o.hasArg = true;
  return o;
}

} // namespace

// --------------------------------------------------------------------------
// Opcode mapping
// --------------------------------------------------------------------------
MTIR_TEST("stack", "signed compare maps to a signed stack opcode") {
  const Instruction instr =
      Instruction::compare(Opcode::ICmpSlt, Ty::I32, Reg{"t", Ty::I1},
                           ConstInt{0, Ty::I32}, ConstInt{0, Ty::I32});
  CHECK_EQ(opcodeFor(instr).toString(), std::string("i32.lt_s"));
}

MTIR_TEST("stack", "unsigned compare maps to an unsigned stack opcode") {
  const Instruction instr =
      Instruction::compare(Opcode::ICmpUlt, Ty::I32, Reg{"t", Ty::I1},
                           ConstInt{0, Ty::I32}, ConstInt{0, Ty::I32});
  CHECK_EQ(opcodeFor(instr).toString(), std::string("i32.lt_u"));
}

MTIR_TEST("stack", "float ops take the f64 prefix") {
  const Instruction instr =
      Instruction::binary(Opcode::FAdd, Ty::F64, Reg{"t", Ty::F64},
                          ConstFloat{1.0}, ConstFloat{2.0});
  CHECK_EQ(opcodeFor(instr).toString(), std::string("f64.add"));
}

MTIR_TEST("stack", "i1 is represented as i32 on the stack") {
  // WebAssembly has no i1; docs/divergence.md row 5 requires 0/1 in an i32.
  const Instruction instr =
      Instruction::binary(Opcode::Add, Ty::I1, Reg{"t", Ty::I1},
                          ConstInt{0, Ty::I1}, ConstInt{1, Ty::I1});
  CHECK_EQ(opcodeFor(instr).toString(), std::string("i32.add"));
}

MTIR_TEST("stack", "signed division maps to div_s") {
  const Instruction instr =
      Instruction::binary(Opcode::SDiv, Ty::I32, Reg{"t", Ty::I32},
                          ConstInt{6, Ty::I32}, ConstInt{2, Ty::I32});
  CHECK_EQ(opcodeFor(instr).toString(), std::string("i32.div_s"));
}

MTIR_TEST("stack", "ret maps to return") {
  CHECK_EQ(opcodeFor(Instruction::ret()).toString(), std::string("return"));
}

MTIR_TEST("stack", "trap maps to unreachable") {
  CHECK_EQ(opcodeFor(Instruction::trap()).toString(), std::string("unreachable"));
}

// --------------------------------------------------------------------------
// Naive schema
// --------------------------------------------------------------------------
MTIR_TEST("stack", "operands are pushed left to right") {
  // CIR says `sub i32 0, %x`, so the constant must be pushed before %x.
  // Getting this backwards silently computes x - 0 instead of 0 - x.
  const Function fn = absFn();
  const std::vector<StackOp> seq =
      lowerBlockNaive(blockNamed(fn, "then"), localTable(fn));
  const std::vector<std::string> got = texts(seq);
  CHECK(got.size() >= 3);
  CHECK_EQ(got[0], std::string("i32.const 0"));
  CHECK_EQ(got[1], std::string("local.get $x"));
  CHECK_EQ(got[2], std::string("i32.sub"));
}

MTIR_TEST("stack", "every defining instruction parks its result") {
  const Function fn = absFn();
  const BasicBlock &entry = blockNamed(fn, "entry");
  const std::vector<StackOp> seq = lowerBlockNaive(entry, localTable(fn));

  std::size_t defines = 0;
  for (const Instruction &i : entry.instructions())
    if (i.dest.has_value())
      ++defines;

  std::size_t sets = 0;
  for (const StackOp &o : seq)
    if (o.op == "local.set")
      ++sets;

  CHECK_EQ(sets, defines);
}

MTIR_TEST("stack", "naive count for abs is 14") {
  const std::pair<std::size_t, std::size_t> counts = countFunction(absFn());
  CHECK_EQ(counts.first, static_cast<std::size_t>(14));
}

// --------------------------------------------------------------------------
// Peephole rule
// --------------------------------------------------------------------------
MTIR_TEST("stack", "peephole removes a single-use set/get pair") {
  const std::vector<StackOp> seq = {constOp("i32.const", 1), op("local.set", "$t"),
                                    op("local.get", "$t"), op("return", "")};
  const std::map<std::string, int> uses = {{"$t", 1}};
  const std::vector<std::string> got = texts(peephole(seq, uses, {}));
  CHECK_EQ(got.size(), static_cast<std::size_t>(2));
  CHECK_EQ(got[0], std::string("i32.const 1"));
  CHECK_EQ(got[1], std::string("return"));
}

MTIR_TEST("stack", "peephole keeps the pair when the local is read twice") {
  const std::vector<StackOp> seq = {op("local.set", "$t"), op("local.get", "$t")};
  const std::map<std::string, int> uses = {{"$t", 2}};
  const std::vector<StackOp> got = peephole(seq, uses, {});
  CHECK(got == seq);
}

MTIR_TEST("stack", "peephole keeps the pair when the local is live out") {
  const std::vector<StackOp> seq = {op("local.set", "$t"), op("local.get", "$t")};
  const std::map<std::string, int> uses = {{"$t", 1}};
  const std::set<std::string> liveOut = {"$t"};
  const std::vector<StackOp> got = peephole(seq, uses, liveOut);
  CHECK(got == seq);
}

MTIR_TEST("stack", "peephole does not pair different locals") {
  const std::vector<StackOp> seq = {op("local.set", "$a"), op("local.get", "$b")};
  const std::map<std::string, int> uses = {{"$a", 1}, {"$b", 1}};
  const std::vector<StackOp> got = peephole(seq, uses, {});
  CHECK(got == seq);
}

MTIR_TEST("stack", "peephole does not touch non-adjacent pairs") {
  const std::vector<StackOp> seq = {op("local.set", "$t"), constOp("i32.const", 0),
                                    op("local.get", "$t")};
  const std::map<std::string, int> uses = {{"$t", 1}};
  const std::vector<StackOp> got = peephole(seq, uses, {});
  CHECK(got == seq);
}

MTIR_TEST("stack", "peephole never lengthens a sequence") {
  const std::pair<std::size_t, std::size_t> counts = countFunction(absFn());
  CHECK(counts.second <= counts.first);
}

MTIR_TEST("stack", "peepholed count for abs is 14 -> 10") {
  // The measured figure quoted in the Review 1 report, section 11.5: a 28.6%
  // reduction.  This is Member 4's measurement, reproduced here rather than
  // restated.
  const std::pair<std::size_t, std::size_t> counts = countFunction(absFn());
  CHECK_EQ(counts.first, static_cast<std::size_t>(14));
  CHECK_EQ(counts.second, static_cast<std::size_t>(10));

  // 4 of 14 removed; guard the quoted percentage against integer surprises.
  const std::size_t removed = counts.first - counts.second;
  CHECK_EQ(removed * 1000 / counts.first, static_cast<std::size_t>(285));
}

// --------------------------------------------------------------------------
// Liveness approximation
// --------------------------------------------------------------------------
MTIR_TEST("stack", "live out includes a register used by another block") {
  const Function fn = absFn();
  const LocalTable locals = localTable(fn);
  // %x is read in `then` and `exit`, so it must not be peepholed away here.
  const std::set<std::string> live = blockLiveOut(fn, blockNamed(fn, "entry"), locals);
  CHECK(live.count("$x") == 1);
}

MTIR_TEST("stack", "live out excludes a block-local temporary") {
  const Function fn = absFn();
  const LocalTable locals = localTable(fn);
  const std::set<std::string> live = blockLiveOut(fn, blockNamed(fn, "entry"), locals);
  CHECK(live.count("$t0") == 0);
}

MTIR_TEST("stack", "use counts are per block") {
  const Function fn = absFn();
  const LocalTable locals = localTable(fn);
  const std::map<std::string, int> counts =
      blockUseCounts(blockNamed(fn, "entry"), locals);
  CHECK_EQ(counts.at("$t0"), 1);
  CHECK_EQ(counts.at("$x"), 1);
}

// --------------------------------------------------------------------------
// Whole-function lowering
// --------------------------------------------------------------------------
MTIR_TEST("stack", "lower function covers every block") {
  const Function fn = absFn();
  const std::vector<std::vector<StackOp>> lowered = lowerFunction(fn);
  // Parallel to fn.blocks(), which is how the emitter consumes it: the Python
  // prototype returned a dict keyed by label, but the emitter needs the order.
  CHECK_EQ(lowered.size(), fn.blocks().size());
  for (const std::vector<StackOp> &seq : lowered)
    CHECK(!seq.empty());
}

MTIR_TEST("stack", "local table covers params and definitions") {
  const LocalTable locals = localTable(absFn());
  const LocalTable expected = {{"x", "$x"}, {"t0", "$t0"}, {"t1", "$t1"}};
  CHECK(locals == expected);
}

MTIR_TEST("stack", "a straight-line block needs no locals after the peephole") {
  // (2 + 3) * 4 with each temporary used once: everything stays on the stack.
  const Reg a{"a", Ty::I32};
  const Reg b{"b", Ty::I32};
  BasicBlock block("entry");
  block.add(Instruction::binary(Opcode::Add, Ty::I32, a, ConstInt{2, Ty::I32},
                                ConstInt{3, Ty::I32}));
  block.add(Instruction::binary(Opcode::Mul, Ty::I32, b, a, ConstInt{4, Ty::I32}));
  block.add(Instruction::ret(Ty::I32, b));

  Function fn("k", {}, Ty::I32);
  fn.setBlocks({block});

  const std::vector<std::vector<StackOp>> lowered = lowerFunction(fn);
  CHECK_EQ(lowered.size(), static_cast<std::size_t>(1));
  const std::vector<std::string> got = texts(lowered[0]);

  const std::vector<std::string> expected = {"i32.const 2", "i32.const 3", "i32.add",
                                             "i32.const 4", "i32.mul",    "return"};
  CHECK(got == expected);
}

// --------------------------------------------------------------------------
// Memory and unary lowering -- no Python counterpart; see the file header.
// --------------------------------------------------------------------------
MTIR_TEST("stack", "store pushes the address before the value") {
  // CIR is `store i32 7, %p`.  WebAssembly's i32.store pops (address, value),
  // so pushing the operands in CIR order would store to address 7.
  const Instruction instr =
      Instruction::store(Ty::I32, ConstInt{7, Ty::I32}, Reg{"p", Ty::Ptr});
  const LocalTable locals = {{"p", "$p"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"local.get $p", "i32.const 7",
                                             "i32.store"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "load reads through the address") {
  const Instruction instr = Instruction::load(Ty::I32, Reg{"v", Ty::I32}, Reg{"p", Ty::Ptr});
  const LocalTable locals = {{"p", "$p"}, {"v", "$v"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"local.get $p", "i32.load",
                                             "local.set $v"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "gep scales the index by the element size") {
  // CIR indexes in elements; linear memory is addressed in bytes.  Without
  // the scale, xs[2] would read one byte past xs[0].
  const Instruction instr =
      Instruction::gep(Ty::I32, Reg{"q", Ty::Ptr}, Reg{"p", Ty::Ptr}, ConstInt{2, Ty::I32});
  const LocalTable locals = {{"p", "$p"}, {"q", "$q"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"local.get $p", "i32.const 2",
                                             "i32.const 4",  "i32.mul",
                                             "i32.add",      "local.set $q"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "gep scales an f64 index by eight") {
  const Instruction instr =
      Instruction::gep(Ty::F64, Reg{"q", Ty::Ptr}, Reg{"p", Ty::Ptr}, ConstInt{1, Ty::I32});
  const LocalTable locals = {{"p", "$p"}, {"q", "$q"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  CHECK(got.size() >= 4);
  CHECK_EQ(got[2], std::string("i32.const 8"));
}

MTIR_TEST("stack", "element sizes match the wasm representation") {
  CHECK_EQ(sizeOf(Ty::I32), static_cast<std::int64_t>(4));
  CHECK_EQ(sizeOf(Ty::I1), static_cast<std::int64_t>(4));
  CHECK_EQ(sizeOf(Ty::Ptr), static_cast<std::int64_t>(4));
  CHECK_EQ(sizeOf(Ty::I64), static_cast<std::int64_t>(8));
  CHECK_EQ(sizeOf(Ty::F64), static_cast<std::int64_t>(8));
}

MTIR_TEST("stack", "integer neg becomes zero minus the operand") {
  // WebAssembly has no i32.neg, and i32.sub needs the zero pushed first.
  const Instruction instr =
      Instruction::unary(Opcode::Neg, Ty::I32, Reg{"t", Ty::I32}, Reg{"x", Ty::I32});
  const LocalTable locals = {{"x", "$x"}, {"t", "$t"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"i32.const 0", "local.get $x",
                                             "i32.sub", "local.set $t"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "float neg uses the native unary opcode") {
  const Instruction instr =
      Instruction::unary(Opcode::Neg, Ty::F64, Reg{"t", Ty::F64}, Reg{"x", Ty::F64});
  const LocalTable locals = {{"x", "$x"}, {"t", "$t"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"local.get $x", "f64.neg",
                                             "local.set $t"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "not on i1 xors with one to stay normalised") {
  // An i1 travels as 0 or 1; xor with -1 would produce -2 and break row 5.
  const Instruction instr =
      Instruction::unary(Opcode::Not, Ty::I1, Reg{"t", Ty::I1}, Reg{"b", Ty::I1});
  const LocalTable locals = {{"b", "$b"}, {"t", "$t"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"local.get $b", "i32.const 1", "i32.xor",
                                             "local.set $t"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "not on i32 xors with all ones") {
  const Instruction instr =
      Instruction::unary(Opcode::Not, Ty::I32, Reg{"t", Ty::I32}, Reg{"x", Ty::I32});
  const LocalTable locals = {{"x", "$x"}, {"t", "$t"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  CHECK(got.size() >= 3);
  CHECK_EQ(got[1], std::string("i32.const -1"));
  CHECK_EQ(got[2], std::string("i32.xor"));
}

MTIR_TEST("stack", "a global is read with global.get, not a load") {
  // A CIR global becomes a WebAssembly global, which is a value and has no
  // address in linear memory.
  const Instruction instr =
      Instruction::load(Ty::I32, Reg{"v", Ty::I32}, GlobalRef{"counter"});
  const LocalTable locals = {{"v", "$v"}};
  const std::vector<std::string> got = texts(lowerInstruction(instr, locals));
  const std::vector<std::string> expected = {"global.get $counter", "local.set $v"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "a global is written with global.set, not a store") {
  const Instruction instr =
      Instruction::store(Ty::I32, ConstInt{5, Ty::I32}, GlobalRef{"counter"});
  const std::vector<std::string> got = texts(lowerInstruction(instr, {}));
  const std::vector<std::string> expected = {"i32.const 5", "global.set $counter"};
  CHECK(got == expected);
}

MTIR_TEST("stack", "alloca has no stack form of its own") {
  // The frame layout is a property of the function, so the WebAssembly
  // emitter rewrites allocas into frame-relative arithmetic before lowering.
  // One reaching here is a bug, and `unreachable` says so rather than
  // inventing an address.
  const Instruction instr = Instruction::alloca_(Ty::I32, Reg{"p", Ty::Ptr});
  const std::vector<std::string> got = texts(lowerInstruction(instr, {{"p", "$p"}}));
  const std::vector<std::string> expected = {"unreachable", "local.set $p"};
  CHECK(got == expected);
}
