#include "mtir/frontend/Lexer.h"

#include <cctype>
#include <cstddef>
#include <utility>

namespace mtir::ast {
namespace {

struct KindName {
  TokKind kind;
  std::string_view name;
};

// clang-format off
constexpr KindName kNames[] = {
  {TokKind::Ident, "IDENT"}, {TokKind::IntLit, "INT_LIT"}, {TokKind::FloatLit, "FLOAT_LIT"},
  {TokKind::KwFn, "KW_FN"}, {TokKind::KwLet, "KW_LET"}, {TokKind::KwGlobal, "KW_GLOBAL"},
  {TokKind::KwIf, "KW_IF"}, {TokKind::KwElse, "KW_ELSE"}, {TokKind::KwWhile, "KW_WHILE"},
  {TokKind::KwFor, "KW_FOR"}, {TokKind::KwBreak, "KW_BREAK"},
  {TokKind::KwContinue, "KW_CONTINUE"}, {TokKind::KwReturn, "KW_RETURN"},
  {TokKind::KwTrue, "KW_TRUE"}, {TokKind::KwFalse, "KW_FALSE"},
  {TokKind::KwInt, "KW_INT"}, {TokKind::KwLong, "KW_LONG"}, {TokKind::KwFloat, "KW_FLOAT"},
  {TokKind::KwBool, "KW_BOOL"}, {TokKind::KwVoid, "KW_VOID"},
  {TokKind::Plus, "PLUS"}, {TokKind::Minus, "MINUS"}, {TokKind::Star, "STAR"},
  {TokKind::Slash, "SLASH"}, {TokKind::Percent, "PERCENT"},
  {TokKind::Assign, "ASSIGN"}, {TokKind::Eq, "EQ"}, {TokKind::Ne, "NE"},
  {TokKind::Lt, "LT"}, {TokKind::Le, "LE"}, {TokKind::Gt, "GT"}, {TokKind::Ge, "GE"},
  {TokKind::AndAnd, "AND_AND"}, {TokKind::OrOr, "OR_OR"}, {TokKind::Bang, "BANG"},
  {TokKind::Amp, "AMP"}, {TokKind::Pipe, "PIPE"}, {TokKind::Caret, "CARET"},
  {TokKind::Tilde, "TILDE"}, {TokKind::Shl, "SHL"}, {TokKind::Shr, "SHR"},
  {TokKind::Arrow, "ARROW"},
  {TokKind::LParen, "LPAREN"}, {TokKind::RParen, "RPAREN"},
  {TokKind::LBrace, "LBRACE"}, {TokKind::RBrace, "RBRACE"},
  {TokKind::LBracket, "LBRACKET"}, {TokKind::RBracket, "RBRACKET"},
  {TokKind::Comma, "COMMA"}, {TokKind::Semi, "SEMI"}, {TokKind::Colon, "COLON"},
  {TokKind::Eof, "EOF"},
};
// clang-format on

} // namespace

std::string_view tokKindName(TokKind kind) {
  for (const KindName &entry : kNames)
    if (entry.kind == kind)
      return entry.name;
  return "<invalid>";
}

bool isTypeKeyword(TokKind kind) {
  return kind == TokKind::KwInt || kind == TokKind::KwLong ||
         kind == TokKind::KwFloat || kind == TokKind::KwBool ||
         kind == TokKind::KwVoid;
}

} // namespace mtir::ast

