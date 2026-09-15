// Tests for the CIR builder and the end-to-end .mini -> CIR -> LLVM pipeline.
//
// Migrated from tests/test_cir_builder.py.  The strongest check here is not
// any single assertion: it is that the CIR the C++ pipeline produces for
// every corpus program is byte-identical to the fixtures in tests/cir/, which
// the Python reference generated.  That is what makes "migrated" a claim about
// behaviour rather than about line count.

#include "mtir_test.h"

#include <cstddef>
#include <string>
#include <vector>

#include "mtir/backend/llvm/EmitLL.h"
#include "mtir/cir/Builder.h"
#include "mtir/cir/CFG.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"
#include "mtir/frontend/Parser.h"
#include "mtir/opt/Pass.h"
#include "mtir/sema/Analyse.h"

#include "TestSupport.h"

using namespace mtir::cir;
using mtir::frontend::parse;

namespace {

/// The whole front end in one call: .mini text -> CIR module.
struct Compiled {
  std::optional<Module> module;
  std::string diagnostics;
};

Compiled compileSource(const std::string &src, const std::string &name = "test") {
  Compiled out;
  auto parsed = parse(src, "<test>");
  if (!parsed.ok()) {
    out.diagnostics = mtir::support::format(parsed.diagnostics);
    return out;
  }
  auto analysis = mtir::sema::analyse(*parsed.program, "<test>");
  if (!analysis.ok()) {
    out.diagnostics = mtir::support::format(analysis.diagnostics);
    return out;
  }
  auto built = build(*parsed.program, *analysis.types, name);
  if (!built.ok()) {
    out.diagnostics = mtir::support::format(built.diagnostics);
    return out;
  }
  out.module = std::move(*built.module);
  return out;
}

std::string cirOf(const std::string &src) {
  Compiled c = compileSource(src);
  return c.module.has_value() ? printModule(*c.module) : "<failed>\n" + c.diagnostics;
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

std::vector<std::string> labelsOf(const Function &fn) {
  std::vector<std::string> out;
  for (const BasicBlock &b : fn.blocks())
    out.push_back(b.label());
  return out;
}

} // namespace

// ==========================================================================
// The worked example
// ==========================================================================
MTIR_TEST("builder", "abs lowers to the Figure 2 module") {
  Compiled c = compileSource("fn abs(x: int) -> int {\n"
                             "    if (x < 0) { return -x; }\n"
                             "    return x;\n}\n");
  CHECK(c.module.has_value());
  if (!c.module.has_value()) {
    mtir::test::reportFailure(__FILE__, __LINE__, c.diagnostics);
    return;
  }
  const Function *fn = c.module->function("abs");
  CHECK(fn != nullptr);
  if (fn == nullptr)
    return;
  CHECK_EQ(opsText(*fn), std::string("icmp.slt br.cond sub ret ret"));
  CHECK_EQ(fn->blocks().size(), std::size_t{3});
  CHECK_EQ(fn->returnType(), Ty::I32);
}

MTIR_TEST("builder", "abs keeps its parameter in a register") {
  // x is never assigned, so it needs no alloca -- the one departure from
  // "every local lives in memory", and why the output matches Figure 2.
  const std::string text = cirOf("fn abs(x: int) -> int {\n"
                                 "    if (x < 0) { return -x; }\n    return x;\n}\n");
  CHECK(text.find("alloca") == std::string::npos);
}

MTIR_TEST("builder", "an assigned parameter does get a slot") {
  Compiled c = compileSource("fn f(x: int) -> int { x = x + 1; return x; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::vector<std::string> ops = opsOf(*c.module->function("f"));
  CHECK(ops.size() >= 2);
  if (ops.size() >= 2) {
    CHECK_EQ(ops[0], std::string("alloca"));
    CHECK_EQ(ops[1], std::string("store"));
  }
}

// ==========================================================================
// Control-flow shapes (docs/cir-spec.md section 5)
// ==========================================================================
MTIR_TEST("builder", "if without an else has three blocks") {
  Compiled c = compileSource("fn f(c: bool) -> int { if (c) { return 1; } return 0; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::vector<std::string> labels = labelsOf(*c.module->function("f"));
  CHECK_EQ(labels.size(), std::size_t{3});
  if (labels.size() == 3) {
    CHECK_EQ(labels[0], std::string("entry"));
    CHECK_EQ(labels[1], std::string("if.then.0"));
    CHECK_EQ(labels[2], std::string("if.end.0"));
  }
}

MTIR_TEST("builder", "no join block is made when both arms return") {
  Compiled c = compileSource(
      "fn f(c: bool) -> int { if (c) { return 1; } else { return 2; } }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  CHECK_EQ(labelsOf(*c.module->function("f")).size(), std::size_t{3});
}

MTIR_TEST("builder", "while uses head/body/end") {
  Compiled c = compileSource("fn f(n: int) { while (n < 3) { } }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Function *fn = c.module->function("f");
  const CFG cfg(*fn);
  CHECK_EQ(labelsOf(*fn).size(), std::size_t{4});
  CHECK(fn->block("while.head.0") != nullptr);
  CHECK(fn->block("while.body.0") != nullptr);
  CHECK(fn->block("while.end.0") != nullptr);
  const auto head = cfg.find("while.head.0");
  CHECK(head.has_value());
  if (head)
    CHECK_EQ(cfg.successors(*head).size(), std::size_t{2});
}

MTIR_TEST("builder", "for uses a separate step block") {
  Compiled c = compileSource("fn f() { for (let i: int = 0; i < 3; i = i + 1) { } }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Function *fn = c.module->function("f");
  CHECK(fn->block("for.head.0") != nullptr);
  CHECK(fn->block("for.body.0") != nullptr);
  CHECK(fn->block("for.step.0") != nullptr);
  CHECK(fn->block("for.end.0") != nullptr);
}

MTIR_TEST("builder", "break goes to the exit block and continue to the step block") {
  Compiled c = compileSource(
      "fn f() {\n"
      "  for (let i: int = 0; i < 9; i = i + 1) {\n"
      "    if (i > 3) { break; }\n"
      "    if (i > 1) { continue; }\n"
      "  }\n}\n");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::string text = printModule(*c.module);
  CHECK(text.find("br for.end.0") != std::string::npos);
  CHECK(text.find("br for.step.0") != std::string::npos);
}

MTIR_TEST("builder", "an infinite for loop has an unconditional head") {
  Compiled c = compileSource("fn f() { for (;;) { } }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Instruction *term = c.module->function("f")->block("for.head.0")->terminator();
  CHECK(term != nullptr);
  if (term != nullptr)
    CHECK_EQ(term->op, Opcode::Br);
}

// ==========================================================================
// Expressions
// ==========================================================================
MTIR_TEST("builder", "expressions are three-address") {
  Compiled c = compileSource("fn f(a: int, b: int, cc: int) -> int { return a + b * cc; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  for (const BasicBlock &b : c.module->function("f")->blocks())
    for (const Instruction &i : b.instructions())
      CHECK(i.args.size() <= 2);
}

MTIR_TEST("builder", "short-circuit && only evaluates the rhs when needed") {
  Compiled c = compileSource("fn f(a: bool, b: bool) -> bool { return a && b; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Function *fn = c.module->function("f");
  CHECK(fn->block("and.rhs.0") != nullptr);
  CHECK(fn->block("and.end.0") != nullptr);
}

MTIR_TEST("builder", "short-circuit || inverts the branch") {
  Compiled c = compileSource("fn f(a: bool, b: bool) -> bool { return a || b; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Function *fn = c.module->function("f");
  const CFG cfg(*fn);
  const auto entry = cfg.find("entry");
  CHECK(entry.has_value());
  if (!entry)
    return;
  // The true edge goes straight to the join.
  CHECK_EQ(cfg.label(cfg.successors(*entry)[0]), std::string("or.end.0"));
}

MTIR_TEST("builder", "negating a literal folds rather than subtracting") {
  // -2147483648 must be one i32 constant, not 0 - 2147483648, which does not
  // fit in i32 at all.
  Compiled c = compileSource("fn f() -> int { return -2147483648; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  CHECK_EQ(opsText(*c.module->function("f")), std::string("ret"));
  CHECK(printModule(*c.module).find("ret i32 -2147483648") != std::string::npos);
}

MTIR_TEST("builder", "integer negation is a subtraction from zero") {
  Compiled c = compileSource("fn f(x: int) -> int { return -x; }");
  CHECK(c.module.has_value());
  if (c.module.has_value())
    CHECK_EQ(opsText(*c.module->function("f")), std::string("sub ret"));
}

MTIR_TEST("builder", "float negation uses neg") {
  Compiled c = compileSource("fn f(x: float) -> float { return -x; }");
  CHECK(c.module.has_value());
  if (c.module.has_value())
    CHECK_EQ(opsText(*c.module->function("f")), std::string("neg ret"));
}

MTIR_TEST("builder", "comparisons pick signed or ordered predicates") {
  Compiled ints = compileSource("fn f(a: int, b: int) -> bool { return a < b; }");
  Compiled floats = compileSource("fn f(a: float, b: float) -> bool { return a < b; }");
  CHECK(ints.module.has_value() && floats.module.has_value());
  if (!ints.module.has_value() || !floats.module.has_value())
    return;
  const std::vector<std::string> intOps = opsOf(*ints.module->function("f"));
  const std::vector<std::string> floatOps = opsOf(*floats.module->function("f"));
  CHECK(!intOps.empty() && !floatOps.empty());
  if (!intOps.empty())
    CHECK_EQ(intOps[0], std::string("icmp.slt"));
  if (!floatOps.empty())
    CHECK_EQ(floatOps[0], std::string("fcmp.olt"));
}

MTIR_TEST("builder", "a widening conversion lowers to sitofp") {
  Compiled c = compileSource("fn g(x: float) { }\nfn f(n: int) { g(n); }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::vector<std::string> ops = opsOf(*c.module->function("f"));
  CHECK(ops.size() >= 2);
  if (ops.size() >= 2) {
    CHECK_EQ(ops[0], std::string("sitofp"));
    CHECK_EQ(ops[1], std::string("call"));
  }
}

MTIR_TEST("builder", "a widened literal costs no instruction") {
  // `let n: long = 1;` should store `1` typed i64, not sext a constant.
  const std::string text = cirOf("fn f() { let n: long = 1; }");
  CHECK(text.find("sext") == std::string::npos);
  CHECK(text.find("store i64 1") != std::string::npos);
}

MTIR_TEST("builder", "builtins lower to intrinsics") {
  Compiled c = compileSource("fn f() { print_int(1); print_float(2.5); }");
  CHECK(c.module.has_value());
  if (c.module.has_value())
    CHECK_EQ(opsText(*c.module->function("f")), std::string("print.i32 print.f64 ret"));
}

MTIR_TEST("builder", "arrays lower to alloca, gep and load/store") {
  Compiled c = compileSource("fn f() -> int {\n"
                             "  let xs: int[4];\n  xs[0] = 7;\n  return xs[0];\n}\n");
  CHECK(c.module.has_value());
  if (c.module.has_value())
    CHECK_EQ(opsText(*c.module->function("f")),
             std::string("alloca gep store gep load ret"));
}

MTIR_TEST("builder", "scalar globals lower to a module global") {
  Compiled c = compileSource(
      "global counter: int = 3;\n"
      "fn f() -> int { counter = counter + 1; return counter; }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  CHECK_EQ(c.module->globals().size(), std::size_t{1});
  CHECK_EQ(c.module->globals()[0].name, std::string("counter"));
  CHECK_EQ(opsText(*c.module->function("f")),
           std::string("load add store load ret"));
}

MTIR_TEST("builder", "recursion needs no forward declaration") {
  Compiled c = compileSource("fn a(n: int) -> int { return b(n); }\n"
                             "fn b(n: int) -> int { return n; }");
  CHECK(c.module.has_value());
  if (c.module.has_value())
    CHECK(printModule(*c.module).find("call i32 @b") != std::string::npos);
}

MTIR_TEST("builder", "code after a return lands in an unreachable block") {
  Compiled c = compileSource("fn f() -> int { return 1; print_int(2); }");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const Function *fn = c.module->function("f");
  const CFG cfg(*fn);
  CHECK(fn->blocks().size() > 1);
  // The extra block exists but nothing reaches it.
  for (std::size_t i = 1; i < fn->blocks().size(); ++i)
    CHECK(!cfg.isReachable(static_cast<BlockId>(i)));
}

// ==========================================================================
// Everything the builder produces must be well formed
// ==========================================================================
MTIR_TEST("builder", "built modules pass the verifier") {
  const char *programs[] = {
      "docs/examples/abs.mini",
      "tests/corpus/valid/arith.mini",
      "tests/corpus/valid/control_flow.mini",
      "tests/corpus/valid/recursion.mini",
      "tests/corpus/boundary/div_edge.mini",
      "tests/corpus/boundary/nesting.mini",
      "tests/corpus/boundary/shift_edge.mini",
  };
  for (const char *path : programs) {
    Compiled c = compileSource(mtir::test::readFile(mtir::test::example(path)), "m");
    CHECK(c.module.has_value());
    if (!c.module.has_value()) {
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" + c.diagnostics);
      continue;
    }
    const auto diags = verify(*c.module);
    CHECK(diags.empty());
    if (!diags.empty())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" +
                                    mtir::support::format(diags));
  }
}

// ==========================================================================
// Parity with the Python reference
// ==========================================================================
MTIR_TEST("builder", "the C++ pipeline reproduces the reference CIR exactly") {
  // tests/cir/*.cir were generated by the Python prototype.  The C++ front
  // end, semantic analysis and builder must produce the same text, character
  // for character -- that is what makes this a migration rather than a
  // rewrite that happens to compile.
  struct Pair {
    const char *source;
    const char *fixture;
  };
  const Pair pairs[] = {
      {"tests/corpus/valid/arith.mini", "tests/cir/arith.cir"},
      {"tests/corpus/valid/control_flow.mini", "tests/cir/control_flow.cir"},
      {"tests/corpus/valid/recursion.mini", "tests/cir/recursion.cir"},
      {"tests/corpus/boundary/div_edge.mini", "tests/cir/div_edge.cir"},
      {"tests/corpus/boundary/nesting.mini", "tests/cir/nesting.cir"},
      {"tests/corpus/boundary/shift_edge.mini", "tests/cir/shift_edge.cir"},
  };
  for (const Pair &p : pairs) {
    const std::string src = mtir::test::readFile(mtir::test::example(p.source));
    const std::string expected = mtir::test::readFile(mtir::test::example(p.fixture));
    CHECK(!src.empty());
    CHECK(!expected.empty());

    // The fixtures were generated with the module named after the file stem.
    std::string stem = p.source;
    stem = stem.substr(stem.find_last_of('/') + 1);
    stem = stem.substr(0, stem.find_last_of('.'));

    Compiled c = compileSource(src, stem);
    CHECK(c.module.has_value());
    if (!c.module.has_value()) {
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(p.source) + ":\n" + c.diagnostics);
      continue;
    }
    CHECK_EQ(printModule(*c.module), expected);
  }
}

// ==========================================================================
// End to end: .mini -> CIR -> optimiser -> LLVM
// ==========================================================================
MTIR_TEST("e2e", "every corpus program reaches valid LLVM") {
  const char *programs[] = {
      "docs/examples/abs.mini",
      "tests/corpus/valid/arith.mini",
      "tests/corpus/valid/control_flow.mini",
      "tests/corpus/valid/recursion.mini",
      "tests/corpus/boundary/div_edge.mini",
      "tests/corpus/boundary/nesting.mini",
      "tests/corpus/boundary/shift_edge.mini",
  };
  for (const char *path : programs) {
    Compiled c = compileSource(mtir::test::readFile(mtir::test::example(path)), "m");
    CHECK(c.module.has_value());
    if (!c.module.has_value())
      continue;

    const auto plain = mtir::backend::llvm::emitModule(*c.module);
    CHECK(plain.ok());
    CHECK(!plain.ir.empty());

    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
    pm.run(*c.module);
    CHECK(verify(*c.module).empty());

    const auto optimised = mtir::backend::llvm::emitModule(*c.module);
    CHECK(optimised.ok());
    if (!optimised.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" +
                                    mtir::support::format(optimised.diagnostics));
  }
}

MTIR_TEST("e2e", "abs.mini reaches the hand-written LLVM shape") {
  Compiled c = compileSource(
      mtir::test::readFile(mtir::test::example("docs/examples/abs.mini")), "abs");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::string ir = mtir::backend::llvm::emitModule(*c.module).ir;
  CHECK(ir.find("define i32 @abs(i32 %x) {") != std::string::npos);
  CHECK(ir.find("%t0 = icmp slt i32 %x, 0") != std::string::npos);
  CHECK(ir.find("%t1 = sub i32 0, %x") != std::string::npos);
  CHECK(ir.find("call i32 (ptr, ...) @printf") != std::string::npos);
}

MTIR_TEST("e2e", "the divergence guards survive the full pipeline") {
  Compiled c = compileSource(
      mtir::test::readFile(mtir::test::example("tests/corpus/boundary/div_edge.mini")),
      "div_edge");
  CHECK(c.module.has_value());
  if (!c.module.has_value())
    return;
  const std::string ir = mtir::backend::llvm::emitModule(*c.module).ir;
  CHECK(ir.find("call void @llvm.trap()") != std::string::npos);
  CHECK(ir.find("icmp eq i32 %b, 0") != std::string::npos);
  CHECK(ir.find("nsw") == std::string::npos);
}
