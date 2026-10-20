// Regression coverage over real programs.
//
// Every other C++ test in this suite uses a hand-written .cir snippet: short,
// regular, and written by the same person as the code under test.  The
// fixtures in tests/cir/ are different -- they are the CIR for the MiniLang
// corpus, so they contain loops, nested branches, recursion, mutual
// recursion, globals and the three docs/divergence.md boundary cases.
//
// They were introduced as a migration scaffold, while there was no C++ front
// end to build CIR with.  They are kept now for a better reason: a back end
// can be tested with no front end in the picture at all, which is the whole
// point of having a textual IR (docs/cir-spec.md section 1).  Deleting them
// would quietly make every back-end test depend on the front end being
// correct.  BuilderTests.cpp pins the front end against them separately.
//
// See tests/cir/README.md for provenance and how to regenerate.

#include "mtir_test.h"

#include <cstddef>
#include <string>
#include <vector>

#include "mtir/backend/llvm/EmitLL.h"
#include "mtir/cir/Parser.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"
#include "mtir/opt/Pass.h"

#include "TestSupport.h"

using namespace mtir::cir;
using mtir::test::example;
using mtir::test::readFile;

namespace {

/// Every fixture, with the MiniLang program it was lowered from.
struct Fixture {
  const char *name;
  const char *source;
};

const Fixture kFixtures[] = {
    {"arith", "tests/corpus/valid/arith.mini"},
    {"control_flow", "tests/corpus/valid/control_flow.mini"},
    {"recursion", "tests/corpus/valid/recursion.mini"},
    {"div_edge", "tests/corpus/boundary/div_edge.mini"},
    {"nesting", "tests/corpus/boundary/nesting.mini"},
    {"shift_edge", "tests/corpus/boundary/shift_edge.mini"},
};

std::string fixtureText(const Fixture &f) {
  return readFile(example(std::string("tests/cir/") + f.name + ".cir"));
}

/// Parse a fixture, reporting the parse diagnostics on failure rather than a
/// bare "false".
bool parseFixture(const Fixture &f, Module &out, const char *file, int line) {
  const std::string text = fixtureText(f);
  if (text.empty()) {
    mtir::test::reportFailure(file, line,
                              std::string("fixture is missing or empty: ") + f.name);
    return false;
  }
  ParseResult r = parseCir(text, f.name);
  if (!r.ok()) {
    mtir::test::reportFailure(file, line,
                              std::string(f.name) + " failed to parse:\n" +
                                  mtir::support::format(r.diagnostics));
    return false;
  }
  out = std::move(*r.module);
  return true;
}

#define PARSE_FIXTURE(f, out)                                                  \
  if (!parseFixture((f), (out), __FILE__, __LINE__))                           \
  continue

std::size_t countOpcode(const Module &m, Opcode op) {
  std::size_t n = 0;
  for (const Function &fn : m.functions())
    for (const BasicBlock &b : fn.blocks())
      for (const Instruction &i : b.instructions())
        if (i.op == op)
          ++n;
  return n;
}

} // namespace

// ==========================================================================
// Parse, print, round-trip
// ==========================================================================
MTIR_TEST("corpus", "every fixture parses") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    CHECK(!m.functions().empty());
  }
}

MTIR_TEST("corpus", "every fixture round-trips byte for byte") {
  // The property of docs/cir-spec.md section 4, on real programs rather than
  // on snippets chosen to be easy.
  for (const Fixture &f : kFixtures) {
    const std::string text = fixtureText(f);
    Module m;
    PARSE_FIXTURE(f, m);
    CHECK_EQ(printModule(m), text);
  }
}

MTIR_TEST("corpus", "printing a fixture twice gives the same bytes") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    CHECK_EQ(printModule(m), printModule(m));
  }
}

// ==========================================================================
// Verification
// ==========================================================================
MTIR_TEST("corpus", "every fixture is well formed") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    const auto diags = verify(m);
    CHECK(diags.empty());
    if (!diags.empty())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(f.name) + ":\n" +
                                    mtir::support::format(diags));
  }
}

// ==========================================================================
// Optimisation
// ==========================================================================
MTIR_TEST("corpus", "optimisation preserves well-formedness") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
    pm.run(m);
    const auto diags = verify(m);
    CHECK(diags.empty());
    if (!diags.empty())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(f.name) + " after --opt=1:\n" +
                                    mtir::support::format(diags) + printModule(m));
  }
}

MTIR_TEST("corpus", "optimised output still round-trips") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
    pm.run(m);
    const std::string text = printModule(m);
    ParseResult again = parseCir(text, f.name);
    CHECK(again.ok());
    if (again.ok())
      CHECK_EQ(printModule(*again.module), text);
  }
}

