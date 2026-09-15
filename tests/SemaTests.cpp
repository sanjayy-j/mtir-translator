// Tests for semantic analysis.
//
// The Python prototype never implemented this layer -- src/sema/typecheck.py
// raises NotImplementedError -- so there is no reference behaviour to migrate.
// These tests are written against docs/minilang-spec.md section 7 directly,
// which lists the twelve diagnostics M2 must detect.

#include "mtir_test.h"

#include <cstddef>
#include <string>

#include "mtir/frontend/Parser.h"
#include "mtir/sema/Analyse.h"

#include "TestSupport.h"

using namespace mtir::ast;
using mtir::frontend::parse;

namespace {

struct Analysed {
  mtir::frontend::ParseResult parsed;
  mtir::sema::AnalysisResult analysis;
  bool parsedOk = false;
};

Analysed analyseSource(const std::string &src) {
  Analysed out;
  out.parsed = parse(src, "<test>");
  out.parsedOk = out.parsed.ok();
  if (out.parsedOk)
    out.analysis = mtir::sema::analyse(*out.parsed.program, "<test>");
  return out;
}

/// True when analysis reports the given diagnostic code.
bool reports(const std::string &src, const char *code, const char *file, int line) {
  Analysed a = analyseSource(src);
  if (!a.parsedOk) {
    mtir::test::reportFailure(file, line, "did not even parse:\n" + src);
    return false;
  }
  if (!mtir::test::hasCode(a.analysis.diagnostics, code)) {
    mtir::test::reportFailure(file, line,
                              std::string("expected ") + code + " for:\n" + src +
                                  "got:\n" +
                                  mtir::support::format(a.analysis.diagnostics));
    return false;
  }
  return true;
}

#define CHECK_REPORTS(src, code) reports((src), (code), __FILE__, __LINE__)

bool accepts(const std::string &src, const char *file, int line) {
  Analysed a = analyseSource(src);
  if (!a.parsedOk) {
    mtir::test::reportFailure(file, line, "did not parse:\n" + src);
    return false;
  }
  if (!a.analysis.ok()) {
    mtir::test::reportFailure(file, line,
                              "should have been accepted:\n" + src + "got:\n" +
                                  mtir::support::format(a.analysis.diagnostics));
    return false;
  }
  return true;
}

#define CHECK_ACCEPTS(src) accepts((src), __FILE__, __LINE__)

} // namespace

// ==========================================================================
// The twelve diagnostics of minilang-spec.md section 7
// ==========================================================================
MTIR_TEST("sema", "E001 undeclared identifier") {
  CHECK_REPORTS("fn f() -> int { return nope; }", "E001");
  CHECK_REPORTS("fn f() { nope = 1; }", "E001");
}

MTIR_TEST("sema", "E002 duplicate declaration in the same scope") {
  CHECK_REPORTS("fn f() { let x: int = 1; let x: int = 2; }", "E002");
  CHECK_REPORTS("fn f(a: int, a: int) { }", "E002");
  CHECK_REPORTS("fn f() { }\nfn f() { }", "E002");
}

MTIR_TEST("sema", "shadowing an outer scope is allowed") {
  // Section 5: shadowing an outer name is permitted; re-declaring in the same
  // scope is not.
  CHECK_ACCEPTS("fn f() { let x: int = 1; { let x: int = 2; } }");
}

MTIR_TEST("sema", "E003 type mismatch in a binary operation") {
  CHECK_REPORTS("fn f(a: bool, b: float) { print_float(a + b); }", "E003");
  CHECK_REPORTS("fn f(a: float) -> float { return a % a; }", "E003");
  CHECK_REPORTS("fn f(a: float, b: float) -> float { return a & b; }", "E003");
  CHECK_REPORTS("fn f(n: int) { if (n) { } }", "E003");
  CHECK_REPORTS("fn f(a: float, b: int) -> int { return a << b; }", "E003");
  CHECK_REPORTS("fn f(a: int, b: int) -> bool { return a && b; }", "E003");
}

MTIR_TEST("sema", "E004 type mismatch in an assignment") {
  // Unrelated types: bool and float widen into each other in neither
  // direction, so this is a plain mismatch rather than a narrowing.
  CHECK_REPORTS("fn f() { let b: bool = true; let x: float = 1.0; x = b; }", "E004");
  CHECK_REPORTS("fn f() { let b: bool = 1.5; }", "E004");
}

