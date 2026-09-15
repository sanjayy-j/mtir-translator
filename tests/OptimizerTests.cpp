// Tests for the CIR optimisation passes.
//
// Migrated from tests/test_opt.py.  Each pass is tested for what it does and
// for what it must refuse to do.  The refusals matter more: an optimisation
// that deletes a trap, or that adopts C++'s arithmetic where CIR has defined
// its own, would silently break the cross-target agreement the whole project
// is measured on.

#include "mtir_test.h"

#include <cmath>
#include <string>
#include <vector>

#include "mtir/cir/Parser.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"
#include "mtir/opt/Pass.h"
#include "mtir/opt/Passes.h"

#include "TestSupport.h"

using namespace mtir::cir;
using namespace mtir::opt;

namespace {

Module parse(const std::string &text) {
  ParseResult r = parseCir(text);
  if (!r.ok())
    return Module("<parse failed>");
  return *r.module;
}

Module fnOf(const std::string &body,
            const std::string &header = "func @f(i32 %x, i32 %y) -> i32 {") {
  return parse(header + "\nentry:\n" + body + "\n}\n");
}

std::vector<std::string> opsOf(const Function &fn) {
  std::vector<std::string> out;
  for (const BasicBlock &b : fn.blocks())
    for (const Instruction &i : b.instructions())
      out.emplace_back(mnemonic(i.op));
  return out;
}

std::string opsText(const Function &fn) {
  std::string out;
  for (const std::string &op : opsOf(fn)) {
    if (!out.empty())
      out += " ";
    out += op;
  }
  return out;
}

/// The first instruction's first operand, which is what most of the folding
/// tests want to look at.
Value firstArg(const Function &fn) {
  return fn.blocks()[0].instructions()[0].args[0];
}

bool runPass(Pass &pass, Module &m) {
  bool changed = false;
  for (Function &fn : m.functions())
    changed = pass.runOnFunction(fn) || changed;
  return changed;
}

} // namespace

// ==========================================================================
// Constant folding
// ==========================================================================
MTIR_TEST("opt.constfold", "arithmetic folds and propagates") {
  Module m = fnOf("  %a = mul i32 3, 4\n  %b = add i32 2, %a\n  ret i32 %b");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("ret"));
  const Value v = firstArg(m.functions()[0]);
  CHECK(v == Value{ConstInt{14, Ty::I32}});
}

MTIR_TEST("opt.constfold", "overflow wraps rather than growing") {
  // row 4: CIR defines signed overflow as wraparound.  Doing this on int64_t
  // without the unsigned detour would be undefined behaviour in the folder.
  Module m = fnOf("  %a = add i32 2147483647, 1\n  ret i32 %a");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{-2147483648LL, Ty::I32}});
}

MTIR_TEST("opt.constfold", "shift counts fold modulo the width") {
  // row 3: 1 << 33 is 1 << 1, not 0 and not a huge number.
  Module m = fnOf("  %a = shl i32 1, 33\n  ret i32 %a");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{2, Ty::I32}});
}

MTIR_TEST("opt.constfold", "lshr folds as unsigned and ashr as signed") {
  Module m = fnOf("  %a = lshr i32 -1, 28\n  %b = ashr i32 -1, 28\n"
                  "  %c = add i32 %a, %b\n  ret i32 %c");
  ConstantFoldingPass pass;
  runPass(pass, m);
  // (unsigned) 0xFFFFFFFF >> 28 == 15;  (signed) -1 >> 28 == -1.
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{14, Ty::I32}});
}

MTIR_TEST("opt.constfold", "comparisons fold to i1") {
  Module m = fnOf("  %a = icmp.slt i32 1, 2\n  %b = zext i32 %a\n  ret i32 %b");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{1, Ty::I32}});
}

MTIR_TEST("opt.constfold", "an unsigned comparison folds on the unsigned reading") {
  Module m = fnOf("  %a = icmp.ult i32 -1, 1\n  %b = zext i32 %a\n  ret i32 %b");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{0, Ty::I32}});
}

MTIR_TEST("opt.constfold", "safe division folds, truncating toward zero") {
  Module m = fnOf("  %a = sdiv i32 -7, 2\n  ret i32 %a");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{-3, Ty::I32}});
}

MTIR_TEST("opt.constfold", "safe remainder takes the sign of the dividend") {
  Module m = fnOf("  %a = srem i32 -7, 2\n  ret i32 %a");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{-1, Ty::I32}});
}

