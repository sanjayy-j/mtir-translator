// Tests for the LLVM IR back end.
//
// Migrated from tests/test_llvm_backend.py.  Two layers of checking:
//
//   * assertStructurallyValid re-reads the emitted text and enforces the
//     invariants LLVM itself enforces -- one terminator per block, every
//     branch target defined, every register defined before use, no duplicate
//     labels.  It runs everywhere, including a machine with no LLVM.
//   * The llvm-as test registered by cmake/LlvmAsCheck.cmake runs the real
//     assembler when CMake finds it.  That is the authority; this file keeps
//     the failure message readable and the loop fast.

#include "mtir_test.h"

#include <cctype>
#include <set>
#include <sstream>
#include <vector>

#include "mtir/backend/llvm/EmitLL.h"
#include "mtir/backend/llvm/TypeMap.h"
#include "mtir/cir/Parser.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"

#include "TestSupport.h"

using namespace mtir::cir;
using mtir::backend::llvm::emitModule;
using mtir::backend::llvm::llvmType;
using mtir::test::demoAbsModule;
using mtir::test::example;
using mtir::test::readFile;

namespace {

bool startsWith(const std::string &text, const std::string &prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    lines.push_back(line);
  return lines;
}

std::string trim(const std::string &s) {
  const std::size_t first = s.find_first_not_of(" \t");
  if (first == std::string::npos)
    return {};
  const std::size_t last = s.find_last_not_of(" \t");
  return s.substr(first, last - first + 1);
}

/// Names of the form %foo or %g.3 appearing in a line.
std::vector<std::string> registersIn(const std::string &line) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] != '%')
      continue;
    std::size_t j = i + 1;
    while (j < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[j])) != 0 ||
            line[j] == '_' || line[j] == '.'))
      ++j;
    if (j > i + 1)
      out.push_back(line.substr(i, j - i));
    i = j - 1;
  }
  return out;
}

/// Everything after "label %" on a line.
std::vector<std::string> branchTargetsIn(const std::string &line) {
  std::vector<std::string> out;
  const std::string marker = "label %";
  std::size_t pos = 0;
  while ((pos = line.find(marker, pos)) != std::string::npos) {
    std::size_t j = pos + marker.size();
    const std::size_t start = j;
    while (j < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[j])) != 0 ||
            line[j] == '_' || line[j] == '.'))
      ++j;
    out.push_back(line.substr(start, j - start));
    pos = j;
  }
  return out;
}

bool isTerminatorLine(const std::string &line) {
  return startsWith(line, "br ") || startsWith(line, "ret ") ||
         line == "ret void" || line == "unreachable" || startsWith(line, "switch ");
}

/// Check the emitted module against the rules llvm-as would enforce, and
/// return every problem found.  Returning them rather than reporting directly
/// is what lets the validator itself be tested below.
std::vector<std::string> structuralProblems(const std::string &ir) {
  std::vector<std::string> problems;
  const auto fail = [&](const std::string &message) { problems.push_back(message); };

  bool inFunction = false;
  bool blockTerminated = true;
  std::set<std::string> labels;
  std::set<std::string> defined;
  std::vector<std::string> targets;
  std::vector<std::string> usedBeforeDef;

  for (const std::string &raw : splitLines(ir)) {
    const std::string text = trim(raw);
    if (text.empty() || text[0] == ';')
      continue;

    if (startsWith(text, "define ")) {
      if (inFunction)
        fail("nested 'define'");
      inFunction = true;
      blockTerminated = true;
      labels.clear();
      defined.clear();
      targets.clear();
      usedBeforeDef.clear();
      const std::size_t open = text.find('(');
      const std::size_t close = text.rfind(')');
      if (open != std::string::npos && close != std::string::npos && close > open)
        for (const std::string &r : registersIn(text.substr(open, close - open)))
          defined.insert(r);
      continue;
    }

    if (!inFunction)
      continue;

    if (text == "}") {
      if (!blockTerminated)
        fail("function ends with an unterminated block");
      for (const std::string &t : targets)
        if (labels.count(t) == 0)
          fail("branch to a label that does not exist: " + t);
      for (const std::string &r : usedBeforeDef)
        fail("register used before it is defined: " + r);
      inFunction = false;
      continue;
    }

    if (text.size() > 1 && text.back() == ':' && text.find(' ') == std::string::npos) {
      const std::string label = text.substr(0, text.size() - 1);
      if (!blockTerminated)
        fail("the block before '" + label + "' has no terminator");
      if (!labels.insert(label).second)
        fail("duplicate label: " + label);
      blockTerminated = false;
      continue;
    }

    if (blockTerminated)
      fail("instruction after a terminator: " + text);

    for (const std::string &t : branchTargetsIn(text))
      targets.push_back(t);

    // Split off the destination, and ignore the label operands when looking
    // for register uses.
    std::string rhs = text;
    std::string dest;
    const std::size_t eq = text.find(" = ");
    if (eq != std::string::npos && text[0] == '%') {
      dest = trim(text.substr(0, eq));
      rhs = text.substr(eq + 3);
    }
    std::string scrubbed;
    std::size_t pos = 0;
    while (pos < rhs.size()) {
      const std::size_t marker = rhs.find("label %", pos);
      if (marker == std::string::npos) {
        scrubbed += rhs.substr(pos);
        break;
      }
      scrubbed += rhs.substr(pos, marker - pos);
      std::size_t j = marker + 7;
      while (j < rhs.size() && (std::isalnum(static_cast<unsigned char>(rhs[j])) != 0 ||
                                rhs[j] == '_' || rhs[j] == '.'))
        ++j;
      pos = j;
    }
    for (const std::string &r : registersIn(scrubbed))
      if (defined.count(r) == 0)
        usedBeforeDef.push_back(r);
    if (!dest.empty())
      defined.insert(dest);

    if (isTerminatorLine(text))
      blockTerminated = true;
  }

  if (inFunction)
    fail("unterminated 'define'");

  return problems;
}