MTIR_TEST("sema", "E005 narrowing without an explicit cast") {
  // A narrowing gets its own code wherever it appears, because that is more
  // informative than the generic mismatch of whatever context it was in.
  // MiniLang has no cast syntax, so a narrowing can only ever be an error.
  CHECK_REPORTS("fn f(x: float) { print_int(x); }", "E005");
  CHECK_REPORTS("fn f() { let x: int = 2.5; }", "E005");
  CHECK_REPORTS("fn f(n: long) { let x: int = n; }", "E005");
  CHECK_REPORTS("fn f() { let x: int = 1; let y: float = 2.0; x = y; }", "E005");
}

MTIR_TEST("sema", "E006 call to an undeclared function") {
  CHECK_REPORTS("fn f() -> int { return nosuch(); }", "E006");
}

MTIR_TEST("sema", "E007 wrong number of arguments") {
  CHECK_REPORTS("fn g(a: int) -> int { return a; }\nfn f() -> int { return g(1, 2); }",
                "E007");
  CHECK_REPORTS("fn g(a: int) -> int { return a; }\nfn f() -> int { return g(); }",
                "E007");
}

MTIR_TEST("sema", "E008 argument type mismatch") {
  // float and bool widen into each other in neither direction, so this is a
  // plain argument mismatch rather than a narrowing.
  CHECK_REPORTS("fn g(a: bool) { }\nfn f() { g(1.5); }", "E008");
}

MTIR_TEST("sema", "E009 missing return on some path") {
  CHECK_REPORTS("fn f() -> int { }", "E009");
  CHECK_REPORTS("fn f(c: bool) -> int { if (c) { return 1; } }", "E009");
  CHECK_REPORTS("fn f() -> int { return; }", "E009");
}

MTIR_TEST("sema", "an if/else where both arms return covers every path") {
  CHECK_ACCEPTS("fn f(c: bool) -> int { if (c) { return 1; } else { return 2; } }");
}

MTIR_TEST("sema", "a trailing return after an if is enough") {
  CHECK_ACCEPTS("fn f(c: bool) -> int { if (c) { return 1; } return 0; }");
}

MTIR_TEST("sema", "E010 return with a value in a void function") {
  CHECK_REPORTS("fn f() { return 1; }", "E010");
}

MTIR_TEST("sema", "E011 break or continue outside a loop") {
  CHECK_REPORTS("fn f() { break; }", "E011");
  CHECK_REPORTS("fn f() { continue; }", "E011");
  CHECK_REPORTS("fn f(c: bool) { if (c) { break; } }", "E011");
}

MTIR_TEST("sema", "break and continue are fine inside a loop") {
  CHECK_ACCEPTS("fn f(c: bool) { while (c) { break; } }");
  CHECK_ACCEPTS("fn f(c: bool) { while (c) { continue; } }");
  CHECK_ACCEPTS("fn f() { for (let i: int = 0; i < 3; i = i + 1) { break; } }");
}

MTIR_TEST("sema", "E012 indexing a non-array or with a non-integer") {
  CHECK_REPORTS("fn f(a: int) -> int { return a[0]; }", "E012");
  CHECK_REPORTS("fn f(i: float) -> int { let xs: int[4]; return xs[i]; }", "E012");
}

// ==========================================================================
// Conversion insertion (minilang-spec.md section 4)
// ==========================================================================
MTIR_TEST("sema", "int to long is inserted automatically") {
  CHECK_ACCEPTS("fn g(x: long) { }\nfn f(n: int) { g(n); }");
}

MTIR_TEST("sema", "int to float is inserted automatically") {
  CHECK_ACCEPTS("fn f(n: int) { print_float(n); }");
}

MTIR_TEST("sema", "a Conv node actually appears in the tree") {
  Analysed a = analyseSource("fn f(n: int) { print_float(n); }");
  CHECK(a.parsedOk && a.analysis.ok());
  if (!a.parsedOk || !a.analysis.ok())
    return;

  const FnDecl *fn = nullptr;
  for (const DeclPtr &d : a.parsed.program->decls())
    if (const auto *candidate = dynCast<FnDecl>(d.get()))
      fn = candidate;
  CHECK(fn != nullptr);
  if (fn == nullptr)
    return;

  const auto *stmt = dynCast<ExprStmt>(fn->body().statements()[0].get());
  CHECK(stmt != nullptr);
  if (stmt == nullptr)
    return;
  const auto *call = dynCast<Call>(&stmt->expr());
  CHECK(call != nullptr);
  if (call == nullptr)
    return;
  // The argument was an int; the checker wrapped it.
  CHECK_EQ(call->args()[0]->kind(), NodeKind::Conv);
  CHECK_EQ(a.analysis.types->typeOf(*call->args()[0]), mtir::cir::Ty::F64);
}