MTIR_TEST("opt.constfold", "a trapping division is never folded") {
  // rows 1 and 2: these trap, so folding them would delete the trap.
  const char *bodies[] = {
      "  %a = sdiv i32 1, 0\n  ret i32 %a",
      "  %a = srem i32 1, 0\n  ret i32 %a",
      "  %a = udiv i32 1, 0\n  ret i32 %a",
      "  %a = sdiv i32 -2147483648, -1\n  ret i32 %a",
      "  %a = srem i32 -2147483648, -1\n  ret i32 %a",
  };
  for (const char *body : bodies) {
    Module m = fnOf(body);
    ConstantFoldingPass pass;
    runPass(pass, m);
    const std::vector<std::string> ops = opsOf(m.functions()[0]);
    CHECK_EQ(ops.size(), std::size_t{2});
    if (ops.size() == 2)
      CHECK_NE(ops[0], std::string("ret"));
  }
}

MTIR_TEST("opt.constfold", "float division is not folded") {
  Module m = parse("func @f() -> f64 {\nentry:\n"
                   "  %a = fdiv f64 1.0, 0.0\n  ret f64 %a\n}\n");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("fdiv ret"));
}

MTIR_TEST("opt.constfold", "float addition folds") {
  Module m = parse("func @f() -> f64 {\nentry:\n"
                   "  %a = fadd f64 1.5, 2.0\n  ret f64 %a\n}\n");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstFloat{3.5}});
}

MTIR_TEST("opt.constfold", "a NaN operand is left alone") {
  Instruction instr = Instruction::binary(Opcode::FAdd, Ty::F64, Reg{"t", Ty::F64},
                                          ConstFloat{std::nan("")}, ConstFloat{1.0});
  CHECK(!foldInstruction(instr).has_value());
}

MTIR_TEST("opt.constfold", "widening conversions fold") {
  Module m = parse("func @f() -> i64 {\nentry:\n"
                   "  %a = sext i64 -1\n  ret i64 %a\n}\n");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK(firstArg(m.functions()[0]) == Value{ConstInt{-1, Ty::I64}});
}

MTIR_TEST("opt.constfold", "a register assigned twice is not propagated") {
  // CIR is not in SSA form; only a single definition may be substituted.
  Module m = fnOf("  %a = add i32 1, 2\n  %a = add i32 3, 4\n  ret i32 %a");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("add add ret"));
}

MTIR_TEST("opt.constfold", "a constant branch becomes unconditional") {
  Module m = parse("func @f() -> i32 {\nentry:\n"
                   "  %c = icmp.slt i32 1, 2\n  br %c ? a : b\n"
                   "a:\n  ret i32 1\n"
                   "b:\n  ret i32 2\n}\n");
  ConstantFoldingPass pass;
  runPass(pass, m);
  const Instruction *term = m.functions()[0].block("entry")->terminator();
  CHECK(term != nullptr);
  if (term != nullptr) {
    CHECK_EQ(term->op, Opcode::Br);
    CHECK_EQ(term->labels[0], std::string("a"));
  }
}

MTIR_TEST("opt.constfold", "a call result is never folded away") {
  Module m = fnOf("  %a = call i32 @g(1, 2)\n  ret i32 %a",
                  "func @f() -> i32 {");
  ConstantFoldingPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("call ret"));
}

// ==========================================================================
// Copy propagation
// ==========================================================================
MTIR_TEST("opt.copyprop", "identity operations are copies") {
  const char *bodies[] = {
      "  %a = add i32 %x, 0\n  ret i32 %a",
      "  %a = add i32 0, %x\n  ret i32 %a",
      "  %a = sub i32 %x, 0\n  ret i32 %a",
      "  %a = mul i32 %x, 1\n  ret i32 %a",
      "  %a = mul i32 1, %x\n  ret i32 %a",
      "  %a = sdiv i32 %x, 1\n  ret i32 %a",
      "  %a = udiv i32 %x, 1\n  ret i32 %a",
      "  %a = or i32 %x, 0\n  ret i32 %a",
      "  %a = xor i32 %x, 0\n  ret i32 %a",
      "  %a = and i32 %x, -1\n  ret i32 %a",
      "  %a = shl i32 %x, 0\n  ret i32 %a",
      "  %a = ashr i32 %x, 0\n  ret i32 %a",
      "  %a = lshr i32 %x, 0\n  ret i32 %a",
  };
  for (const char *body : bodies) {
    Module m = fnOf(body);
    CopyPropagationPass pass;
    runPass(pass, m);
    CHECK_EQ(opsText(m.functions()[0]), std::string("ret"));
    if (opsOf(m.functions()[0]).size() == 1)
      CHECK(firstArg(m.functions()[0]) == Value{Reg{"x", Ty::I32}});
  }
}

