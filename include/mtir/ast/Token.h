// Token.h -- the MiniLang token model.
//
// Module M1a.  The token kinds and their spellings are docs/minilang-spec.md
// section 1.  Every token carries a source position so that every downstream
// diagnostic can point at real source coordinates.
#ifndef MTIR_AST_TOKEN_H
#define MTIR_AST_TOKEN_H

#include <cstdint>
#include <string>
#include <string_view>

#include "mtir/support/SourceLoc.h"

namespace mtir::ast {

enum class TokKind : std::uint8_t {
  // literals and names
  Ident, IntLit, FloatLit,
  // keywords
  KwFn, KwLet, KwGlobal, KwIf, KwElse, KwWhile, KwFor,
  KwBreak, KwContinue, KwReturn, KwTrue, KwFalse,
  KwInt, KwLong, KwFloat, KwBool, KwVoid,
  // operators and punctuation
  Plus, Minus, Star, Slash, Percent,
  Assign, Eq, Ne, Lt, Le, Gt, Ge,
  AndAnd, OrOr, Bang, Amp, Pipe, Caret, Tilde, Shl, Shr, Arrow,
  LParen, RParen, LBrace, RBrace, LBracket, RBracket,
  Comma, Semi, Colon,
  Eof
};

/// The screaming-snake name of a token kind, e.g. "KW_FN", "IDENT".
/// This is what `mtirc --emit=tokens` prints.
std::string_view tokKindName(TokKind kind);

/// True when the kind is one of the five type keywords.
bool isTypeKeyword(TokKind kind);

struct Token {
  TokKind kind = TokKind::Eof;
  std::string text;
  support::SourceLoc loc;
};

} // namespace mtir::ast

#endif // MTIR_AST_TOKEN_H
