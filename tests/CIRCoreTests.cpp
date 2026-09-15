// Tests for the CIR type system, arithmetic semantics, opcode table and the
// instruction/block/function/module data model.
//
// Migrated from tests/test_cir_printer.py (data-model half) and the wrap_int
// section of tests/test_opt.py in the Python prototype.

#include "mtir_test.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "mtir/cir/Arith.h"
#include "mtir/cir/CFG.h"
#include "mtir/cir/Module.h"
#include "mtir/cir/Opcode.h"

#include "TestSupport.h"

using namespace mtir::cir;

// ==========================================================================
// Types
// ==========================================================================
MTIR_TEST("cir.type", "spellings round-trip") {
  const Ty all[] = {Ty::I1, Ty::I32, Ty::I64, Ty::F64, Ty::Ptr, Ty::Void};
  for (Ty t : all) {
    const auto parsed = parseTy(toString(t));
    CHECK(parsed.has_value());
    CHECK(parsed.has_value() && *parsed == t);
  }
}

MTIR_TEST("cir.type", "unknown type names are rejected") {
  CHECK(!parseTy("i16").has_value());
  CHECK(!parseTy("").has_value());
  CHECK(!parseTy("float").has_value());
}

MTIR_TEST("cir.type", "integer widths match the specification") {
  CHECK_EQ(intWidth(Ty::I1), 1u);
  CHECK_EQ(intWidth(Ty::I32), 32u);
  CHECK_EQ(intWidth(Ty::I64), 64u);
  CHECK_EQ(intWidth(Ty::F64), 0u);
  CHECK(isInteger(Ty::I1) && isInteger(Ty::I32) && isInteger(Ty::I64));
  CHECK(!isInteger(Ty::F64) && !isInteger(Ty::Ptr) && !isInteger(Ty::Void));
  CHECK(isFloat(Ty::F64));
}

// ==========================================================================
// Arithmetic -- docs/divergence.md rows 3 and 4
// ==========================================================================
MTIR_TEST("cir.arith", "wrapInt is two's complement") {
  CHECK_EQ(wrapInt(0, Ty::I32), 0LL);
  CHECK_EQ(wrapInt(2147483647LL, Ty::I32), 2147483647LL);
  CHECK_EQ(wrapInt(2147483648LL, Ty::I32), -2147483648LL);
  CHECK_EQ(wrapInt(-2147483649LL, Ty::I32), 2147483647LL);
  CHECK_EQ(wrapInt(1LL << 40, Ty::I32), 0LL);
  CHECK_EQ(wrapInt(1, Ty::I1), -1LL);
  CHECK_EQ(wrapInt(0, Ty::I1), 0LL);
}

MTIR_TEST("cir.arith", "overflow wraps rather than growing") {
  // row 4: the whole point is that this is computed without signed overflow.
  const std::int64_t sum = wrapInt(2147483647LL + 1LL, Ty::I32);
  CHECK_EQ(sum, -2147483648LL);
}

MTIR_TEST("cir.arith", "shift counts are taken modulo the width") {
  // row 3: 1 << 33 is 1 << 1, and 1 << 32 is 1 << 0.
  CHECK_EQ(maskShift(33, Ty::I32), 1u);
  CHECK_EQ(maskShift(32, Ty::I32), 0u);
  CHECK_EQ(maskShift(31, Ty::I32), 31u);
  CHECK_EQ(maskShift(64, Ty::I64), 0u);
  CHECK_EQ(maskShift(65, Ty::I64), 1u);
  CHECK_EQ(maskShift(-1, Ty::I32), 31u);
}

MTIR_TEST("cir.arith", "intMin matches the width") {
  CHECK_EQ(intMin(Ty::I32), -2147483648LL);
  CHECK_EQ(intMin(Ty::I1), -1LL);
}

MTIR_TEST("cir.arith", "division traps exactly where CIR says it does") {
  // rows 1 and 2.
  CHECK(divTraps(true, 1, 0, Ty::I32));
  CHECK(divTraps(false, 1, 0, Ty::I32));
  CHECK(divTraps(true, -2147483648LL, -1, Ty::I32));
  CHECK(!divTraps(true, -2147483648LL, 1, Ty::I32));
  CHECK(!divTraps(true, 7, 2, Ty::I32));
  // Unsigned division by -1 is division by 0xFFFFFFFF, which is fine.
  CHECK(!divTraps(false, -2147483648LL, -1, Ty::I32));
}

