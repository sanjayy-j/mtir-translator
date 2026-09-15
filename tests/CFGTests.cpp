// Tests for CFG construction and the CIR verifier.
//
// Migrated from tests/test_cir_cfg.py.  Each malformed-IR test names the rule
// of docs/cir-spec.md section 6 it covers, so the eight detectable classes
// that Objective O1 requires can be traced from the spec to a test.

#include "mtir_test.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mtir/cir/CFG.h"
#include "mtir/cir/Verifier.h"

#include "TestSupport.h"

using namespace mtir::cir;
using mtir::test::demoAbsModule;
using mtir::test::hasCode;
using mtir::test::mentions;

namespace {

Instruction retInt(std::int64_t v = 0) {
  return Instruction::ret(Ty::I32, ConstInt{v, Ty::I32});
}

Instruction addConst(const char *dest) {
  return Instruction::binary(Opcode::Add, Ty::I32, Reg{dest, Ty::I32},
                             ConstInt{1, Ty::I32}, ConstInt{2, Ty::I32});
}

Function makeFn(std::vector<BasicBlock> blocks, std::vector<Param> params = {},
                Ty ret = Ty::I32, const char *name = "f") {
  Function fn(name, std::move(params), ret);
  fn.setBlocks(std::move(blocks));
  return fn;
}

std::vector<std::string> successorLabels(const CFG &cfg, const char *label) {
  std::vector<std::string> out;
  const auto id = cfg.find(label);
  if (!id)
    return out;
  for (BlockId s : cfg.successors(*id))
    out.push_back(cfg.label(s));
  return out;
}

std::vector<std::string> predecessorLabels(const CFG &cfg, const char *label) {
  std::vector<std::string> out;
  const auto id = cfg.find(label);
  if (!id)
    return out;
  for (BlockId p : cfg.predecessors(*id))
    out.push_back(cfg.label(p));
  return out;
}

} // namespace

// ==========================================================================
// Queries
// ==========================================================================
MTIR_TEST("cfg", "successors follow terminator order") {
  const Module m = demoAbsModule();
  const CFG cfg(*m.function("abs"));
  const std::vector<std::string> succs = successorLabels(cfg, "entry");
  CHECK_EQ(succs.size(), std::size_t{2});
  if (succs.size() == 2) {
    CHECK_EQ(succs[0], std::string("then"));
    CHECK_EQ(succs[1], std::string("exit"));
  }
  CHECK(successorLabels(cfg, "then").empty());
  CHECK(successorLabels(cfg, "exit").empty());
}

MTIR_TEST("cfg", "predecessors invert the successor map") {
  const Module m = demoAbsModule();
  const CFG cfg(*m.function("abs"));
  // The vectors are held in locals rather than indexed inline: CHECK_EQ binds
  // a reference to its argument, and a reference into a temporary container
  // would dangle before the comparison runs.
  const std::vector<std::string> entryPreds = predecessorLabels(cfg, "entry");
  const std::vector<std::string> thenPreds = predecessorLabels(cfg, "then");
  const std::vector<std::string> exitPreds = predecessorLabels(cfg, "exit");
  CHECK(entryPreds.empty());
  CHECK_EQ(thenPreds.size(), std::size_t{1});
  CHECK_EQ(exitPreds.size(), std::size_t{1});
  if (!thenPreds.empty())
    CHECK_EQ(thenPreds[0], std::string("entry"));
  if (!exitPreds.empty())
    CHECK_EQ(exitPreds[0], std::string("entry"));
}

MTIR_TEST("cfg", "a doubled edge is one predecessor, not two") {
  BasicBlock entry("entry");
  entry.add(Instruction::brCond(ConstInt{1, Ty::I1}, "join", "join"));
  BasicBlock join("join");
  join.add(retInt());
  const Function fn = makeFn({entry, join});
  const CFG cfg(fn);
  const std::vector<std::string> joinPreds = predecessorLabels(cfg, "join");
  CHECK_EQ(joinPreds.size(), std::size_t{1});
  // Both successor slots are still present: the CFG records the edges the
  // terminator names, and only the predecessor list is deduplicated.
  CHECK_EQ(successorLabels(cfg, "entry").size(), std::size_t{2});
  CHECK(successorLabels(cfg, "join").empty());
}

