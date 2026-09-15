// Tests for the textual CIR printer and parser.
//
// Migrated from tests/test_cir_printer.py and tests/test_cir_parser.py.
// The property under test is the one docs/cir-spec.md section 4 states:
//
//     printModule(*parseCir(t).module) == t
//
// It is what makes .cir a real interchange format rather than a debug dump,
// and therefore what lets each back end be developed against a checked-in
// file with no front end involved.

#include "mtir_test.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

#include "mtir/cir/Parser.h"
#include "mtir/cir/Printer.h"
#include "mtir/opt/Pass.h"

#include "TestSupport.h"

using namespace mtir::cir;
using mtir::test::demoAbsModule;
using mtir::test::example;
using mtir::test::readFile;

namespace {

std::string roundTrip(const std::string &text) {
  ParseResult result = parseCir(text);
  if (!result.ok())
    return "<parse failed>\n" + mtir::support::format(result.diagnostics);
  return printModule(*result.module);
}

/// Wrap a single instruction in the smallest function that can hold it.
std::string wrapInstruction(const std::string &body,
                            const std::string &header = "func @f(i32 %x) -> void {") {
  return header + "\nentry:\n" + body + "\n  ret void\n}\n";
}

} // namespace

// ==========================================================================
// The golden file
// ==========================================================================
MTIR_TEST("cir.printer", "the abs module prints byte-identically to the golden file") {
  const std::string golden = readFile(example("docs/examples/abs.cir"));
  CHECK(!golden.empty());
  CHECK_EQ(printModule(demoAbsModule()), golden);
}

MTIR_TEST("cir.printer", "printing is deterministic") {
  CHECK_EQ(printModule(demoAbsModule()), printModule(demoAbsModule()));
}

MTIR_TEST("cir.parser", "the golden file round-trips byte for byte") {
  const std::string golden = readFile(example("docs/examples/abs.cir"));
  CHECK(!golden.empty());
  CHECK_EQ(roundTrip(golden), golden);
}

MTIR_TEST("cir.parser", "parsing the golden file rebuilds the hand-built module") {
  // Text and object graph agree, not just text and text.
  const std::string golden = readFile(example("docs/examples/abs.cir"));
  ParseResult result = parseCir(golden, "abs");
  CHECK(result.ok());
  if (result.ok())
    CHECK(*result.module == demoAbsModule());
}

// ==========================================================================
// Instruction shapes
// ==========================================================================
MTIR_TEST("cir.printer", "instruction formats match the textual grammar") {
  const Reg x{"x", Ty::I32};
  const Reg t{"t0", Ty::I32};

  CHECK_EQ(printInstruction(
               Instruction::binary(Opcode::Add, Ty::I32, t, x, ConstInt{1, Ty::I32})),
           std::string("%t0 = add i32 %x, 1"));
  CHECK_EQ(printInstruction(Instruction::br("exit")), std::string("br exit"));
  CHECK_EQ(printInstruction(Instruction::ret(Ty::I32, x)), std::string("ret i32 %x"));
  CHECK_EQ(printInstruction(Instruction::ret()), std::string("ret void"));
  CHECK_EQ(printInstruction(Instruction::trap()), std::string("trap"));
  CHECK_EQ(printInstruction(Instruction::brCond(Reg{"c", Ty::I1}, "a", "b")),
           std::string("br %c ? a : b"));
}

MTIR_TEST("cir.printer", "a call prints with and without a destination") {
  CHECK_EQ(printInstruction(Instruction::call(Ty::I32, Reg{"t0", Ty::I32}, "f",
                                              {ConstInt{3, Ty::I32}})),
           std::string("%t0 = call i32 @f(3)"));
  CHECK_EQ(printInstruction(Instruction::call(Ty::Void, std::nullopt, "g", {})),
           std::string("call void @g()"));
}

MTIR_TEST("cir.parser", "every instruction shape round-trips") {
  const char *bodies[] = {
      "  %t0 = add i32 %x, 1",
      "  %t0 = sub i64 1, 2",
      "  %t0 = icmp.ult i32 %x, 7",
      "  %t0 = fcmp.olt f64 1.5, 2.5",
      "  %t0 = sext i64 %x",
      "  %t0 = sitofp f64 %x",
      "  %t0 = not i32 %x",
      "  %t0 = neg i32 %x",
      "  %t0 = alloca i32",
      "  %t0 = alloca i32, 8",
      "  %t0 = load i32 %p",
      "  store i32 1, %p",
      "  %t0 = gep i32 %p, 3",
      "  %t0 = call i32 @g(1, %x)",
      "  call void @g()",
      "  print.i32 %x",
      "  print.f64 2.5",
      "  trap",
  };
  for (const char *body : bodies) {
    // %p is a pointer, so the fixture gives the function a ptr parameter too.
    const std::string text =
        wrapInstruction(body, "func @f(i32 %x, ptr %p) -> void {");
    CHECK_EQ(roundTrip(text), text);
  }
}