MTIR_TEST("corpus", "optimisation never grows a function") {
  // Every pass only ever removes instructions, so the count is monotone.
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    const std::size_t before = mtir::opt::countInstructions(m);
    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
    pm.run(m);
    CHECK(mtir::opt::countInstructions(m) <= before);
  }
}

MTIR_TEST("corpus", "optimisation reaches a fixed point on real programs") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    mtir::opt::PassManager first = mtir::opt::defaultPipeline(1);
    first.run(m);
    const std::string once = printModule(m);
    mtir::opt::PassManager second = mtir::opt::defaultPipeline(1);
    CHECK(!second.run(m));
    CHECK_EQ(printModule(m), once);
  }
}

MTIR_TEST("corpus", "arith folds to constants but keeps its output") {
  // tests/corpus/valid/arith.mini is all compile-time arithmetic, so folding
  // should remove every mul and srem while leaving the print.i32 alone.
  Module m;
  const Fixture &f = kFixtures[0];
  ParseResult r = parseCir(fixtureText(f), f.name);
  CHECK(r.ok());
  if (!r.ok())
    return;
  m = std::move(*r.module);
  const std::size_t printsBefore = countOpcode(m, Opcode::PrintI32);
  mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
  pm.run(m);
  CHECK_EQ(countOpcode(m, Opcode::Mul), std::size_t{0});
  CHECK_EQ(countOpcode(m, Opcode::SRem), std::size_t{0});
  CHECK_EQ(countOpcode(m, Opcode::PrintI32), printsBefore);
}

MTIR_TEST("corpus", "a trapping division survives optimisation") {
  // div_edge exists to exercise docs/divergence.md rows 1 and 2.  No pass may
  // delete the sdiv, because deleting it would delete the trap.
  Module m;
  const Fixture &f = kFixtures[3];
  ParseResult r = parseCir(fixtureText(f), f.name);
  CHECK(r.ok());
  if (!r.ok())
    return;
  m = std::move(*r.module);
  CHECK(countOpcode(m, Opcode::SDiv) > 0);
  mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
  pm.run(m);
  CHECK(countOpcode(m, Opcode::SDiv) > 0);
}

// ==========================================================================
// LLVM emission
// ==========================================================================
MTIR_TEST("corpus", "every fixture emits LLVM without diagnostics") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    const auto result = mtir::backend::llvm::emitModule(m);
    CHECK(result.ok());
    if (!result.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(f.name) + ":\n" +
                                    mtir::support::format(result.diagnostics));
  }
}

MTIR_TEST("corpus", "every optimised fixture emits LLVM without diagnostics") {
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
    pm.run(m);
    const auto result = mtir::backend::llvm::emitModule(m);
    CHECK(result.ok());
    if (!result.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(f.name) + " after --opt=1:\n" +
                                    mtir::support::format(result.diagnostics));
  }
}

MTIR_TEST("corpus", "the divergence guards fire on the boundary programs") {
  // shift_edge: every shift count is a literal, so row 3 masking happens at
  // compile time and no `and` survives into the IR.
  Module shifts;
  ParseResult rs = parseCir(fixtureText(kFixtures[5]), "shift_edge");
  CHECK(rs.ok());
  if (rs.ok()) {
    shifts = std::move(*rs.module);
    const std::string ir = mtir::backend::llvm::emitModule(shifts).ir;
    // 1 << 32 becomes 1 << 0 and 1 << 33 becomes 1 << 1.
    CHECK(ir.find("shl i32 1, 0") != std::string::npos);
    CHECK(ir.find("shl i32 1, 1") != std::string::npos);
    CHECK(ir.find("shl i32 1, 32") == std::string::npos);
    CHECK(ir.find("shl i32 1, 33") == std::string::npos);
  }

  // div_edge: rows 1 and 2 put a trap block in front of the sdiv.
  Module div;
  ParseResult rd = parseCir(fixtureText(kFixtures[3]), "div_edge");
  CHECK(rd.ok());
  if (rd.ok()) {
    div = std::move(*rd.module);
    const std::string ir = mtir::backend::llvm::emitModule(div).ir;
    CHECK(ir.find("call void @llvm.trap()") != std::string::npos);
    CHECK(ir.find("icmp eq i32 %b, 0") != std::string::npos);
    CHECK(ir.find("icmp eq i32 %a, -2147483648") != std::string::npos);
  }
}

MTIR_TEST("corpus", "no emitted IR carries nsw or nuw") {
  // docs/divergence.md row 4, checked across every real program rather than
  // on one hand-written instruction.
  for (const Fixture &f : kFixtures) {
    Module m;
    PARSE_FIXTURE(f, m);
    const std::string ir = mtir::backend::llvm::emitModule(m).ir;
    CHECK(ir.find("nsw") == std::string::npos);
    CHECK(ir.find("nuw") == std::string::npos);
  }
}