MTIR_TEST("cir.arith", "division truncates toward zero") {
  CHECK_EQ(divTrunc(true, -7, 2, Ty::I32), -3LL);
  CHECK_EQ(divTrunc(true, 7, -2, Ty::I32), -3LL);
  CHECK_EQ(remTrunc(true, -7, 2, Ty::I32), -1LL);
  CHECK_EQ(remTrunc(true, 7, -2, Ty::I32), 1LL);
}

MTIR_TEST("cir.arith", "unsigned division reads the operands as unsigned") {
  // -1 as u32 is 4294967295, so -1 / 2 unsigned is 2147483647.
  CHECK_EQ(divTrunc(false, -1, 2, Ty::I32), 2147483647LL);
}

// ==========================================================================
// Opcodes
// ==========================================================================
MTIR_TEST("cir.opcode", "every opcode has a unique mnemonic that parses back") {
  for (std::size_t i = 0; i < static_cast<std::size_t>(Opcode::Count); ++i) {
    const auto op = static_cast<Opcode>(i);
    const auto parsed = parseOpcode(mnemonic(op));
    CHECK(parsed.has_value());
    CHECK(parsed.has_value() && *parsed == op);
  }
}

MTIR_TEST("cir.opcode", "the instruction set has the size the spec states") {
  // docs/cir-spec.md section 3: 51 mnemonics, 37 opcodes counting icmp.* and
  // fcmp.* as one family each (51 - 9 extra icmp - 5 extra fcmp = 37).
  CHECK_EQ(static_cast<std::size_t>(Opcode::Count), std::size_t{51});
}

MTIR_TEST("cir.opcode", "only br, br.cond and ret are terminators") {
  std::size_t terminators = 0;
  for (std::size_t i = 0; i < static_cast<std::size_t>(Opcode::Count); ++i)
    if (isTerminator(static_cast<Opcode>(i)))
      ++terminators;
  CHECK_EQ(terminators, std::size_t{3});
  CHECK(isTerminator(Opcode::Br));
  CHECK(isTerminator(Opcode::BrCond));
  CHECK(isTerminator(Opcode::Ret));
  // trap is an intrinsic, so a block containing one still needs a terminator.
  CHECK(!isTerminator(Opcode::Trap));
}

MTIR_TEST("cir.opcode", "side effects are marked on exactly the observable opcodes") {
  CHECK(hasSideEffect(Opcode::Store));
  CHECK(hasSideEffect(Opcode::Call));
  CHECK(hasSideEffect(Opcode::PrintI32));
  CHECK(hasSideEffect(Opcode::PrintF64));
  CHECK(hasSideEffect(Opcode::Trap));
  CHECK(!hasSideEffect(Opcode::Add));
  CHECK(!hasSideEffect(Opcode::Load));
  CHECK(!hasSideEffect(Opcode::Alloca));
}

MTIR_TEST("cir.opcode", "result type rules: comparisons are i1") {
  // Well-formedness rule 7.  The printed type is the *operand* type.
  CHECK_EQ(resultType(Opcode::ICmpSlt, Ty::I32), Ty::I1);
  CHECK_EQ(resultType(Opcode::FCmpOlt, Ty::F64), Ty::I1);
}

MTIR_TEST("cir.opcode", "result type rules: alloca and gep are pointers") {
  CHECK_EQ(resultType(Opcode::Alloca, Ty::I32), Ty::Ptr);
  CHECK_EQ(resultType(Opcode::GEP, Ty::I32), Ty::Ptr);
}

MTIR_TEST("cir.opcode", "result type rules: everything else prints its result type") {
  CHECK_EQ(resultType(Opcode::Add, Ty::I64), Ty::I64);
  CHECK_EQ(resultType(Opcode::Load, Ty::F64), Ty::F64);
  CHECK_EQ(resultType(Opcode::Store, Ty::I32), Ty::Void);
}

MTIR_TEST("cir.opcode", "comparisons carry their predicate") {
  CHECK_EQ(std::string(info(Opcode::ICmpSlt).predicate), std::string("slt"));
  CHECK_EQ(std::string(info(Opcode::FCmpOge).predicate), std::string("oge"));
  CHECK(info(Opcode::Add).predicate.empty());
}

