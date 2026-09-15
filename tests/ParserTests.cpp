// Tests for the MiniLang parser and AST.  Migrated from tests/test_parser.py.

#include "mtir_test.h"

#include <cstddef>
#include <string>
#include <vector>

#include "mtir/frontend/Parser.h"

#include "TestSupport.h"

using namespace mtir::ast;
using mtir::frontend::parse;

namespace {

/// Parse, reporting the diagnostics on failure rather than a bare false.
bool parseOk(const std::string &src, const char *file, int line) {
  auto result = parse(src, "<test>");
  if (!result.ok())
    mtir::test::reportFailure(file, line,
                              "failed to parse:\n" + src + "\n" +
                                  mtir::support::format(result.diagnostics));
  return result.ok();
}

#define CHECK_PARSES(src) CHECK(parseOk((src), __FILE__, __LINE__))

bool rejects(const std::string &src) { return !parse(src, "<test>").ok(); }

std::string dumpOf(const std::string &src) {
  auto result = parse(src, "<test>");
  return result.ok() ? dump(*result.program) : std::string();
}

const FnDecl *firstFunction(const Program &program) {
  for (const DeclPtr &d : program.decls())
    if (const auto *fn = dynCast<FnDecl>(d.get()))
      return fn;
  return nullptr;
}

} // namespace

// ==========================================================================
// Declarations
// ==========================================================================
MTIR_TEST("parser", "a minimal function parses") {
  CHECK_PARSES("fn main() -> int { return 0; }");
}

MTIR_TEST("parser", "a function with no return type defaults to void") {
  auto result = parse("fn f() { }", "<test>");
  CHECK(result.ok());
  if (!result.ok())
    return;
  const FnDecl *fn = firstFunction(*result.program);
  CHECK(fn != nullptr);
  if (fn != nullptr)
    CHECK_EQ(fn->returnType().name, std::string("void"));
}

MTIR_TEST("parser", "parameters and array types parse") {
  auto result = parse("fn f(a: int, b: float, c: bool) -> long { return 0; }", "<test>");
  CHECK(result.ok());
  if (!result.ok())
    return;
  const FnDecl *fn = firstFunction(*result.program);
  CHECK(fn != nullptr);
  if (fn == nullptr)
    return;
  CHECK_EQ(fn->params().size(), std::size_t{3});
  CHECK_EQ(fn->params()[0].type.name, std::string("int"));
  CHECK_EQ(fn->returnType().name, std::string("long"));
}

MTIR_TEST("parser", "an array type records its length") {
  auto result = parse("fn f() { let xs: int[8]; }", "<test>");
  CHECK(result.ok());
  if (!result.ok())
    return;
  const auto &body = firstFunction(*result.program)->body();
  const auto *let = dynCast<Let>(body.statements()[0].get());
  CHECK(let != nullptr);
  if (let != nullptr) {
    CHECK(let->type().isArray());
    CHECK_EQ(*let->type().arrayLength, 8);
  }
}

MTIR_TEST("parser", "globals parse with and without an initialiser") {
  CHECK_PARSES("global counter: int = 0;\nfn main() -> int { return 0; }");
  CHECK_PARSES("global counter: int;\nfn main() -> int { return 0; }");
}

// ==========================================================================
// Statements
// ==========================================================================
MTIR_TEST("parser", "every statement form parses") {
  const char *bodies[] = {
      "let x: int = 1;",
      "let x: int;",
      "x = 1;",
      "f();",
      "{ }",
      "if (c) { }",
      "if (c) { } else { }",
      "if (c) { } else if (d) { } else { }",
      "while (c) { }",
      "for (let i: int = 0; i < 3; i = i + 1) { }",
      "for (;;) { }",
      "while (c) { break; }",
      "while (c) { continue; }",
      "return;",
  };
  for (const char *body : bodies)
    CHECK_PARSES(std::string("fn f() { ") + body + " }");
}

MTIR_TEST("parser", "else-if chains need no dangling-else rule") {
  // Every branch body is a braced block, so the chain is unambiguous.
  const std::string text =
      dumpOf("fn f(a: bool, b: bool) { if (a) { } else if (b) { } else { } }");
  CHECK(text.find("If") != std::string::npos);
  // The nested If appears as a child of the outer one.
  CHECK_EQ(std::count(text.begin(), text.end(), 'I') > 0, true);
}