MTIR_TEST("cfg", "reachability excludes an orphan block") {
  BasicBlock entry("entry");
  entry.add(retInt());
  BasicBlock orphan("orphan");
  orphan.add(retInt(1));
  const Function fn = makeFn({entry, orphan});
  const CFG cfg(fn);
  CHECK(cfg.isReachable(*cfg.find("entry")));
  CHECK(!cfg.isReachable(*cfg.find("orphan")));
}

MTIR_TEST("cfg", "a loop back edge is a predecessor and reachability terminates") {
  BasicBlock entry("entry");
  entry.add(Instruction::br("head"));
  BasicBlock head("head");
  head.add(Instruction::brCond(ConstInt{1, Ty::I1}, "head", "end"));
  BasicBlock end("end");
  end.add(retInt());
  const Function fn = makeFn({entry, head, end});
  const CFG cfg(fn);
  const std::vector<std::string> headPreds = predecessorLabels(cfg, "head");
  CHECK_EQ(headPreds.size(), std::size_t{2});
  CHECK(cfg.isReachable(*cfg.find("end")));
  CHECK_EQ(cfg.reversePostorder().size(), std::size_t{3});
}

MTIR_TEST("cfg", "reverse postorder starts at the entry block") {
  const Module m = demoAbsModule();
  const CFG cfg(*m.function("abs"));
  const std::vector<BlockId> &rpo = cfg.reversePostorder();
  CHECK(!rpo.empty());
  if (!rpo.empty())
    CHECK_EQ(cfg.label(rpo[0]), std::string("entry"));
}

MTIR_TEST("cfg", "a dangling branch target is flagged and produces no edge") {
  BasicBlock entry("entry");
  entry.add(Instruction::br("nowhere"));
  const Function fn = makeFn({entry});
  const CFG cfg(fn);
  CHECK(cfg.hasDanglingEdge());
  CHECK(successorLabels(cfg, "entry").empty());
}

// ==========================================================================
// Well-formedness: the eight rules
// ==========================================================================
MTIR_TEST("verifier", "a well-formed module produces no diagnostics") {
  const Module m = demoAbsModule();
  const auto diags = verify(m);
  CHECK(diags.empty());
  if (!diags.empty())
    mtir::test::reportFailure(__FILE__, __LINE__, mtir::support::format(diags));
}

MTIR_TEST("verifier", "rule 1: a block without a terminator") {
  BasicBlock entry("entry");
  entry.add(addConst("t"));
  const auto diags = checkCFG(makeFn({entry}));
  CHECK(hasCode(diags, "CIR01"));
  CHECK(mentions(diags, "does not end in a terminator"));
}

MTIR_TEST("verifier", "rule 1: an empty block is also unterminated") {
  CHECK(hasCode(checkCFG(makeFn({BasicBlock("entry")})), "CIR01"));
}

MTIR_TEST("verifier", "rule 2: an instruction after a terminator") {
  BasicBlock entry("entry");
  entry.add(retInt());
  entry.add(addConst("t"));
  entry.add(retInt());
  const auto diags = checkCFG(makeFn({entry}));
  CHECK(hasCode(diags, "CIR02"));
  CHECK(mentions(diags, "after a terminator"));
}

MTIR_TEST("verifier", "rule 3: a branch to a block that does not exist") {
  BasicBlock entry("entry");
  entry.add(Instruction::br("nowhere"));
  const auto diags = checkCFG(makeFn({entry}));
  CHECK(hasCode(diags, "CIR03"));
  CHECK(mentions(diags, "undefined block 'nowhere'"));
}

MTIR_TEST("verifier", "rule 5: the entry block has a predecessor") {
  BasicBlock entry("entry");
  entry.add(Instruction::br("back"));
  BasicBlock back("back");
  back.add(Instruction::br("entry"));
  const auto diags = checkCFG(makeFn({entry, back}));
  CHECK(hasCode(diags, "CIR05"));
  CHECK(mentions(diags, "has a predecessor"));
}

MTIR_TEST("verifier", "duplicate block labels are rejected") {
  BasicBlock a("entry");
  a.add(retInt());
  BasicBlock b("entry");
  b.add(retInt(1));
  CHECK(hasCode(checkCFG(makeFn({a, b})), "CIR09"));
}

MTIR_TEST("verifier", "a function with no blocks is rejected") {
  CHECK(hasCode(checkCFG(makeFn({})), "CIR00"));
}