MTIR_TEST("cir.parser", "globals round-trip") {
  const std::string text =
      "global @counter : i32 = 0\n"
      "global @rate : f64 = 2.5\n"
      "global @table : i32[8]\n"
      "\n"
      "func @f() -> void {\nentry:\n  ret void\n}\n";
  CHECK_EQ(roundTrip(text), text);
}

MTIR_TEST("cir.parser", "both branch forms round-trip") {
  const std::string text =
      "func @f(i1 %c) -> void {\n"
      "entry:\n  br %c ? a : b\n"
      "a:\n  br b\n"
      "b:\n  ret void\n}\n";
  CHECK_EQ(roundTrip(text), text);
}

MTIR_TEST("cir.parser", "dotted labels survive the round trip") {
  // The builder's label scheme is if.then.N / while.head.N and so on.
  const std::string text =
      "func @f(i1 %c) -> i32 {\n"
      "entry:\n  br %c ? if.then.0 : if.end.0\n"
      "if.then.0:\n  ret i32 1\n"
      "if.end.0:\n  ret i32 0\n}\n";
  CHECK_EQ(roundTrip(text), text);
}

// ==========================================================================
// Type recovery
// ==========================================================================
MTIR_TEST("cir.parser", "a comparison defines i1 from an i32 operand type") {
  ParseResult r = parseCir("func @f(i32 %x) -> i1 {\nentry:\n"
                           "  %t0 = icmp.slt i32 %x, 0\n  ret i1 %t0\n}\n");
  CHECK(r.ok());
  if (!r.ok())
    return;
  const Instruction &cmp = r.module->functions()[0].blocks()[0].instructions()[0];
  CHECK_EQ(cmp.ty, Ty::I32);                 // the printed operand type
  CHECK(cmp.dest.has_value());
  CHECK_EQ(cmp.dest->ty, Ty::I1);            // rule 7
}

MTIR_TEST("cir.parser", "alloca and gep define pointers") {
  ParseResult r = parseCir("func @f() -> void {\nentry:\n"
                           "  %p = alloca i32, 4\n"
                           "  %q = gep i32 %p, 1\n"
                           "  ret void\n}\n");
  CHECK(r.ok());
  if (!r.ok())
    return;
  const auto &instrs = r.module->functions()[0].blocks()[0].instructions();
  CHECK_EQ(instrs[0].dest->ty, Ty::Ptr);
  CHECK_EQ(instrs[1].dest->ty, Ty::Ptr);
}

MTIR_TEST("cir.parser", "a use may precede its definition in the text") {
  // A loop back edge legitimately puts the use in an earlier block.
  ParseResult r = parseCir("func @f() -> i32 {\n"
                           "entry:\n  br head\n"
                           "head:\n  ret i32 %v\n"
                           "body:\n  %v = add i32 1, 2\n  br head\n}\n");
  CHECK(r.ok());
  if (!r.ok())
    return;
  const Function &fn = r.module->functions()[0];
  const Value &arg = fn.block("head")->instructions()[0].args[0];
  CHECK(arg == Value{Reg{"v", Ty::I32}});
}

MTIR_TEST("cir.parser", "float and integer literals are distinguished") {
  ParseResult r = parseCir("func @f() -> void {\nentry:\n"
                           "  %a = add i32 1, 2\n"
                           "  %b = fadd f64 1.5, 2.0\n"
                           "  ret void\n}\n");
  CHECK(r.ok());
  if (!r.ok())
    return;
  const auto &instrs = r.module->functions()[0].blocks()[0].instructions();
  CHECK(instrs[0].args[0] == Value{ConstInt{1, Ty::I32}});
  CHECK(instrs[1].args[0] == Value{ConstFloat{1.5}});
}

MTIR_TEST("cir.parser", "negative literals parse") {
  ParseResult r = parseCir("func @f() -> i32 {\nentry:\n  ret i32 -2147483648\n}\n");
  CHECK(r.ok());
  if (!r.ok())
    return;
  const Value &v = r.module->functions()[0].blocks()[0].instructions()[0].args[0];
  CHECK(v == Value{ConstInt{-2147483648LL, Ty::I32}});
}

// ==========================================================================
// Float spelling
// ==========================================================================
MTIR_TEST("cir.printer", "doubles print in the shortest round-tripping form") {
  CHECK_EQ(printDouble(1.5), std::string("1.5"));
  CHECK_EQ(printDouble(100.0), std::string("100.0"));
  CHECK_EQ(printDouble(0.0), std::string("0.0"));
  CHECK_EQ(printDouble(-2.5), std::string("-2.5"));
  CHECK_EQ(printDouble(0.1), std::string("0.1"));
}