MTIR_TEST("opt.copyprop", "non-identities are left alone") {
  const char *bodies[] = {
      "  %a = sub i32 0, %x\n  ret i32 %a",  // 0 - x is a negation
      "  %a = sdiv i32 1, %x\n  ret i32 %a", // 1 / x is not a copy
      "  %a = shl i32 0, %x\n  ret i32 %a",
      "  %a = and i32 %x, 1\n  ret i32 %a",
      "  %a = mul i32 %x, 2\n  ret i32 %a",
  };
  for (const char *body : bodies) {
    Module m = fnOf(body);
    CopyPropagationPass pass;
    runPass(pass, m);
    CHECK_EQ(opsOf(m.functions()[0]).size(), std::size_t{2});
  }
}

MTIR_TEST("opt.copyprop", "a chain of copies collapses") {
  Module m = fnOf("  %a = add i32 %x, 0\n  %b = mul i32 %a, 1\n"
                  "  %c = or i32 %b, 0\n  ret i32 %c");
  CopyPropagationPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("ret"));
  if (opsOf(m.functions()[0]).size() == 1)
    CHECK(firstArg(m.functions()[0]) == Value{Reg{"x", Ty::I32}});
}

MTIR_TEST("opt.copyprop", "copies propagate across blocks") {
  Module m = parse("func @f(i32 %x) -> i32 {\nentry:\n"
                   "  %a = add i32 %x, 0\n  br next\n"
                   "next:\n  ret i32 %a\n}\n");
  CopyPropagationPass pass;
  runPass(pass, m);
  const Function &fn = m.functions()[0];
  CHECK(fn.block("next")->instructions()[0].args[0] == Value{Reg{"x", Ty::I32}});
}

MTIR_TEST("opt.copyprop", "float identities are not treated as copies") {
  // fadd %x, 0.0 is not %x: it turns -0.0 into +0.0.
  Module m = parse("func @f(f64 %x) -> f64 {\nentry:\n"
                   "  %a = fadd f64 %x, 0.0\n  ret f64 %a\n}\n");
  CopyPropagationPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("fadd ret"));
}

// ==========================================================================
// Dead-code elimination
// ==========================================================================
MTIR_TEST("opt.dce", "an unread definition is removed") {
  Module m = fnOf("  %a = add i32 %x, %y\n  ret i32 %x");
  DeadCodeEliminationPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("ret"));
}

MTIR_TEST("opt.dce", "a chain of dead definitions is removed") {
  Module m = fnOf("  %a = add i32 %x, %y\n  %b = mul i32 %a, %a\n  ret i32 %x");
  DeadCodeEliminationPass pass;
  runPass(pass, m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("ret"));
}

MTIR_TEST("opt.dce", "observable instructions are kept") {
  // A call is kept even with an unused result: CIR has no purity annotation,
  // so assuming a call is pure would be unsound.
  const char *bodies[] = {
      "  store i32 1, %p\n  ret i32 0",
      "  %a = call i32 @g(1)\n  ret i32 0",
      "  print.i32 1\n  ret i32 0",
      "  trap\n  ret i32 0",
  };
  for (const char *body : bodies) {
    Module m = fnOf(body, "func @f(ptr %p) -> i32 {");
    DeadCodeEliminationPass pass;
    runPass(pass, m);
    CHECK_EQ(opsOf(m.functions()[0]).size(), std::size_t{2});
  }
}

MTIR_TEST("opt.dce", "an unreachable block is removed") {
  Module m = parse("func @f() -> i32 {\nentry:\n  ret i32 0\n"
                   "orphan:\n  ret i32 1\n}\n");
  DeadCodeEliminationPass pass;
  runPass(pass, m);
  CHECK_EQ(m.functions()[0].blocks().size(), std::size_t{1});
}

