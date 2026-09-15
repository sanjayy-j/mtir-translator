// Tests for the MiniLang lexer.  Migrated from tests/test_lexer.py.

#include "mtir_test.h"

#include <cstddef>
#include <string>
#include <vector>

#include "mtir/frontend/Lexer.h"

#include "TestSupport.h"

using namespace mtir::ast;
using mtir::frontend::tokenize;

namespace {

/// Token kinds for a source string, without the trailing Eof.
std::vector<TokKind> kindsOf(const std::string &src) {
  std::vector<TokKind> out;
  for (const Token &t : tokenize(src).tokens)
    if (t.kind != TokKind::Eof)
      out.push_back(t.kind);
  return out;
}

std::vector<std::string> textsOf(const std::string &src) {
  std::vector<std::string> out;
  for (const Token &t : tokenize(src).tokens)
    if (t.kind != TokKind::Eof)
      out.push_back(t.text);
  return out;
}

} // namespace

MTIR_TEST("lexer", "an empty source yields only EOF") {
  const auto result = tokenize("");
  CHECK_EQ(result.tokens.size(), std::size_t{1});
  CHECK(result.ok());
  if (!result.tokens.empty())
    CHECK_EQ(result.tokens[0].kind, TokKind::Eof);
}

MTIR_TEST("lexer", "keywords are distinguished from identifiers") {
  const auto kinds = kindsOf("fn let global if else while for break continue return "
                             "true false int long float bool void");
  const std::vector<TokKind> expected{
      TokKind::KwFn, TokKind::KwLet, TokKind::KwGlobal, TokKind::KwIf,
      TokKind::KwElse, TokKind::KwWhile, TokKind::KwFor, TokKind::KwBreak,
      TokKind::KwContinue, TokKind::KwReturn, TokKind::KwTrue, TokKind::KwFalse,
      TokKind::KwInt, TokKind::KwLong, TokKind::KwFloat, TokKind::KwBool,
      TokKind::KwVoid};
  CHECK_EQ(kinds.size(), expected.size());
  for (std::size_t i = 0; i < expected.size() && i < kinds.size(); ++i)
    CHECK_EQ(kinds[i], expected[i]);
}

MTIR_TEST("lexer", "a keyword prefix is still an identifier") {
  const auto kinds = kindsOf("iffy forx returned");
  CHECK_EQ(kinds.size(), std::size_t{3});
  for (TokKind k : kinds)
    CHECK_EQ(k, TokKind::Ident);
}

MTIR_TEST("lexer", "longest match wins") {
  // '<<' is one token, never two '<'.
  CHECK_EQ(kindsOf("<<").size(), std::size_t{1});
  CHECK_EQ(kindsOf("<<")[0], TokKind::Shl);
  CHECK_EQ(kindsOf("<=")[0], TokKind::Le);
  CHECK_EQ(kindsOf("->")[0], TokKind::Arrow);
  CHECK_EQ(kindsOf("==")[0], TokKind::Eq);
  CHECK_EQ(kindsOf("&&")[0], TokKind::AndAnd);
  // ...but a lone '<' is still LT.
  CHECK_EQ(kindsOf("< <").size(), std::size_t{2});
}

MTIR_TEST("lexer", "integer literals: decimal, hex and separators") {
  CHECK_EQ(kindsOf("42")[0], TokKind::IntLit);
  CHECK_EQ(kindsOf("0xFF")[0], TokKind::IntLit);
  CHECK_EQ(kindsOf("0XdeadBEEF")[0], TokKind::IntLit);
  CHECK_EQ(kindsOf("1_000_000")[0], TokKind::IntLit);
  const std::vector<std::string> texts = textsOf("1_000");
  CHECK_EQ(texts.size(), std::size_t{1});
  if (!texts.empty())
    CHECK_EQ(texts[0], std::string("1_000"));
}

MTIR_TEST("lexer", "float literals") {
  CHECK_EQ(kindsOf("1.5")[0], TokKind::FloatLit);
  CHECK_EQ(kindsOf("1e9")[0], TokKind::FloatLit);
  CHECK_EQ(kindsOf("2.5e-3")[0], TokKind::FloatLit);
  CHECK_EQ(kindsOf("1E+10")[0], TokKind::FloatLit);
}