MTIR_TEST("sema", "no Conv is inserted when the types already match") {
  Analysed a = analyseSource("fn f(n: int) { print_int(n); }");
  CHECK(a.parsedOk && a.analysis.ok());
  if (!a.parsedOk || !a.analysis.ok())
    return;
  const FnDecl *fn = nullptr;
  for (const DeclPtr &d : a.parsed.program->decls())
    if (const auto *candidate = dynCast<FnDecl>(d.get()))
      fn = candidate;
  const auto *call =
      dynCast<Call>(&dynCast<ExprStmt>(fn->body().statements()[0].get())->expr());
  CHECK_EQ(call->args()[0]->kind(), NodeKind::VarRef);
}

// ==========================================================================
// isAssigned, which the CIR builder relies on
// ==========================================================================
MTIR_TEST("sema", "an unassigned parameter is reported as unassigned") {
  Analysed a = analyseSource("fn abs(x: int) -> int { if (x < 0) { return -x; } return x; }");
  CHECK(a.parsedOk && a.analysis.ok());
  if (!a.parsedOk || !a.analysis.ok())
    return;
  const FnDecl *fn = nullptr;
  for (const DeclPtr &d : a.parsed.program->decls())
    if (const auto *candidate = dynCast<FnDecl>(d.get()))
      fn = candidate;
  CHECK(!a.analysis.types->isAssigned(fn->params()[0]));
}

MTIR_TEST("sema", "an assigned parameter is reported as assigned") {
  Analysed a = analyseSource("fn f(x: int) -> int { x = x + 1; return x; }");
  CHECK(a.parsedOk && a.analysis.ok());
  if (!a.parsedOk || !a.analysis.ok())
    return;
  const FnDecl *fn = nullptr;
  for (const DeclPtr &d : a.parsed.program->decls())
    if (const auto *candidate = dynCast<FnDecl>(d.get()))
      fn = candidate;
  CHECK(a.analysis.types->isAssigned(fn->params()[0]));
}

MTIR_TEST("sema", "assignment inside nested control flow still counts") {
  Analysed a = analyseSource(
      "fn f(x: int) -> int { while (x < 3) { if (x > 1) { x = x + 1; } } return x; }");
  CHECK(a.parsedOk && a.analysis.ok());
  if (!a.parsedOk || !a.analysis.ok())
    return;
  const FnDecl *fn = nullptr;
  for (const DeclPtr &d : a.parsed.program->decls())
    if (const auto *candidate = dynCast<FnDecl>(d.get()))
      fn = candidate;
  CHECK(a.analysis.types->isAssigned(fn->params()[0]));
}

// ==========================================================================
// The corpus
// ==========================================================================
MTIR_TEST("sema", "every valid corpus program type-checks") {
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
    const std::string src = mtir::test::readFile(mtir::test::example(path));
    CHECK(!src.empty());
    Analysed a = analyseSource(src);
    CHECK(a.parsedOk);
    CHECK(a.analysis.ok());
    if (a.parsedOk && !a.analysis.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" +
                                    mtir::support::format(a.analysis.diagnostics));
  }
}

MTIR_TEST("sema", "arrays.mini is rejected for its array initialiser") {
  // docs/decisions/0001-array-initialisers.md: `let xs: int[8] = 0;` is
  // syntactically legal but has no defined semantics, so the checker refuses
  // it rather than inventing one.  This test pins the *current* decision; if
  // the team chooses option B it should be updated deliberately.
  const std::string src =
      mtir::test::readFile(mtir::test::example("tests/corpus/valid/arrays.mini"));
  CHECK(!src.empty());
  Analysed a = analyseSource(src);
  CHECK(a.parsedOk);
  CHECK(!a.analysis.ok());
  CHECK(mtir::test::hasCode(a.analysis.diagnostics, "E004"));
  CHECK(mtir::test::mentions(a.analysis.diagnostics, "array declaration cannot take"));
}