MTIR_TEST("opt.dce", "a loop body is not mistaken for dead code") {
  Module m = parse("func @f(i32 %n) -> i32 {\n"
                   "entry:\n  br head\n"
                   "head:\n  %c = icmp.slt i32 %n, 3\n  br %c ? body : end\n"
                   "body:\n  print.i32 %n\n  br head\n"
                   "end:\n  ret i32 0\n}\n");
  DeadCodeEliminationPass pass;
  runPass(pass, m);
  CHECK_EQ(m.functions()[0].blocks().size(), std::size_t{4});
}

// ==========================================================================
// The pipeline
// ==========================================================================
MTIR_TEST("opt", "level 0 changes nothing") {
  Module m = fnOf("  %a = mul i32 3, 4\n  ret i32 %a");
  const std::string before = printModule(m);
  PassManager pm = defaultPipeline(0);
  pm.run(m);
  CHECK_EQ(printModule(m), before);
}

MTIR_TEST("opt", "the pipeline folds an arithmetic function") {
  Module m = fnOf("  %a = mul i32 3, 4\n  %b = add i32 2, %a\n"
                  "  %c = add i32 %b, 0\n  %d = add i32 %c, %x\n  ret i32 %d");
  const std::size_t before = countInstructions(m);
  PassManager pm = defaultPipeline(1);
  pm.run(m);
  const std::size_t after = countInstructions(m);
  CHECK(after < before);
  CHECK_EQ(opsText(m.functions()[0]), std::string("add ret"));
}

MTIR_TEST("opt", "a constant-false loop disappears entirely") {
  // Folding turns the head's br.cond into a br, and DCE then drops the body.
  // Neither pass could do it alone.
  Module m = parse("func @f() -> void {\n"
                   "entry:\n  br while.head.0\n"
                   "while.head.0:\n  br 0 ? while.body.0 : while.end.0\n"
                   "while.body.0:\n  print.i32 1\n  br while.head.0\n"
                   "while.end.0:\n  ret void\n}\n");
  PassManager pm = defaultPipeline(1);
  pm.run(m);
  const Function &fn = m.functions()[0];
  CHECK(fn.block("while.body.0") == nullptr);
  CHECK_EQ(opsText(fn), std::string("br br ret"));
}

MTIR_TEST("opt", "optimisation preserves well-formedness") {
  const char *sources[] = {
      "func @f(i32 %x) -> i32 {\nentry:\n  %a = mul i32 3, 4\n"
      "  %b = add i32 %a, %x\n  ret i32 %b\n}\n",
      "func @g(i32 %n) -> i32 {\n"
      "entry:\n  %c = icmp.slt i32 %n, 3\n  br %c ? a : b\n"
      "a:\n  ret i32 1\n"
      "b:\n  ret i32 0\n}\n",
  };
  for (const char *src : sources) {
    Module m = parse(src);
    PassManager pm = defaultPipeline(1);
    pm.run(m);
    const auto diags = verify(m);
    CHECK(diags.empty());
    if (!diags.empty())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                mtir::support::format(diags) + printModule(m));
  }
}

MTIR_TEST("opt", "optimised output still round-trips") {
  Module m = fnOf("  %a = mul i32 3, 4\n  %b = add i32 2, %a\n  ret i32 %b");
  PassManager pm = defaultPipeline(1);
  pm.run(m);
  const std::string text = printModule(m);
  ParseResult reparsed = parseCir(text);
  CHECK(reparsed.ok());
  if (reparsed.ok())
    CHECK_EQ(printModule(*reparsed.module), text);
}

MTIR_TEST("opt", "optimisation reaches a fixed point") {
  Module m = fnOf("  %a = mul i32 3, 4\n  %b = add i32 2, %a\n  ret i32 %b");
  PassManager pm = defaultPipeline(1);
  pm.run(m);
  const std::string once = printModule(m);
  PassManager again = defaultPipeline(1);
  CHECK(!again.run(m));
  CHECK_EQ(printModule(m), once);
}

MTIR_TEST("opt", "a trap survives optimisation") {
  Module m = parse("func @div(i32 %a, i32 %b) -> i32 {\nentry:\n"
                   "  %t = sdiv i32 %a, %b\n  ret i32 %t\n}\n");
  PassManager pm = defaultPipeline(1);
  pm.run(m);
  CHECK_EQ(opsText(m.functions()[0]), std::string("sdiv ret"));
}