MTIR_TEST("lexer", "a dot is part of a number only when a digit follows") {
  // docs/minilang-spec.md section 1.  "1." is an int followed by something
  // the lexer does not recognise as part of the number.
  const auto kinds = kindsOf("1.5");
  CHECK_EQ(kinds.size(), std::size_t{1});
  // '1e' with no digits is an int followed by an identifier, not a float.
  const auto split = kindsOf("1e");
  CHECK_EQ(split.size(), std::size_t{2});
  if (split.size() == 2) {
    CHECK_EQ(split[0], TokKind::IntLit);
    CHECK_EQ(split[1], TokKind::Ident);
  }
}

MTIR_TEST("lexer", "line comments run to end of line") {
  const auto kinds = kindsOf("let // this is ignored\nx");
  CHECK_EQ(kinds.size(), std::size_t{2});
  if (kinds.size() == 2) {
    CHECK_EQ(kinds[0], TokKind::KwLet);
    CHECK_EQ(kinds[1], TokKind::Ident);
  }
}

MTIR_TEST("lexer", "block comments are skipped and are not nested") {
  CHECK_EQ(kindsOf("a /* b */ c").size(), std::size_t{2});
  // Not nested: the first '*/' closes it, so the trailing '*/' is an error.
  const auto result = tokenize("a /* /* b */ c");
  CHECK(result.ok());
}

MTIR_TEST("lexer", "an unterminated block comment is reported") {
  const auto result = tokenize("a /* never closed");
  CHECK(!result.ok());
  CHECK(mtir::test::mentions(result.diagnostics, "unterminated block comment"));
}

MTIR_TEST("lexer", "a hex literal with no digits is reported") {
  const auto result = tokenize("0x");
  CHECK(!result.ok());
  CHECK(mtir::test::mentions(result.diagnostics, "no digits"));
}

MTIR_TEST("lexer", "an unexpected character is reported and scanning continues") {
  const auto result = tokenize("a $ b");
  CHECK(!result.ok());
  CHECK(mtir::test::mentions(result.diagnostics, "unexpected character"));
  // Both identifiers still come through, so one bad byte does not hide the
  // rest of the file.
  CHECK_EQ(kindsOf("a $ b").size(), std::size_t{2});
}

MTIR_TEST("lexer", "positions are 1-based and track newlines") {
  const auto tokens = tokenize("fn\n  abs").tokens;
  CHECK_EQ(tokens.size(), std::size_t{3});
  if (tokens.size() < 3)
    return;
  CHECK_EQ(tokens[0].loc.line, 1);
  CHECK_EQ(tokens[0].loc.col, 1);
  CHECK_EQ(tokens[1].loc.line, 2);
  CHECK_EQ(tokens[1].loc.col, 3);
}

MTIR_TEST("lexer", "the worked example lexes cleanly") {
  const std::string src = mtir::test::readFile(mtir::test::example("docs/examples/abs.mini"));
  CHECK(!src.empty());
  const auto result = tokenize(src, "abs.mini");
  CHECK(result.ok());
  if (!result.ok())
    mtir::test::reportFailure(__FILE__, __LINE__,
                              mtir::support::format(result.diagnostics));
  CHECK(result.tokens.size() > 20);
}

MTIR_TEST("lexer", "every valid corpus program lexes cleanly") {
  const char *programs[] = {
      "tests/corpus/valid/arith.mini",       "tests/corpus/valid/arrays.mini",
      "tests/corpus/valid/control_flow.mini", "tests/corpus/valid/recursion.mini",
      "tests/corpus/boundary/div_edge.mini", "tests/corpus/boundary/nesting.mini",
      "tests/corpus/boundary/shift_edge.mini",
  };
  for (const char *path : programs) {
    const std::string src = mtir::test::readFile(mtir::test::example(path));
    CHECK(!src.empty());
    const auto result = tokenize(src, path);
    CHECK(result.ok());
    if (!result.ok())
      mtir::test::reportFailure(__FILE__, __LINE__,
                                std::string(path) + ":\n" +
                                    mtir::support::format(result.diagnostics));
  }
}

MTIR_TEST("lexer", "token kind names match what --emit=tokens prints") {
  CHECK_EQ(std::string(tokKindName(TokKind::KwFn)), std::string("KW_FN"));
  CHECK_EQ(std::string(tokKindName(TokKind::Ident)), std::string("IDENT"));
  CHECK_EQ(std::string(tokKindName(TokKind::Shl)), std::string("SHL"));
  CHECK_EQ(std::string(tokKindName(TokKind::Eof)), std::string("EOF"));
}
