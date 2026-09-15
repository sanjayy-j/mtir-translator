// Lexer.h -- MiniLang source text -> tokens.
//
// Module M1a.  A hand-written scanner: no generator and no regex engine
// driving the token loop.  Longest match is obtained by ordering the operator
// table by descending length, so "<<" wins over "<" and "->" over "-";
// keywords are recognised by looking a scanned identifier up in a table,
// which keeps the identifier rule a single loop.
//
// Errors are accumulated rather than thrown, so one run reports several
// lexical problems.  On an error the scanner skips the offending character
// and carries on, which is what lets the parser still see the rest of the
// file.
#ifndef MTIR_FRONTEND_LEXER_H
#define MTIR_FRONTEND_LEXER_H

#include <string>
#include <string_view>
#include <vector>

#include "mtir/ast/Token.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::frontend {

struct LexResult {
  std::vector<ast::Token> tokens;   ///< always ends with an Eof token
  support::Diagnostics diagnostics;

  bool ok() const { return !support::hasErrors(diagnostics); }
};

LexResult tokenize(std::string_view source, std::string file = "<input>");

} // namespace mtir::frontend

#endif // MTIR_FRONTEND_LEXER_H