// -- rule 4 ----------------------------------------------------------------
MTIR_TEST("verifier", "rule 4: use of an undefined register") {
  BasicBlock entry("entry");
  entry.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"t", Ty::I32},
                                Reg{"ghost", Ty::I32}, ConstInt{1, Ty::I32}));
  entry.add(retInt());
  const auto diags = checkDefinitions(makeFn({entry}));
  CHECK(hasCode(diags, "CIR04"));
  CHECK(mentions(diags, "%ghost"));
}

MTIR_TEST("verifier", "rule 4: a parameter is defined on entry") {
  BasicBlock entry("entry");
  entry.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"t", Ty::I32},
                                Reg{"x", Ty::I32}, ConstInt{1, Ty::I32}));
  entry.add(Instruction::ret(Ty::I32, Reg{"t", Ty::I32}));
  CHECK(checkDefinitions(makeFn({entry}, {Param{"x", Ty::I32}})).empty());
}

MTIR_TEST("verifier", "rule 4: a register defined on only one path is rejected") {
  // %only is defined in one arm of the branch and used after the join.
  BasicBlock entry("entry");
  entry.add(Instruction::brCond(ConstInt{1, Ty::I1}, "a", "join"));
  BasicBlock a("a");
  a.add(addConst("only"));
  a.add(Instruction::br("join"));
  BasicBlock join("join");
  join.add(Instruction::ret(Ty::I32, Reg{"only", Ty::I32}));
  const auto diags = checkDefinitions(makeFn({entry, a, join}));
  CHECK(hasCode(diags, "CIR04"));
  CHECK(mentions(diags, "%only"));
}

MTIR_TEST("verifier", "rule 4: a register defined on every path is accepted") {
  BasicBlock entry("entry");
  entry.add(Instruction::brCond(ConstInt{1, Ty::I1}, "a", "b"));
  BasicBlock a("a");
  a.add(addConst("v"));
  a.add(Instruction::br("join"));
  BasicBlock b("b");
  b.add(addConst("v"));
  b.add(Instruction::br("join"));
  BasicBlock join("join");
  join.add(Instruction::ret(Ty::I32, Reg{"v", Ty::I32}));
  const auto diags = checkDefinitions(makeFn({entry, a, b, join}));
  CHECK(diags.empty());
  if (!diags.empty())
    mtir::test::reportFailure(__FILE__, __LINE__, mtir::support::format(diags));
}

MTIR_TEST("verifier", "rule 4: unreachable blocks are skipped") {
  // An orphan has no incoming path, so definedness there is vacuous.
  BasicBlock entry("entry");
  entry.add(retInt());
  BasicBlock orphan("orphan");
  orphan.add(Instruction::ret(Ty::I32, Reg{"g", Ty::I32}));
  CHECK(checkDefinitions(makeFn({entry, orphan})).empty());
}

MTIR_TEST("verifier", "rule 4: the dataflow terminates on a loop") {
  BasicBlock entry("entry");
  entry.add(addConst("i"));
  entry.add(Instruction::br("head"));
  BasicBlock head("head");
  head.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"n", Ty::I32},
                               Reg{"i", Ty::I32}, ConstInt{1, Ty::I32}));
  head.add(Instruction::brCond(ConstInt{1, Ty::I1}, "head", "end"));
  BasicBlock end("end");
  end.add(Instruction::ret(Ty::I32, Reg{"n", Ty::I32}));
  const auto diags = checkDefinitions(makeFn({entry, head, end}));
  CHECK(diags.empty());
  if (!diags.empty())
    mtir::test::reportFailure(__FILE__, __LINE__, mtir::support::format(diags));
}

// -- rules 6, 7, 8 ---------------------------------------------------------
MTIR_TEST("verifier", "rule 6: an operand of the wrong type") {
  BasicBlock entry("entry");
  entry.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"t", Ty::I32},
                                ConstInt{1, Ty::I64}, ConstInt{2, Ty::I32}));
  entry.add(retInt());
  const auto diags = checkTypes(makeFn({entry}));
  CHECK(hasCode(diags, "CIR06"));
  CHECK(mentions(diags, "operand 0"));
}

MTIR_TEST("verifier", "rule 6: the wrong number of operands") {
  Instruction bad = Instruction::binary(Opcode::Neg, Ty::I32, Reg{"t", Ty::I32},
                                        ConstInt{1, Ty::I32}, ConstInt{2, Ty::I32});
  BasicBlock entry("entry");
  entry.add(bad);
  entry.add(retInt());
  const auto diags = checkTypes(makeFn({entry}));
  CHECK(hasCode(diags, "CIR06"));
  CHECK(mentions(diags, "takes 1 operand"));
}

