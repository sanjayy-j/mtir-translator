// Parser.h -- tokens -> AST.
//
// Module M1b.  Recursive descent over the grammar of docs/minilang-spec.md
// section 2.  The statement grammar is LL(1) after left-factoring, so each
// statement rule dispatches on one lookahead token.  Expressions use
// precedence climbing rather than one rule per level: the grammar stays flat
// and adding a level is a table edit rather than a new function.
//
// Every branch body is a braced block, so there is no dangling-else ambiguity
// and `else if` chains need no special rule.
#ifndef MTIR_FRONTEND_PARSER_H
#define MTIR_FRONTEND_PARSER_H

#include <optional>
#include <string>
#include <string_view>

#include "mtir/ast/AST.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::frontend {

struct ParseResult {
  std::optional<ast::Program> program;
  support::Diagnostics diagnostics;

  bool ok() const { return program.has_value(); }
};

/// Lex and parse in one step.  Lexical diagnostics are merged in, so a caller
/// gets one list covering the whole front end.
ParseResult parse(std::string_view source, std::string file = "<input>");

} // namespace mtir::frontend

#endif // MTIR_FRONTEND_PARSER_H