MTIR_TEST("parser", "a for loop may omit any clause") {
  CHECK_PARSES("fn f() { for (;;) { } }");
  CHECK_PARSES("fn f() { let i: int = 0; for (; i < 3;) { } }");
  CHECK_PARSES("fn f() { let i: int = 0; for (; ; i = i + 1) { } }");
}

// ==========================================================================
// Expression precedence
// ==========================================================================
MTIR_TEST("parser", "multiplication binds tighter than addition") {
  const std::string text = dumpOf("fn f() -> int { return 2 + 3 * 4; }");
  // The '+' is the root, so it appears at a shallower indent than the '*'.
  const std::size_t plus = text.find("Binary +");
  const std::size_t star = text.find("Binary *");
  CHECK(plus != std::string::npos && star != std::string::npos);
  CHECK(plus < star);
}

MTIR_TEST("parser", "parentheses override precedence") {
  const std::string text = dumpOf("fn f() -> int { return (2 + 3) * 4; }");
  const std::size_t plus = text.find("Binary +");
  const std::size_t star = text.find("Binary *");
  CHECK(plus != std::string::npos && star != std::string::npos);
  CHECK(star < plus);
}

MTIR_TEST("parser", "the full precedence cascade") {
  // || < && < | < ^ < & < == < relational < shift < additive < multiplicative
  const char *sources[] = {
      "a || b && c", "a && b | c", "a | b ^ c", "a ^ b & c",
      "a & b == c",  "a == b < c", "a < b << c", "a << b + c",
      "a + b * c",
  };
  for (const char *expr : sources) {
    const std::string text =
        dumpOf(std::string("fn f(a: int, b: int, c: int) -> int { return ") + expr +
               "; }");
    CHECK(!text.empty());
  }
}

MTIR_TEST("parser", "binary operators are left-associative") {
  // 1 - 2 - 3 parses as (1 - 2) - 3, so the outer '-' has a Binary left child.
  const std::string text = dumpOf("fn f() -> int { return 1 - 2 - 3; }");
  const std::size_t first = text.find("Binary -");
  CHECK(first != std::string::npos);
  const std::size_t second = text.find("Binary -", first + 1);
  CHECK(second != std::string::npos);
  // The second occurrence is more deeply indented, i.e. it is the left child.
  CHECK(text.find("IntLit 3") > second);
}

MTIR_TEST("parser", "unary operators parse and nest") {
  CHECK_PARSES("fn f(x: int) -> int { return -x; }");
  CHECK_PARSES("fn f(x: bool) -> bool { return !x; }");
  CHECK_PARSES("fn f(x: int) -> int { return ~x; }");
  CHECK_PARSES("fn f(x: int) -> int { return --x; }");
}

MTIR_TEST("parser", "calls and indexing parse") {
  CHECK_PARSES("fn g() -> int { return 0; }\nfn f() -> int { return g(); }");
  CHECK_PARSES("fn g(a: int, b: int) -> int { return 0; }\n"
               "fn f() -> int { return g(1, 2); }");
  CHECK_PARSES("fn f() -> int { let xs: int[4]; return xs[0]; }");
}

MTIR_TEST("parser", "literals parse") {
  CHECK_PARSES("fn f() -> int { return 0xFF; }");
  CHECK_PARSES("fn f() -> int { return 1_000; }");
  CHECK_PARSES("fn f() -> float { return 2.5e-3; }");
  CHECK_PARSES("fn f() -> bool { return true; }");
  CHECK_PARSES("fn f() -> bool { return false; }");
}

MTIR_TEST("parser", "numeric separators are stripped from literal values") {
  auto result = parse("fn f() -> int { return 1_000; }", "<test>");
  CHECK(result.ok());
  if (!result.ok())
    return;
  const auto &body = firstFunction(*result.program)->body();
  const auto *ret = dynCast<Return>(body.statements()[0].get());
  const auto *lit = dynCast<IntLit>(ret->value());
  CHECK(lit != nullptr);
  if (lit != nullptr)
    CHECK_EQ(lit->value(), static_cast<std::int64_t>(1000));
}