// ==========================================================================
// Values
// ==========================================================================
MTIR_TEST("cir.value", "typeOf covers every operand kind") {
  CHECK_EQ(typeOf(Value{Reg{"x", Ty::I64}}), Ty::I64);
  CHECK_EQ(typeOf(Value{ConstInt{7, Ty::I32}}), Ty::I32);
  CHECK_EQ(typeOf(Value{ConstFloat{1.5}}), Ty::F64);
  CHECK_EQ(typeOf(Value{GlobalRef{"counter"}}), Ty::Ptr);
}

MTIR_TEST("cir.value", "registers with the same name but different types differ") {
  const Value narrow{Reg{"x", Ty::I32}};
  const Value wide{Reg{"x", Ty::I64}};
  const Value alsoNarrow{Reg{"x", Ty::I32}};
  CHECK(!(narrow == wide));
  CHECK(narrow == alsoNarrow);
}

// ==========================================================================
// Instructions, blocks, functions
// ==========================================================================
MTIR_TEST("cir.block", "the terminator is the last instruction when it is one") {
  const Module m = mtir::test::demoAbsModule();
  const Function *fn = m.function("abs");
  CHECK(fn != nullptr);
  for (const BasicBlock &b : fn->blocks())
    CHECK(b.terminator() != nullptr);
}

MTIR_TEST("cir.block", "a block whose last instruction is not a terminator has none") {
  BasicBlock b("entry");
  b.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"t", Ty::I32},
                            ConstInt{1, Ty::I32}, ConstInt{2, Ty::I32}));
  CHECK(b.terminator() == nullptr);
  CHECK(b.successors().empty());
}

MTIR_TEST("cir.block", "successors follow terminator order") {
  const Module m = mtir::test::demoAbsModule();
  const std::vector<std::string> succs = m.function("abs")->block("entry")->successors();
  CHECK_EQ(succs.size(), std::size_t{2});
  CHECK_EQ(succs[0], std::string("then"));
  CHECK_EQ(succs[1], std::string("exit"));
}

MTIR_TEST("cir.function", "block lookup finds every block and misses absent ones") {
  Module m = mtir::test::demoAbsModule();
  Function *fn = m.function("abs");
  CHECK(fn->block("entry") != nullptr);
  CHECK(fn->block("then") != nullptr);
  CHECK(fn->block("exit") != nullptr);
  CHECK(fn->block("nowhere") == nullptr);
}

MTIR_TEST("cir.function", "the entry block is the first block") {
  const Module m = mtir::test::demoAbsModule();
  CHECK(m.function("abs")->entry() != nullptr);
  CHECK_EQ(m.function("abs")->entry()->label(), std::string("entry"));
}

MTIR_TEST("cir.function", "setBlocks rebuilds the label index") {
  Module m = mtir::test::demoAbsModule();
  Function *fn = m.function("abs");
  std::vector<BasicBlock> kept{fn->blocks()[0], fn->blocks()[2]};
  fn->setBlocks(std::move(kept));
  CHECK_EQ(fn->blocks().size(), std::size_t{2});
  CHECK(fn->block("exit") != nullptr);
  CHECK(fn->block("then") == nullptr);
}

MTIR_TEST("cir.module", "function lookup works and equality is structural") {
  const Module a = mtir::test::demoAbsModule();
  const Module b = mtir::test::demoAbsModule();
  CHECK(a == b);
  CHECK(a.function("abs") != nullptr);
  CHECK(a.function("missing") == nullptr);
}

MTIR_TEST("cir.instruction", "factories build what the spec describes") {
  const Instruction cmp = Instruction::compare(Opcode::ICmpSlt, Ty::I32,
                                               Reg{"t0", Ty::I1}, Reg{"x", Ty::I32},
                                               ConstInt{0, Ty::I32});
  CHECK_EQ(cmp.ty, Ty::I32);          // the operand type is what is printed
  CHECK_EQ(cmp.resultType(), Ty::I1); // rule 7
  CHECK_EQ(cmp.args.size(), std::size_t{2});

  const Instruction alloca = Instruction::allocaArray(Ty::I32, Reg{"xs", Ty::Ptr}, 8);
  CHECK_EQ(alloca.ty, Ty::I32);        // the element type is what is printed
  CHECK_EQ(alloca.resultType(), Ty::Ptr);

  const Instruction voidRet = Instruction::ret();
  CHECK(voidRet.isTerminator());
  CHECK(voidRet.args.empty());
}