void assertStructurallyValid(const std::string &ir, const char *file, int line) {
  for (const std::string &problem : structuralProblems(ir))
    mtir::test::reportFailure(file, line,
                              problem + "\n--- emitted IR ---\n" + ir);
}

#define CHECK_VALID_IR(ir) assertStructurallyValid((ir), __FILE__, __LINE__)

std::string emitOf(const Module &m) { return emitModule(m).ir; }

/// Emit LLVM for a .cir source string.
std::string llOf(const std::string &cirText) {
  ParseResult parsed = parseCir(cirText);
  if (!parsed.ok())
    return "<parse failed>\n" + mtir::support::format(parsed.diagnostics);
  return emitOf(*parsed.module);
}

/// Wrap one instruction in the smallest function that can hold it.
std::string wrap(const std::string &body,
                 const std::string &header = "func @f(i32 %x, i32 %y) -> void {") {
  return llOf(header + "\nentry:\n" + body + "\n  ret void\n}\n");
}

} // namespace

// ==========================================================================
// The worked example
// ==========================================================================
MTIR_TEST("llvm", "abs matches the hand-written LLVM IR") {
  // The generated @abs is identical to the hand-written one in
  // docs/examples/abs.ll, which CI already validates with llvm-as.
  const std::string ir = emitOf(demoAbsModule());
  std::vector<std::string> body;
  for (const std::string &line : splitLines(ir))
    if (!line.empty() && line[0] != ';')
      body.push_back(line);

  const std::vector<std::string> expected{
      "define i32 @abs(i32 %x) {",
      "entry:",
      "  %t0 = icmp slt i32 %x, 0",
      "  br i1 %t0, label %then, label %exit",
      "then:",
      "  %t1 = sub i32 0, %x",
      "  ret i32 %t1",
      "exit:",
      "  ret i32 %x",
      "}",
  };
  CHECK_EQ(body.size(), expected.size());
  for (std::size_t i = 0; i < expected.size() && i < body.size(); ++i)
    CHECK_EQ(body[i], expected[i]);
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm", "the generated abs.ll matches the checked-in golden file") {
  const std::string golden = readFile(example("docs/examples/abs.gen.ll"));
  CHECK(!golden.empty());
  const std::string cir = readFile(example("docs/examples/abs.cir"));
  ParseResult parsed = parseCir(cir, "abs");
  CHECK(parsed.ok());
  if (parsed.ok())
    CHECK_EQ(emitOf(*parsed.module), golden);
}

MTIR_TEST("llvm", "emission is deterministic") {
  CHECK_EQ(emitOf(demoAbsModule()), emitOf(demoAbsModule()));
}

