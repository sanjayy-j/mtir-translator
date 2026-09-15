#include "mtir/frontend/Parser.h"

#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "mtir/frontend/Lexer.h"

namespace mtir::frontend {
namespace {

using namespace mtir::ast;

/// Binding power table: token -> (precedence, AST operator).  Higher binds
/// tighter; every one of these is left-associative.  This is the normative
/// `orExpr ... mulExpr` cascade of the spec, collapsed into one routine.
struct BinaryOp {
  TokKind token;
  int precedence;
  std::string_view op;
};

constexpr BinaryOp kBinaryOps[] = {
    {TokKind::OrOr, 1, "||"},
    {TokKind::AndAnd, 2, "&&"},
    {TokKind::Pipe, 3, "|"},
    {TokKind::Caret, 4, "^"},
    {TokKind::Amp, 5, "&"},
    {TokKind::Eq, 6, "=="},
    {TokKind::Ne, 6, "!="},
    {TokKind::Lt, 7, "<"},
    {TokKind::Le, 7, "<="},
    {TokKind::Gt, 7, ">"},
    {TokKind::Ge, 7, ">="},
    {TokKind::Shl, 8, "<<"},
    {TokKind::Shr, 8, ">>"},
    {TokKind::Plus, 9, "+"},
    {TokKind::Minus, 9, "-"},
    {TokKind::Star, 10, "*"},
    {TokKind::Slash, 10, "/"},
    {TokKind::Percent, 10, "%"},
};

const BinaryOp *binaryOpFor(TokKind kind) {
  for (const BinaryOp &e : kBinaryOps)
    if (e.token == kind)
      return &e;
  return nullptr;
}

std::string_view typeKeywordName(TokKind kind) {
  switch (kind) {
  case TokKind::KwInt: return "int";
  case TokKind::KwLong: return "long";
  case TokKind::KwFloat: return "float";
  case TokKind::KwBool: return "bool";
  case TokKind::KwVoid: return "void";
  default: return "";
  }
}

/// Strip the '_' digit separators the spec permits before conversion.
std::string withoutSeparators(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text)
    if (c != '_')
      out += c;
  return out;
}

/// Thrown to unwind to the top of parse() once a syntax error is reported.
/// The parser reports one syntax error and stops: recovery beyond statement
/// level was never implemented in the reference either, and a cascade of
/// derived errors is worse than one accurate one.
struct Bail {};

class Parser {
public:
  Parser(std::vector<Token> tokens, std::string file)
      : toks_(std::move(tokens)), file_(std::move(file)) {}

  ParseResult run();
  support::Diagnostics &diagnostics() { return diags_; }

private:
  // -- token helpers -----------------------------------------------------
  const Token &cur() const { return toks_[pos_]; }
  bool at(TokKind kind) const { return cur().kind == kind; }

  bool accept(TokKind kind) {
    if (!at(kind))
      return false;
    ++pos_;
    return true;
  }

  const Token &expect(TokKind kind, std::string_view what) {
    if (!at(kind))
      fail(std::string("expected ") + std::string(what) + ", found " + describe(cur()));
    return toks_[pos_++];
  }

  static std::string describe(const Token &tok) {
    if (tok.kind == TokKind::Eof)
      return "end of file";
    return "'" + tok.text + "'";
  }

  [[noreturn]] void fail(std::string message) {
    support::Location loc;
    loc.file = file_;
    loc.loc = cur().loc;
    diags_.push_back(support::error("E000", "syntax error: " + std::move(message), loc));
    throw Bail{};
  }

  // -- grammar -----------------------------------------------------------
  DeclPtr parseFn();
  DeclPtr parseGlobal();
  TypeNode parseType();
  std::unique_ptr<Block> parseBlock();
  StmtPtr parseStmt();
  StmtPtr parseSimpleStmt();
  StmtPtr parseLet(bool consumeSemi);
  StmtPtr parseIf();
  StmtPtr parseWhile();
  StmtPtr parseFor();
  ExprPtr parseExpr(int minPrecedence = 1);
  ExprPtr parseUnary();
  ExprPtr parsePostfix();
  ExprPtr parsePrimary();

  std::vector<Token> toks_;
  std::string file_;
  std::size_t pos_ = 0;
  support::Diagnostics diags_;
};

// -- declarations ----------------------------------------------------------
DeclPtr Parser::parseFn() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwFn, "'fn'");
  const std::string name = expect(TokKind::Ident, "a function name").text;
  expect(TokKind::LParen, "'('");

  std::vector<Param> params;
  if (!at(TokKind::RParen)) {
    for (;;) {
      const Token &nameTok = expect(TokKind::Ident, "a parameter name");
      const support::SourceLoc pLoc = nameTok.loc;
      const std::string pName = nameTok.text;
      expect(TokKind::Colon, "':'");
      params.push_back(Param{pName, parseType(), pLoc});
      if (!accept(TokKind::Comma))
        break;
    }
  }
  expect(TokKind::RParen, "')'");

  TypeNode ret{"void", std::nullopt, cur().loc};
  if (accept(TokKind::Arrow))
    ret = parseType();

  return std::make_unique<FnDecl>(name, std::move(params), std::move(ret),
                                  parseBlock(), loc);
}