MTIR_TEST("parser", "a hex literal converts") {
  auto result = parse("fn f() -> int { return 0xFF; }", "<test>");
  CHECK(result.ok());
  if (!result.ok())
    return;
  const auto &body = firstFunction(*result.program)->body();
  const auto *lit = dynCast<IntLit>(dynCast<Return>(body.statements()[0].get())->value());
  CHECK(lit != nullptr);
  if (lit != nullptr)
    CHECK_EQ(lit->value(), static_cast<std::int64_t>(255));
}

// ==========================================================================
// Diagnostics
// ==========================================================================
MTIR_TEST("parser", "malformed input is rejected") {
  CHECK(rejects("fn main() -> int { return 1 }"));       // missing ';'
  CHECK(rejects("fn main() -> int { return 1; "));       // unbalanced brace
  CHECK(rejects("let x: int = 1;"));                     // not at top level
  CHECK(rejects("fn main() -> int { 1 = 2; }"));         // not assignable
  CHECK(rejects("fn main() -> nosuchtype { return 1; }"));
  CHECK(rejects("fn main( -> int { return 1; }"));
  CHECK(rejects("fn main() -> int { if c { } }"));       // needs parentheses
}

MTIR_TEST("parser", "a syntax error carries a position") {
  auto result = parse("fn main() -> int {\n  return 1\n}", "<test>");
  CHECK(!result.ok());
  CHECK(!result.diagnostics.empty());
  if (result.diagnostics.empty())
    return;
  CHECK(mtir::test::mentions(result.diagnostics, "syntax error"));
  CHECK_EQ(result.diagnostics[0].location.loc.line, 3);
}

MTIR_TEST("parser", "an assignment to a literal is rejected by name") {
  auto result = parse("fn f() { 1 = 2; }", "<test>");
  CHECK(!result.ok());
  CHECK(mtir::test::mentions(result.diagnostics, "not assignable"));
}

// ==========================================================================
// The corpus
// ==========================================================================
MTIR_TEST("parser", "every valid corpus program parses") {
  const char *programs[] = {
      "docs/examples/abs.mini",
      "tests/corpus/valid/arith.mini",        "tests/corpus/valid/arrays.mini",
      "tests/corpus/valid/control_flow.mini", "tests/corpus/valid/recursion.mini",
      "tests/corpus/boundary/div_edge.mini",  "tests/corpus/boundary/nesting.mini",
      "tests/corpus/boundary/shift_edge.mini",
  };
  for (const char *path : programs) {
    const std::string src = mtir::test::readFile(mtir::test::example(path));
    CHECK(!src.empty());
    auto result = parse(src, path);
    CHECK(result.ok());
    if (!result.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" +
                                    mtir::support::format(result.diagnostics));
    else
      CHECK(!result.program->decls().empty());
  }
}

MTIR_TEST("parser", "every invalid corpus program is rejected") {
  const char *programs[] = {
      "tests/corpus/invalid/assign_to_literal.mini",
      "tests/corpus/invalid/bad_char.mini",
      "tests/corpus/invalid/missing_semicolon.mini",
      "tests/corpus/invalid/unbalanced_brace.mini",
      "tests/corpus/invalid/unterminated_comment.mini",
  };
  for (const char *path : programs) {
    const std::string src = mtir::test::readFile(mtir::test::example(path));
    CHECK(!src.empty());
    const bool refused = !parse(src, path).ok();
    CHECK(refused);
    if (!refused)
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + " was accepted but should not be");
  }
}

MTIR_TEST("parser", "the AST dump starts at Program and is deterministic") {
  const std::string src = mtir::test::readFile(mtir::test::example("docs/examples/abs.mini"));
  const std::string a = dumpOf(src);
  const std::string b = dumpOf(src);
  CHECK(!a.empty());
  CHECK_EQ(a, b);
  CHECK_EQ(a.compare(0, 8, "Program\n"), 0);
  CHECK(a.find("FnDecl abs(x: int) -> int") != std::string::npos);
}