MTIR_TEST("verifier", "rule 6: a store must write through a pointer") {
  BasicBlock entry("entry");
  entry.add(Instruction::store(Ty::I32, ConstInt{1, Ty::I32}, ConstInt{0, Ty::I32}));
  entry.add(retInt());
  CHECK(hasCode(checkTypes(makeFn({entry})), "CIR06"));
}

MTIR_TEST("verifier", "rule 7: a comparison whose result is not i1") {
  Instruction bad = Instruction::compare(Opcode::ICmpSlt, Ty::I32,
                                         Reg{"t", Ty::I32}, // should be i1
                                         ConstInt{1, Ty::I32}, ConstInt{2, Ty::I32});
  BasicBlock entry("entry");
  entry.add(bad);
  entry.add(retInt());
  const auto diags = checkTypes(makeFn({entry}));
  CHECK(hasCode(diags, "CIR07"));
}

MTIR_TEST("verifier", "rule 8: ret without a value in a non-void function") {
  BasicBlock entry("entry");
  entry.add(Instruction::ret());
  const auto diags = checkTypes(makeFn({entry}));
  CHECK(hasCode(diags, "CIR08"));
  CHECK(mentions(diags, "without a value"));
}

MTIR_TEST("verifier", "rule 8: ret with a value in a void function") {
  BasicBlock entry("entry");
  entry.add(retInt());
  const auto diags = checkTypes(makeFn({entry}, {}, Ty::Void));
  CHECK(hasCode(diags, "CIR08"));
}

MTIR_TEST("verifier", "rule 8: ret of the wrong type") {
  BasicBlock entry("entry");
  entry.add(Instruction::ret(Ty::I64, ConstInt{0, Ty::I64}));
  const auto diags = checkTypes(makeFn({entry}));
  CHECK(hasCode(diags, "CIR08"));
}

MTIR_TEST("verifier", "a call is checked against the callee's signature") {
  Module m("t");
  BasicBlock cEntry("entry");
  cEntry.add(retInt());
  m.addFunction(makeFn({cEntry}, {Param{"a", Ty::I32}}, Ty::I32, "callee"));

  BasicBlock entry("entry");
  entry.add(Instruction::call(Ty::I32, Reg{"t", Ty::I32}, "callee",
                              {ConstInt{1, Ty::I32}, ConstInt{2, Ty::I32}}));
  entry.add(retInt());
  m.addFunction(makeFn({entry}, {}, Ty::I32, "caller"));

  const auto diags = verify(m);
  CHECK(hasCode(diags, "CIR06"));
  CHECK(mentions(diags, "takes 1 argument"));
}

MTIR_TEST("verifier", "a call to an undefined function is rejected") {
  Module m("t");
  BasicBlock entry("entry");
  entry.add(Instruction::call(Ty::I32, Reg{"t", Ty::I32}, "nope", {}));
  entry.add(retInt());
  m.addFunction(makeFn({entry}));
  CHECK(mentions(verify(m), "undefined function @nope"));
}

MTIR_TEST("verifier", "a broken graph short-circuits the later checks") {
  // A dangling branch target makes the dataflow meaningless, so it should be
  // the only thing reported rather than the head of a cascade.
  BasicBlock entry("entry");
  entry.add(Instruction::binary(Opcode::Add, Ty::I32, Reg{"t", Ty::I32},
                                Reg{"ghost", Ty::I32}, ConstInt{1, Ty::I32}));
  entry.add(Instruction::br("nowhere"));
  const auto diags = verifyFunction(makeFn({entry}));
  CHECK_EQ(diags.size(), std::size_t{1});
  CHECK(hasCode(diags, "CIR03"));
}

MTIR_TEST("verifier", "diagnostics carry the function and block they point at") {
  BasicBlock entry("entry");
  entry.add(addConst("t"));
  const auto diags = checkCFG(makeFn({entry}));
  CHECK(!diags.empty());
  if (diags.empty())
    return;
  CHECK_EQ(diags[0].location.function, std::string("f"));
  CHECK_EQ(diags[0].location.block, std::string("entry"));
  CHECK(diags[0].toString().find("@f:entry") != std::string::npos);
}