// ==========================================================================
// Type mapping
// ==========================================================================
MTIR_TEST("llvm", "type mapping") {
  CHECK_EQ(std::string(llvmType(Ty::I1)), std::string("i1"));
  CHECK_EQ(std::string(llvmType(Ty::I32)), std::string("i32"));
  CHECK_EQ(std::string(llvmType(Ty::I64)), std::string("i64"));
  CHECK_EQ(std::string(llvmType(Ty::F64)), std::string("double"));
  CHECK_EQ(std::string(llvmType(Ty::Ptr)), std::string("ptr"));
  CHECK_EQ(std::string(llvmType(Ty::Void)), std::string("void"));
}

// ==========================================================================
// Instruction selection
// ==========================================================================
MTIR_TEST("llvm", "opcode lowering") {
  struct Case {
    const char *cir;
    const char *llvm;
  };
  const Case cases[] = {
      {"  %t = add i32 %x, %y", "%t = add i32 %x, %y"},
      {"  %t = mul i32 %x, %y", "%t = mul i32 %x, %y"},
      {"  %t = and i32 %x, %y", "%t = and i32 %x, %y"},
      {"  %t = icmp.slt i32 %x, %y", "%t = icmp slt i32 %x, %y"},
      {"  %t = icmp.uge i32 %x, %y", "%t = icmp uge i32 %x, %y"},
      {"  %t = sext i64 %x", "%t = sext i32 %x to i64"},
      {"  %t = trunc i32 %x", "%t = trunc i32 %x to i32"},
      {"  %t = sitofp f64 %x", "%t = sitofp i32 %x to double"},
      {"  %t = not i32 %x", "%t = xor i32 %x, -1"},
      {"  %t = neg i32 %x", "%t = sub i32 0, %x"},
      {"  print.i32 %x", "call i32 (ptr, ...) @printf(ptr @.fmt.i32, i32 %x)"},
  };
  for (const Case &c : cases) {
    const std::string ir = wrap(c.cir);
    CHECK(contains(ir, c.llvm));
    if (!contains(ir, c.llvm))
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string("for ") + c.cir + " expected " + c.llvm +
                                    "\ngot:\n" + ir);
  }
}