MTIR_TEST("cir.printer", "an exponent form keeps a decimal point") {
  // LLVM's lexer only treats a number as floating point once it has seen a
  // '.', so "1e-05" would lex as the integer 1.
  const std::string text = printDouble(1e-05);
  const std::size_t e = text.find('e');
  CHECK(e != std::string::npos);
  CHECK(text.substr(0, e).find('.') != std::string::npos);
}

MTIR_TEST("cir.parser", "non-finite doubles the printer emits are read back") {
  // Regression: constant folding can overflow to infinity
  // (fmul f64 1.0e308, 10.0), the printer spells that `inf`, and the parser
  // used to reject it -- so the compiler could not read its own output.
  CHECK_EQ(printDouble(std::numeric_limits<double>::infinity()), std::string("inf"));
  CHECK_EQ(printDouble(-std::numeric_limits<double>::infinity()), std::string("-inf"));

  const std::string text =
      "func @f() -> f64 {\nentry:\n  %a = fadd f64 inf, -inf\n  ret f64 %a\n}\n";
  CHECK_EQ(roundTrip(text), text);

  ParseResult r = parseCir(text);
  CHECK(r.ok());
  if (!r.ok())
    return;
  const Value &lhs = r.module->functions()[0].blocks()[0].instructions()[0].args[0];
  const ConstFloat *f = asConstFloat(lhs);
  CHECK(f != nullptr);
  CHECK(f != nullptr && std::isinf(f->value) && f->value > 0);
}

MTIR_TEST("cir.parser", "a folded overflow round-trips end to end") {
  Module m = *parseCir("func @f() -> f64 {\nentry:\n"
                       "  %t = fmul f64 1.0e308, 10.0\n  ret f64 %t\n}\n").module;
  mtir::opt::PassManager pm = mtir::opt::defaultPipeline(1);
  pm.run(m);
  const std::string folded = printModule(m);
  CHECK(folded.find("inf") != std::string::npos);
  // The whole point: the printer's own output must parse.
  CHECK_EQ(roundTrip(folded), folded);
}

MTIR_TEST("cir.printer", "large magnitudes still round-trip through the parser") {
  const double values[] = {1e100, 1e-100, 123456789.125, -0.0009765625};
  for (double v : values) {
    const std::string text =
        "func @f() -> f64 {\nentry:\n  ret f64 " + printDouble(v) + "\n}\n";
    CHECK_EQ(roundTrip(text), text);
  }
}

// ==========================================================================
// Diagnostics
// ==========================================================================
MTIR_TEST("cir.parser", "malformed input is reported, not crashed") {
  struct Case {
    const char *text;
    const char *fragment;
  };
  const Case cases[] = {
      {"func @f() -> i32 {\nentry:\n  %t = frobnicate i32 1\n  ret i32 0\n}\n",
       "unknown opcode"},
      {"func @f() -> i32 {\nentry:\n  ret i32 %missing\n}\n", "never defined"},
      {"func @f() -> i32 {\nentry:\n  ret i32 0\n", "unterminated function"},
      {"func @f() -> quux {\nentry:\n  ret i32 0\n}\n", "unknown type"},
      {"not a declaration\n", "expected 'func' or 'global'"},
      {"func @f() -> i32 {\n  ret i32 0\n}\n", "instruction before the first block label"},
      {"func @f() -> i32 {\nentry:\n  %t = neg i32 1, 2\n  ret i32 0\n}\n",
       "takes 1 operand"},
      {"func @f( -> i32 {\nentry:\n  ret i32 0\n}\n", "type name"},
  };
  for (const Case &c : cases) {
    ParseResult r = parseCir(c.text);
    CHECK(!r.ok());
    CHECK(mtir::test::mentions(r.diagnostics, c.fragment));
    if (!mtir::test::mentions(r.diagnostics, c.fragment))
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string("for input:\n") + c.text + "got:\n" +
                                    mtir::support::format(r.diagnostics));
  }
}

MTIR_TEST("cir.parser", "a diagnostic carries a line and a column") {
  ParseResult r = parseCir("func @f() -> i32 {\nentry:\n  ret i32 %nope\n}\n");
  CHECK(!r.ok());
  CHECK(!r.diagnostics.empty());
  if (r.diagnostics.empty())
    return;
  CHECK_EQ(r.diagnostics[0].location.loc.line, 3);
  // "  ret i32 %nope" -- the register starts at column 11.
  CHECK_EQ(r.diagnostics[0].location.loc.col, 11);
}

MTIR_TEST("cir.parser", "a parse error is reported once, not twice") {
  // The parser dissects each line twice (definitions, then operands); only
  // the second pass may report.
  ParseResult r = parseCir("func @f() -> i32 {\nentry:\n  %t = bogus i32 1\n}\n");
  CHECK(!r.ok());
  std::size_t unknownOpcode = 0;
  for (const auto &d : r.diagnostics)
    if (d.message.find("unknown opcode") != std::string::npos)
      ++unknownOpcode;
  CHECK_EQ(unknownOpcode, std::size_t{1});
}