namespace mtir::frontend {
namespace {

using ast::TokKind;
using ast::Token;

struct Keyword {
  std::string_view text;
  TokKind kind;
};

constexpr Keyword kKeywords[] = {
    {"fn", TokKind::KwFn}, {"let", TokKind::KwLet}, {"global", TokKind::KwGlobal},
    {"if", TokKind::KwIf}, {"else", TokKind::KwElse}, {"while", TokKind::KwWhile},
    {"for", TokKind::KwFor}, {"break", TokKind::KwBreak},
    {"continue", TokKind::KwContinue}, {"return", TokKind::KwReturn},
    {"true", TokKind::KwTrue}, {"false", TokKind::KwFalse},
    {"int", TokKind::KwInt}, {"long", TokKind::KwLong}, {"float", TokKind::KwFloat},
    {"bool", TokKind::KwBool}, {"void", TokKind::KwVoid},
};

/// Ordered longest-first, so that '<<' wins over '<' and '->' over '-'.
constexpr Keyword kOperators[] = {
    {"&&", TokKind::AndAnd}, {"||", TokKind::OrOr}, {"==", TokKind::Eq},
    {"!=", TokKind::Ne}, {"<=", TokKind::Le}, {">=", TokKind::Ge},
    {"<<", TokKind::Shl}, {">>", TokKind::Shr}, {"->", TokKind::Arrow},
    {"+", TokKind::Plus}, {"-", TokKind::Minus}, {"*", TokKind::Star},
    {"/", TokKind::Slash}, {"%", TokKind::Percent}, {"=", TokKind::Assign},
    {"<", TokKind::Lt}, {">", TokKind::Gt}, {"!", TokKind::Bang},
    {"&", TokKind::Amp}, {"|", TokKind::Pipe}, {"^", TokKind::Caret},
    {"~", TokKind::Tilde}, {"(", TokKind::LParen}, {")", TokKind::RParen},
    {"{", TokKind::LBrace}, {"}", TokKind::RBrace}, {"[", TokKind::LBracket},
    {"]", TokKind::RBracket}, {",", TokKind::Comma}, {";", TokKind::Semi},
    {":", TokKind::Colon},
};

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

bool isHexDigit(char c) {
  return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

class Lexer {
public:
  Lexer(std::string_view source, std::string file)
      : src_(source), file_(std::move(file)) {}

  LexResult run();

private:
  char peek(std::size_t k = 0) const {
    const std::size_t j = i_ + k;
    return j < src_.size() ? src_[j] : '\0';
  }

  char advance() {
    const char c = src_[i_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  bool startsWith(std::string_view s) const { return src_.compare(i_, s.size(), s) == 0; }

  support::SourceLoc here() const { return support::SourceLoc{line_, col_}; }

  void error(support::SourceLoc loc, std::string message) {
    support::Location l;
    l.file = file_;
    l.loc = loc;
    diags_.push_back(support::error("E000", std::move(message), l));
  }

  void skipTrivia();
  Token lexNumber();
  Token lexIdent();

  std::string_view src_;
  std::string file_;
  std::size_t i_ = 0;
  int line_ = 1;
  int col_ = 1;
  support::Diagnostics diags_;
};

void Lexer::skipTrivia() {
  while (i_ < src_.size()) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
    } else if (startsWith("//")) {
      while (i_ < src_.size() && peek() != '\n')
        advance();
    } else if (startsWith("/*")) {
      const support::SourceLoc start = here();
      advance();
      advance();
      for (;;) {
        if (i_ >= src_.size()) {
          error(start, "unterminated block comment");
          return;
        }
        if (startsWith("*/")) {
          advance();
          advance();
          break;
        }
        advance();
      }
    } else {
      return;
    }
  }
}

Token Lexer::lexNumber() {
  const support::SourceLoc loc = here();
  const std::size_t start = i_;

  if (startsWith("0x") || startsWith("0X")) {
    advance();
    advance();
    if (!isHexDigit(peek())) {
      error(loc, "hexadecimal literal has no digits");
      // Still produce a token so the parser sees something plausible.
      return Token{TokKind::IntLit, std::string(src_.substr(start, i_ - start)), loc};
    }
    while (isHexDigit(peek()) || peek() == '_')
      advance();
    return Token{TokKind::IntLit, std::string(src_.substr(start, i_ - start)), loc};
  }

  while (isDigit(peek()) || peek() == '_')
    advance();

  bool isFloat = false;
  // A '.' is part of the number only when a digit follows, so "1..2" does not
  // silently become a float (docs/minilang-spec.md section 1).
  if (peek() == '.' && isDigit(peek(1))) {
    isFloat = true;
    advance();
    while (isDigit(peek()) || peek() == '_')
      advance();
  }
  if ((peek() == 'e' || peek() == 'E') &&
      (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
    isFloat = true;
    advance();
    if (peek() == '+' || peek() == '-')
      advance();
    while (isDigit(peek()))
      advance();
  }

  return Token{isFloat ? TokKind::FloatLit : TokKind::IntLit,
               std::string(src_.substr(start, i_ - start)), loc};
}

Token Lexer::lexIdent() {
  const support::SourceLoc loc = here();
  const std::size_t start = i_;
  while (isAlnum(peek()) || peek() == '_')
    advance();
  const std::string text(src_.substr(start, i_ - start));
  for (const Keyword &kw : kKeywords)
    if (kw.text == text)
      return Token{kw.kind, text, loc};
  return Token{TokKind::Ident, text, loc};
}

LexResult Lexer::run() {
  LexResult result;
  for (;;) {
    skipTrivia();
    if (i_ >= src_.size()) {
      result.tokens.push_back(Token{TokKind::Eof, "", here()});
      break;
    }

    const char c = peek();
    if (isDigit(c)) {
      result.tokens.push_back(lexNumber());
      continue;
    }
    if (isAlpha(c) || c == '_') {
      result.tokens.push_back(lexIdent());
      continue;
    }

    bool matched = false;
    for (const Keyword &op : kOperators) {
      if (startsWith(op.text)) {
        const support::SourceLoc loc = here();
        for (std::size_t k = 0; k < op.text.size(); ++k)
          advance();
        result.tokens.push_back(Token{op.kind, std::string(op.text), loc});
        matched = true;
        break;
      }
    }
    if (!matched) {
      const support::SourceLoc loc = here();
      error(loc, "unexpected character '" + std::string(1, c) + "'");
      // Skip it and keep going, so one bad byte does not hide the rest.
      advance();
    }
  }
  result.diagnostics = std::move(diags_);
  return result;
}

} // namespace

LexResult tokenize(std::string_view source, std::string file) {
  return Lexer(source, std::move(file)).run();
}

} // namespace mtir::frontend