MTIR_TEST("llvm", "memory operations") {
  const std::string ir = llOf("func @f(ptr %p) -> void {\nentry:\n"
                              "  %a = alloca i32\n"
                              "  %b = alloca i32, 8\n"
                              "  %t = load i32 %p\n"
                              "  store i32 1, %p\n"
                              "  ret void\n}\n");
  CHECK(contains(ir, "%a = alloca i32"));
  CHECK(contains(ir, "%b = alloca i32, i32 8"));
  CHECK(contains(ir, "%t = load i32, ptr %p"));
  CHECK(contains(ir, "store i32 1, ptr %p"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm", "float operations") {
  const std::string ir = llOf("func @f(f64 %x) -> void {\nentry:\n"
                              "  %a = fadd f64 %x, 1.5\n"
                              "  %b = neg f64 %x\n"
                              "  %c = fcmp.olt f64 %x, 2.0\n"
                              "  print.f64 %a\n  ret void\n}\n");
  CHECK(contains(ir, "%a = fadd double %x, 1.5"));
  CHECK(contains(ir, "%b = fneg double %x"));
  CHECK(contains(ir, "%c = fcmp olt double %x, 2.0"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm", "boolean constants are spelled true and false") {
  const std::string ir = llOf("func @f() -> i1 {\nentry:\n"
                              "  br 1 ? a : b\n"
                              "a:\n  ret i1 1\n"
                              "b:\n  ret i1 0\n}\n");
  CHECK(contains(ir, "br i1 true, label %a, label %b"));
  CHECK(contains(ir, "ret i1 true"));
  CHECK(contains(ir, "ret i1 false"));
}

MTIR_TEST("llvm", "a void call is emitted without a destination") {
  const std::string ir = llOf("func @g() -> void {\nentry:\n  ret void\n}\n"
                              "func @f() -> void {\nentry:\n  call void @g()\n  ret void\n}\n");
  CHECK(contains(ir, "  call void @g()"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm", "call arguments are typed from the callee signature") {
  const std::string ir =
      llOf("func @g(i64 %a) -> void {\nentry:\n  ret void\n}\n"
           "func @f() -> void {\nentry:\n  call void @g(3)\n  ret void\n}\n");
  CHECK(contains(ir, "call void @g(i64 3)"));
}

MTIR_TEST("llvm", "a wrong argument count is reported, not emitted") {
  ParseResult parsed =
      parseCir("func @g(i32 %a) -> void {\nentry:\n  ret void\n}\n"
               "func @f() -> void {\nentry:\n  call void @g(1, 2)\n  ret void\n}\n");
  CHECK(parsed.ok());
  if (!parsed.ok())
    return;
  const auto result = emitModule(*parsed.module);
  CHECK(!result.ok());
  CHECK(mtir::test::mentions(result.diagnostics, "takes 1 argument"));
}

MTIR_TEST("llvm", "globals are emitted with their initialiser") {
  const std::string ir = llOf("global @counter : i32 = 3\n"
                              "global @rate : f64 = 2.5\n"
                              "global @blank : i32\n"
                              "global @table : i32[8]\n"
                              "\nfunc @f() -> void {\nentry:\n  ret void\n}\n");
  CHECK(contains(ir, "@counter = global i32 3"));
  CHECK(contains(ir, "@rate = global double 2.5"));
  CHECK(contains(ir, "@blank = global i32 zeroinitializer"));
  CHECK(contains(ir, "@table = global [8 x i32] zeroinitializer"));
}

MTIR_TEST("llvm", "runtime declarations appear only when used") {
  const std::string plain = llOf("func @f() -> void {\nentry:\n  ret void\n}\n");
  CHECK(!contains(plain, "@printf"));
  CHECK(!contains(plain, "llvm.trap"));
}

// ==========================================================================
// docs/divergence.md, realised
// ==========================================================================
MTIR_TEST("llvm.divergence", "rows 1 and 2: the division guard") {
  const std::string ir = wrap("  %t = sdiv i32 %x, %y");
  CHECK(contains(ir, "icmp eq i32 %y, 0"));
  CHECK(contains(ir, "icmp eq i32 %x, -2147483648"));
  CHECK(contains(ir, "icmp eq i32 %y, -1"));
  CHECK(contains(ir, "call void @llvm.trap()"));
  CHECK(contains(ir, "unreachable"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "unsigned division guards only the zero divisor") {
  const std::string ir = wrap("  %t = udiv i32 %x, %y");
  CHECK(contains(ir, "icmp eq i32 %y, 0"));
  CHECK(!contains(ir, "-2147483648"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "a provably non-zero divisor needs no guard") {
  const std::string ir = wrap("  %t = sdiv i32 %x, 4");
  CHECK(!contains(ir, "llvm.trap"));
  CHECK(contains(ir, "%t = sdiv i32 %x, 4"));
}

MTIR_TEST("llvm.divergence", "a constant -1 divisor keeps the overflow guard") {
  CHECK(contains(wrap("  %t = sdiv i32 %x, -1"), "icmp eq i32 %x, -2147483648"));
}

MTIR_TEST("llvm.divergence", "row 3: a constant shift count is masked at compile time") {
  CHECK(contains(wrap("  %t = shl i32 %x, 33"), "%t = shl i32 %x, 1"));
  CHECK(contains(wrap("  %t = shl i32 %x, 32"), "%t = shl i32 %x, 0"));
  CHECK(contains(wrap("  %t = shl i32 %x, 31"), "%t = shl i32 %x, 31"));
}

MTIR_TEST("llvm.divergence", "row 3: a dynamic shift count is masked with an and") {
  const std::string ir = wrap("  %t = shl i32 %x, %y");
  CHECK(contains(ir, "and i32 %y, 31"));
  CHECK(contains(ir, "%t = shl i32 %x, %g."));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "row 3: i64 uses a mask of 63") {
  const std::string ir = llOf("func @f(i64 %x, i64 %y) -> void {\nentry:\n"
                              "  %t = shl i64 %x, %y\n  ret void\n}\n");
  CHECK(contains(ir, "and i64 %y, 63"));
}

MTIR_TEST("llvm.divergence", "row 4: no nsw or nuw flags are emitted") {
  // CIR defines signed overflow as wraparound, so the flags that would make
  // it poison must not appear anywhere.
  const std::string ir = wrap("  %t = add i32 %x, %y\n"
                              "  %u = mul i32 %x, %y\n"
                              "  %v = sub i32 %x, %y");
  CHECK(!contains(ir, "nsw"));
  CHECK(!contains(ir, "nuw"));
}

MTIR_TEST("llvm.divergence", "row 4: an out-of-range literal is wrapped, not emitted raw") {
  // i32 2147483648 is not a valid LLVM i32 literal.
  const std::string ir = llOf("func @f() -> i32 {\nentry:\n"
                              "  %t = add i32 2147483648, 0\n  ret i32 %t\n}\n");
  CHECK(contains(ir, "add i32 -2147483648, 0"));
  // The raw positive literal must not survive: "i32 2147483648" is out of
  // range for i32 and llvm-as rejects it.
  CHECK(!contains(ir, "i32 2147483648"));
}

MTIR_TEST("llvm.divergence", "row 6: fptosi checks NaN and range") {
  const std::string ir = llOf("func @f(f64 %x) -> void {\nentry:\n"
                              "  %t = fptosi i32 %x\n  ret void\n}\n");
  CHECK(contains(ir, "fcmp uno double %x, %x"));
  CHECK(contains(ir, "fcmp ole double %x, -2147483649.0"));
  CHECK(contains(ir, "fcmp oge double %x, 2147483648.0"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "row 6: the i64 lower bound is not over-strict") {
  // -(2^63) - 1 is not representable as a double -- it rounds back to -(2^63).
  // Comparing `ole -(2^63)` would trap on x = -2^63, which converts fine, so
  // the i64 guard must compare strictly below instead.
  const std::string ir = llOf("func @f(f64 %x) -> void {\nentry:\n"
                              "  %t = fptosi i64 %x\n  ret void\n}\n");
  // printDouble chooses the shortest round-tripping spelling, so the expected
  // text is derived rather than hard-coded.
  const std::string minI64 = printDouble(-9223372036854775808.0);
  const std::string maxI64 = printDouble(9223372036854775808.0);
  CHECK(contains(ir, "fcmp olt double %x, " + minI64));
  CHECK(!contains(ir, "fcmp ole double %x, " + minI64));
  CHECK(contains(ir, "fcmp oge double %x, " + maxI64));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "row 6: the i32 lower bound stays inclusive") {
  // -(2^31) - 1 == -2147483649 *is* an exact double, so the i32 guard keeps
  // the tighter `ole` form.
  const std::string ir = llOf("func @f(f64 %x) -> void {\nentry:\n"
                              "  %t = fptosi i32 %x\n  ret void\n}\n");
  CHECK(contains(ir, "fcmp ole double %x, " + printDouble(-2147483649.0)));
}

MTIR_TEST("llvm.divergence", "row 7: a gep off a sized alloca is bounds-checked") {
  const std::string ir = llOf("func @f(i32 %i) -> void {\nentry:\n"
                              "  %p = alloca i32, 8\n"
                              "  %q = gep i32 %p, %i\n  ret void\n}\n");
  CHECK(contains(ir, "icmp uge i32 %i, 8"));
  CHECK(contains(ir, "call void @llvm.trap()"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm.divergence", "row 7: a constant index in range needs no check") {
  const std::string ir = llOf("func @f() -> void {\nentry:\n"
                              "  %p = alloca i32, 8\n"
                              "  %q = gep i32 %p, 3\n  ret void\n}\n");
  CHECK(!contains(ir, "llvm.trap"));
}

MTIR_TEST("llvm.divergence", "row 7: a constant index out of range is still checked") {
  const std::string ir = llOf("func @f() -> void {\nentry:\n"
                              "  %p = alloca i32, 8\n"
                              "  %q = gep i32 %p, 9\n  ret void\n}\n");
  CHECK(contains(ir, "icmp uge i32 9, 8"));
}

MTIR_TEST("llvm", "a CIR trap does not terminate the LLVM block") {
  // 'trap' is an intrinsic, not a CIR terminator, so the block's own
  // terminator follows it.  Emitting 'unreachable' here would make the LLVM
  // block malformed.
  const std::string ir = llOf("func @f() -> void {\nentry:\n  trap\n  ret void\n}\n");
  CHECK(contains(ir, "  call void @llvm.trap()\n  ret void"));
  CHECK_VALID_IR(ir);
}

// ==========================================================================
// The structural validator itself
// ==========================================================================
// ==========================================================================
// Malformed CIR must be refused, not emitted
// ==========================================================================
MTIR_TEST("llvm", "a conversion with the wrong result type is refused") {
  // Regression: `fptosi f64` drove the fptosi guard with a width of zero, so
  // `1 << (bits - 1)` shifted by 0xFFFFFFFF -- undefined behaviour -- and the
  // emitter produced `fptosi double %x to double`, which llvm-as rejects.
  struct Case {
    const char *cir;
    const char *fragment;
  };
  const Case cases[] = {
      {"  %t = fptosi f64 %d", "fptosi must produce an integer"},
      {"  %t = sext f64 %d", "sext must produce an integer"},
      {"  %t = trunc f64 %d", "trunc must produce an integer"},
      {"  %t = sitofp i32 %x", "sitofp must produce f64"},
      {"  %t = fptosi i32 %x", "fptosi takes an f64 operand"},
  };
  for (const Case &c : cases) {
    ParseResult parsed = parseCir("func @f(i32 %x, f64 %d) -> void {\nentry:\n" +
                                  std::string(c.cir) + "\n  ret void\n}\n");
    CHECK(parsed.ok());
    if (!parsed.ok())
      continue;
    const auto result = emitModule(*parsed.module);
    CHECK(!result.ok());
    CHECK(mtir::test::mentions(result.diagnostics, c.fragment));
    if (!mtir::test::mentions(result.diagnostics, c.fragment))
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string("for ") + c.cir + " expected '" +
                                    c.fragment + "', got:\n" +
                                    mtir::support::format(result.diagnostics));
  }
}

MTIR_TEST("llvm", "a shift or integer division on a non-integer type is refused") {
  const char *bodies[] = {
      "  %t = shl f64 %d, %d",
      "  %t = ashr f64 %d, %d",
      "  %t = sdiv f64 %d, %d",
      "  %t = srem f64 %d, %d",
  };
  for (const char *body : bodies) {
    ParseResult parsed = parseCir("func @f(f64 %d) -> void {\nentry:\n" +
                                  std::string(body) + "\n  ret void\n}\n");
    CHECK(parsed.ok());
    if (!parsed.ok())
      continue;
    const auto result = emitModule(*parsed.module);
    CHECK(!result.ok());
    CHECK(mtir::test::mentions(result.diagnostics, "requires an integer type"));
  }
}

MTIR_TEST("llvm", "fdiv on f64 is still accepted") {
  // The refusal above must not catch legitimate floating-point division.
  const std::string ir = llOf("func @f(f64 %d) -> void {\nentry:\n"
                              "  %t = fdiv f64 %d, %d\n  ret void\n}\n");
  CHECK(contains(ir, "%t = fdiv double %d, %d"));
  CHECK_VALID_IR(ir);
}

MTIR_TEST("llvm", "the verifier rejects the same malformed conversions") {
  // The back end's refusal is a second line of defence; the verifier is the
  // first, and both must agree that this IR is bad.
  ParseResult parsed = parseCir("func @f(f64 %d) -> void {\nentry:\n"
                                "  %t = fptosi f64 %d\n  ret void\n}\n");
  CHECK(parsed.ok());
  if (parsed.ok())
    CHECK(mtir::test::hasCode(verify(*parsed.module), "CIR06"));
}

MTIR_TEST("llvm", "the structural validator is not vacuous") {
  // Every CHECK_VALID_IR above is only worth something if the checker really
  // rejects broken IR.  These are the five things it must catch.
  struct Case {
    const char *ir;
    const char *problem;
  };
  const Case cases[] = {
      {"define i32 @f() {\nentry:\n  %t = add i32 1, 2\n}\n", "unterminated block"},
      {"define i32 @f() {\nentry:\n  ret i32 0\n  ret i32 1\n}\n",
       "instruction after a terminator"},
      {"define i32 @f() {\nentry:\n  br label %nowhere\n}\n",
       "branch to a label that does not exist"},
      {"define i32 @f() {\nentry:\n  ret i32 %ghost\n}\n",
       "register used before it is defined"},
      {"define i32 @f() {\nentry:\n  ret i32 0\nentry:\n  ret i32 1\n}\n",
       "duplicate label"},
  };
  for (const Case &c : cases) {
    const std::vector<std::string> problems = structuralProblems(c.ir);
    bool found = false;
    for (const std::string &p : problems)
      if (p.find(c.problem) != std::string::npos)
        found = true;
    CHECK(found);
    if (!found)
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string("expected to catch: ") + c.problem);
  }
  // ...and it must accept IR that is actually fine.
  CHECK(structuralProblems(emitOf(demoAbsModule())).empty());
}