DeclPtr Parser::parseGlobal() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwGlobal, "'global'");
  const std::string name = expect(TokKind::Ident, "a global name").text;
  expect(TokKind::Colon, "':'");
  TypeNode type = parseType();
  ExprPtr init;
  if (accept(TokKind::Assign))
    init = parseExpr();
  expect(TokKind::Semi, "';'");
  return std::make_unique<GlobalDecl>(name, std::move(type), std::move(init), loc);
}

TypeNode Parser::parseType() {
  const Token &tok = cur();
  if (!isTypeKeyword(tok.kind))
    fail("expected a type name, found " + describe(tok));
  ++pos_;

  TypeNode type{std::string(typeKeywordName(tok.kind)), std::nullopt, tok.loc};
  if (accept(TokKind::LBracket)) {
    const Token &len = expect(TokKind::IntLit, "an array length");
    type.arrayLength = static_cast<int>(
        std::strtol(withoutSeparators(len.text).c_str(), nullptr, 0));
    expect(TokKind::RBracket, "']'");
  }
  return type;
}

// -- statements ------------------------------------------------------------
std::unique_ptr<Block> Parser::parseBlock() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::LBrace, "'{'");
  std::vector<StmtPtr> stmts;
  while (!at(TokKind::RBrace)) {
    if (at(TokKind::Eof))
      fail("unterminated block: expected '}'");
    stmts.push_back(parseStmt());
  }
  expect(TokKind::RBrace, "'}'");
  return std::make_unique<Block>(std::move(stmts), loc);
}

StmtPtr Parser::parseStmt() {
  const support::SourceLoc loc = cur().loc;

  if (at(TokKind::KwLet))
    return parseLet(/*consumeSemi=*/true);
  if (at(TokKind::KwIf))
    return parseIf();
  if (at(TokKind::KwWhile))
    return parseWhile();
  if (at(TokKind::KwFor))
    return parseFor();
  if (at(TokKind::LBrace))
    return parseBlock();

  if (accept(TokKind::KwBreak)) {
    expect(TokKind::Semi, "';'");
    return std::make_unique<Break>(loc);
  }
  if (accept(TokKind::KwContinue)) {
    expect(TokKind::Semi, "';'");
    return std::make_unique<Continue>(loc);
  }
  if (accept(TokKind::KwReturn)) {
    ExprPtr value;
    if (!at(TokKind::Semi))
      value = parseExpr();
    expect(TokKind::Semi, "';'");
    return std::make_unique<Return>(std::move(value), loc);
  }

  StmtPtr stmt = parseSimpleStmt();
  expect(TokKind::Semi, "';'");
  return stmt;
}

/// An assignment or a bare expression, without the trailing ';'.  Shared with
/// the init and step clauses of a for-loop, which is why the semicolon is the
/// caller's responsibility.
StmtPtr Parser::parseSimpleStmt() {
  const support::SourceLoc loc = cur().loc;
  if (at(TokKind::KwLet))
    return parseLet(/*consumeSemi=*/false);

  ExprPtr expr = parseExpr();
  if (accept(TokKind::Assign)) {
    if (expr->kind() != NodeKind::VarRef && expr->kind() != NodeKind::Index) {
      support::Location l;
      l.file = file_;
      l.loc = loc;
      diags_.push_back(support::error(
          "E000", "syntax error: left-hand side of assignment is not assignable", l));
      throw Bail{};
    }
    return std::make_unique<Assign>(std::move(expr), parseExpr(), loc);
  }
  return std::make_unique<ExprStmt>(std::move(expr), loc);
}

StmtPtr Parser::parseLet(bool consumeSemi) {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwLet, "'let'");
  const std::string name = expect(TokKind::Ident, "a variable name").text;
  expect(TokKind::Colon, "':'");
  TypeNode type = parseType();
  ExprPtr init;
  if (accept(TokKind::Assign))
    init = parseExpr();
  if (consumeSemi)
    expect(TokKind::Semi, "';'");
  return std::make_unique<Let>(name, std::move(type), std::move(init), loc);
}

StmtPtr Parser::parseIf() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwIf, "'if'");
  expect(TokKind::LParen, "'('");
  ExprPtr cond = parseExpr();
  expect(TokKind::RParen, "')'");
  StmtPtr thenBlock = parseBlock();

  StmtPtr elseBlock;
  if (accept(TokKind::KwElse)) {
    // `else if` chains without a dangling-else rule, because every branch
    // body is a braced block.
    elseBlock = at(TokKind::KwIf) ? parseIf() : StmtPtr(parseBlock());
  }
  return std::make_unique<If>(std::move(cond), std::move(thenBlock),
                              std::move(elseBlock), loc);
}

StmtPtr Parser::parseWhile() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwWhile, "'while'");
  expect(TokKind::LParen, "'('");
  ExprPtr cond = parseExpr();
  expect(TokKind::RParen, "')'");
  return std::make_unique<While>(std::move(cond), parseBlock(), loc);
}

StmtPtr Parser::parseFor() {
  const support::SourceLoc loc = cur().loc;
  expect(TokKind::KwFor, "'for'");
  expect(TokKind::LParen, "'('");

  StmtPtr init;
  if (!at(TokKind::Semi))
    init = parseSimpleStmt();
  expect(TokKind::Semi, "';'");

  ExprPtr cond;
  if (!at(TokKind::Semi))
    cond = parseExpr();
  expect(TokKind::Semi, "';'");

  StmtPtr step;
  if (!at(TokKind::RParen))
    step = parseSimpleStmt();
  expect(TokKind::RParen, "')'");

  return std::make_unique<For>(std::move(init), std::move(cond), std::move(step),
                               parseBlock(), loc);
}

// -- expressions (precedence climbing) -------------------------------------
ExprPtr Parser::parseExpr(int minPrecedence) {
  ExprPtr lhs = parseUnary();
  for (;;) {
    const BinaryOp *entry = binaryOpFor(cur().kind);
    if (entry == nullptr || entry->precedence < minPrecedence)
      return lhs;
    const support::SourceLoc loc = cur().loc;
    ++pos_;
    ExprPtr rhs = parseExpr(entry->precedence + 1); // left-associative
    lhs = std::make_unique<Binary>(std::string(entry->op), std::move(lhs),
                                   std::move(rhs), loc);
  }
}

ExprPtr Parser::parseUnary() {
  const support::SourceLoc loc = cur().loc;
  if (accept(TokKind::Minus))
    return std::make_unique<Unary>("-", parseUnary(), loc);
  if (accept(TokKind::Bang))
    return std::make_unique<Unary>("!", parseUnary(), loc);
  if (accept(TokKind::Tilde))
    return std::make_unique<Unary>("~", parseUnary(), loc);
  return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
  ExprPtr expr = parsePrimary();
  while (at(TokKind::LBracket)) {
    const support::SourceLoc loc = cur().loc;
    ++pos_;
    ExprPtr index = parseExpr();
    expect(TokKind::RBracket, "']'");
    expr = std::make_unique<Index>(std::move(expr), std::move(index), loc);
  }
  return expr;
}

ExprPtr Parser::parsePrimary() {
  const Token tok = cur();

  if (accept(TokKind::LParen)) {
    ExprPtr expr = parseExpr();
    expect(TokKind::RParen, "')'");
    return expr;
  }
  if (accept(TokKind::IntLit)) {
    const std::string text = withoutSeparators(tok.text);
    return std::make_unique<IntLit>(
        static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 0)), tok.loc);
  }
  if (accept(TokKind::FloatLit))
    return std::make_unique<FloatLit>(
        std::strtod(withoutSeparators(tok.text).c_str(), nullptr), tok.loc);
  if (accept(TokKind::KwTrue))
    return std::make_unique<BoolLit>(true, tok.loc);
  if (accept(TokKind::KwFalse))
    return std::make_unique<BoolLit>(false, tok.loc);

  if (accept(TokKind::Ident)) {
    if (accept(TokKind::LParen)) {
      std::vector<ExprPtr> args;
      if (!at(TokKind::RParen)) {
        for (;;) {
          args.push_back(parseExpr());
          if (!accept(TokKind::Comma))
            break;
        }
      }
      expect(TokKind::RParen, "')'");
      return std::make_unique<Call>(tok.text, std::move(args), tok.loc);
    }
    return std::make_unique<VarRef>(tok.text, tok.loc);
  }

  fail("expected an expression, found " + describe(tok));
}

ParseResult Parser::run() {
  ParseResult result;
  Program program;
  try {
    while (!at(TokKind::Eof)) {
      if (at(TokKind::KwFn))
        program.add(parseFn());
      else if (at(TokKind::KwGlobal))
        program.add(parseGlobal());
      else
        fail("expected 'fn' or 'global' at top level, found " + describe(cur()));
    }
    result.program = std::move(program);
  } catch (const Bail &) {
    // The diagnostic is already recorded; result.program stays empty.
  }
  result.diagnostics = std::move(diags_);
  return result;
}

} // namespace

ParseResult parse(std::string_view source, std::string file) {
  LexResult lexed = tokenize(source, file);
  if (!lexed.ok()) {
    ParseResult result;
    result.diagnostics = std::move(lexed.diagnostics);
    return result;
  }

  Parser parser(std::move(lexed.tokens), std::move(file));
  ParseResult result = parser.run();
  // Lexer warnings (if any) belong in the same list.
  result.diagnostics.insert(result.diagnostics.begin(), lexed.diagnostics.begin(),
                            lexed.diagnostics.end());
  return result;
}

} // namespace mtir::frontend
