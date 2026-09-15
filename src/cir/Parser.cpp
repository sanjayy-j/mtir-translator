#include "mtir/cir/Parser.h"

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mtir::cir {
namespace {

using support::Diagnostics;
using support::Location;

// --------------------------------------------------------------------------
// Tokens
// --------------------------------------------------------------------------
enum class TokKind {
  End, Ident, Reg, Global, Number,
  Arrow, LParen, RParen, LBracket, RBracket, LBrace, RBrace,
  Comma, Colon, Question, Equals, Unknown
};

struct Token {
  TokKind kind = TokKind::End;
  std::string text;
  int col = 1; // 1-based
};

bool isIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentBody(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.';
}

bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

/// Lex one line.  The .cir grammar is line-oriented, so there is no need for
/// a lexer that spans lines.
std::vector<Token> lexLine(const std::string &line) {
  std::vector<Token> toks;
  std::size_t i = 0;
  const std::size_t n = line.size();

  const auto push = [&](TokKind kind, std::string text, std::size_t start) {
    toks.push_back(Token{kind, std::move(text), static_cast<int>(start) + 1});
  };

  while (i < n) {
    const char c = line[i];
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      ++i;
      continue;
    }
    const std::size_t start = i;

    if (c == '%' || c == '@') {
      ++i;
      const std::size_t nameStart = i;
      while (i < n && isIdentBody(line[i]))
        ++i;
      push(c == '%' ? TokKind::Reg : TokKind::Global,
           line.substr(nameStart, i - nameStart), start);
      continue;
    }

    if (isIdentStart(c)) {
      while (i < n && isIdentBody(line[i]))
        ++i;
      push(TokKind::Ident, line.substr(start, i - start), start);
      continue;
    }

    if (isDigit(c) || (c == '-' && i + 1 < n &&
                       (isDigit(line[i + 1]) || line[i + 1] == '.' ||
                        line.compare(i + 1, 3, "inf") == 0 ||
                        line.compare(i + 1, 3, "nan") == 0))) {
      if (c == '-')
        ++i;
      if (i < n && isIdentStart(line[i])) { // -inf / -nan
        while (i < n && isIdentBody(line[i]))
          ++i;
      } else {
        while (i < n && (isDigit(line[i]) || line[i] == '.'))
          ++i;
        if (i < n && (line[i] == 'e' || line[i] == 'E')) {
          ++i;
          if (i < n && (line[i] == '+' || line[i] == '-'))
            ++i;
          while (i < n && isDigit(line[i]))
            ++i;
        }
      }
      push(TokKind::Number, line.substr(start, i - start), start);
      continue;
    }

    if (c == '-' && i + 1 < n && line[i + 1] == '>') {
      i += 2;
      push(TokKind::Arrow, "->", start);
      continue;
    }

    ++i;
    switch (c) {
    case '(': push(TokKind::LParen, "(", start); break;
    case ')': push(TokKind::RParen, ")", start); break;
    case '[': push(TokKind::LBracket, "[", start); break;
    case ']': push(TokKind::RBracket, "]", start); break;
    case '{': push(TokKind::LBrace, "{", start); break;
    case '}': push(TokKind::RBrace, "}", start); break;
    case ',': push(TokKind::Comma, ",", start); break;
    case ':': push(TokKind::Colon, ":", start); break;
    case '?': push(TokKind::Question, "?", start); break;
    case '=': push(TokKind::Equals, "=", start); break;
    default: push(TokKind::Unknown, std::string(1, c), start); break;
    }
  }

  toks.push_back(Token{TokKind::End, "", static_cast<int>(n) + 1});
  return toks;
}

// --------------------------------------------------------------------------
// Parser state for one module
// --------------------------------------------------------------------------
class Parser {
public:
  Parser(std::string name, std::string file)
      : module_(std::move(name)), file_(std::move(file)) {}

  void run(std::string_view text);

  ParseResult finish() {
    ParseResult result;
    result.diagnostics = std::move(diags_);
    if (!failed_) {
      module_.rebuildIndex();
      result.module = std::move(module_);
    }
    return result;
  }

private:
  struct PendingLine {
    int line = 0;
    std::string text;
    std::vector<Token> tokens;
  };

  // -- diagnostics -------------------------------------------------------
  void error(int line, int col, std::string message) {
    Location loc;
    loc.file = file_;
    loc.loc = support::SourceLoc{line, col};
    diags_.push_back(support::error("CIR-PARSE", std::move(message), loc));
    failed_ = true;
  }

  // -- helpers -----------------------------------------------------------
  std::optional<Ty> expectTy(const Token &tok, int line) {
    if (tok.kind != TokKind::Ident) {
      error(line, tok.col, "expected a type name");
      return std::nullopt;
    }
    std::optional<Ty> ty = parseTy(tok.text);
    if (!ty)
      error(line, tok.col, "unknown type '" + tok.text + "'");
    return ty;
  }

  /// dest name, opcode and the type the instruction prints.
  struct Head {
    std::optional<std::string> dest;
    Opcode op = Opcode::Trap;
    Ty printed = Ty::Void;
    std::size_t next = 0; // first token index after opcode and type
    bool ok = false;
  };

  Head dissect(const PendingLine &pl, bool report);
  std::optional<Value> parseValue(const Token &tok, Ty context, int line);
  std::optional<Instruction> parseInstruction(const PendingLine &pl);

  void beginFunction(const PendingLine &pl);
  void endFunction(int line);
  void parseGlobal(const PendingLine &pl);

  Module module_;
  std::string file_;
  Diagnostics diags_;
  bool failed_ = false;

  // Current function being collected.
  bool inFunction_ = false;
  std::string fnName_;
  Ty fnRet_ = Ty::Void;
  std::vector<Param> fnParams_;
  std::vector<PendingLine> fnLines_;
  std::unordered_map<std::string, Ty> regTypes_;
};

void Parser::beginFunction(const PendingLine &pl) {
  const std::vector<Token> &t = pl.tokens;
  std::size_t i = 1; // t[0] is "func"

  if (t[i].kind != TokKind::Global) {
    error(pl.line, t[i].col, "expected a function name after 'func'");
    return;
  }
  fnName_ = t[i].text;
  ++i;

  if (t[i].kind != TokKind::LParen) {
    error(pl.line, t[i].col, "expected '(' after the function name");
    return;
  }
  ++i;

  fnParams_.clear();
  regTypes_.clear();
  while (t[i].kind != TokKind::RParen) {
    std::optional<Ty> ty = expectTy(t[i], pl.line);
    if (!ty)
      return;
    ++i;
    if (t[i].kind != TokKind::Reg) {
      error(pl.line, t[i].col, "expected a parameter register");
      return;
    }
    fnParams_.push_back(Param{t[i].text, *ty});
    regTypes_[t[i].text] = *ty;
    ++i;
    if (t[i].kind == TokKind::Comma) {
      ++i;
      continue;
    }
    if (t[i].kind != TokKind::RParen) {
      error(pl.line, t[i].col, "expected ',' or ')' in the parameter list");
      return;
    }
  }
  ++i; // ')'

  if (t[i].kind != TokKind::Arrow) {
    error(pl.line, t[i].col, "expected '->' and a return type");
    return;
  }
  ++i;

  std::optional<Ty> ret = expectTy(t[i], pl.line);
  if (!ret)
    return;
  fnRet_ = *ret;
  ++i;

  if (t[i].kind != TokKind::LBrace) {
    error(pl.line, t[i].col, "expected '{' to open the function body");
    return;
  }

  inFunction_ = true;
  fnLines_.clear();
}

Parser::Head Parser::dissect(const PendingLine &pl, bool report) {
  Head head;
  const auto fail = [&](int line, int col, std::string message) {
    if (report)
      error(line, col, std::move(message));
  };
  const std::vector<Token> &t = pl.tokens;
  std::size_t i = 0;

  if (t[i].kind == TokKind::Reg && t[i + 1].kind == TokKind::Equals) {
    head.dest = t[i].text;
    i += 2;
  }

  if (t[i].kind != TokKind::Ident) {
    fail(pl.line, t[i].col, "expected an opcode");
    return head;
  }

  std::string mnem = t[i].text;
  // The printer spells both terminators 'br' and distinguishes them by the
  // '? then : else' suffix, so the opcode is recovered here rather than by
  // changing the textual syntax.
  if (mnem == "br") {
    for (const Token &tok : t)
      if (tok.kind == TokKind::Question)
        mnem = "br.cond";
  }

  std::optional<Opcode> op = parseOpcode(mnem);
  if (!op) {
    fail(pl.line, t[i].col, "unknown opcode '" + t[i].text + "'");
    return head;
  }
  head.op = *op;
  ++i;

  switch (head.op) {
  case Opcode::Br:
  case Opcode::Trap:
    head.printed = Ty::Void;
    break;
  case Opcode::BrCond:
    head.printed = Ty::I1;
    break;
  case Opcode::PrintI32:
    head.printed = Ty::I32;
    break;
  case Opcode::PrintF64:
    head.printed = Ty::F64;
    break;
  default: {
    if (t[i].kind != TokKind::Ident) {
      fail(pl.line, t[i].col, "expected a type name");
      return head;
    }
    std::optional<Ty> ty = parseTy(t[i].text);
    if (!ty) {
      fail(pl.line, t[i].col, "unknown type '" + t[i].text + "'");
      return head;
    }
    head.printed = *ty;
    ++i;
    break;
  }
  }

  head.next = i;
  head.ok = true;
  return head;
}

std::optional<Value> Parser::parseValue(const Token &tok, Ty context, int line) {
  switch (tok.kind) {
  case TokKind::Reg: {
    const auto it = regTypes_.find(tok.text);
    if (it == regTypes_.end()) {
      error(line, tok.col, "%" + tok.text + " is used but never defined");
      return std::nullopt;
    }
    return Value{Reg{tok.text, it->second}};
  }
  case TokKind::Global:
    return Value{GlobalRef{tok.text}};
  case TokKind::Ident:
    // The printer emits bare `inf` and `nan` for non-finite doubles, and
    // constant folding can produce one (fmul f64 1.0e308, 10.0 overflows), so
    // the parser has to read them back or the round-trip property fails on
    // the compiler's own output.  `-inf` and `-nan` are lexed as Number.
    if (tok.text == "inf")
      return Value{ConstFloat{std::numeric_limits<double>::infinity()}};
    if (tok.text == "nan")
      return Value{ConstFloat{std::numeric_limits<double>::quiet_NaN()}};
    error(line, tok.col, "expected a value, found '" + tok.text + "'");
    return std::nullopt;
  case TokKind::Number: {
    const std::string &s = tok.text;
    const bool isFloatText =
        s.find('.') != std::string::npos || s.find('e') != std::string::npos ||
        s.find('E') != std::string::npos || s.find("inf") != std::string::npos ||
        s.find("nan") != std::string::npos;
    if (isFloatText)
      return Value{ConstFloat{std::strtod(s.c_str(), nullptr)}};
    const Ty ty = isInteger(context) ? context : Ty::I32;
    return Value{ConstInt{static_cast<std::int64_t>(std::strtoll(s.c_str(), nullptr, 10)), ty}};
  }
  default:
    error(line, tok.col, "expected a value, found '" + tok.text + "'");
    return std::nullopt;
  }
}

std::optional<Instruction> Parser::parseInstruction(const PendingLine &pl) {
  const Head head = dissect(pl, /*report=*/true);
  if (!head.ok)
    return std::nullopt;

  const std::vector<Token> &t = pl.tokens;
  std::size_t i = head.next;
  const int line = pl.line;

  Instruction instr;
  instr.op = head.op;
  instr.ty = head.printed;
  if (head.dest)
    instr.dest = Reg{*head.dest, resultType(head.op, head.printed)};

  const auto valueAt = [&](std::size_t index) { return parseValue(t[index], head.printed, line); };

  switch (head.op) {
  case Opcode::Br:
    if (t[i].kind != TokKind::Ident) {
      error(line, t[i].col, "br is missing its target label");
      return std::nullopt;
    }
    instr.labels = {t[i].text};
    return instr;

  case Opcode::BrCond: {
    std::optional<Value> cond = parseValue(t[i], Ty::I1, line);
    if (!cond)
      return std::nullopt;
    instr.args = {*cond};
    ++i;
    if (t[i].kind != TokKind::Question) {
      error(line, t[i].col, "expected '?' in 'br <cond> ? <then> : <else>'");
      return std::nullopt;
    }
    ++i;
    if (t[i].kind != TokKind::Ident) {
      error(line, t[i].col, "expected the 'then' label");
      return std::nullopt;
    }
    instr.labels.push_back(t[i].text);
    ++i;
    if (t[i].kind != TokKind::Colon) {
      error(line, t[i].col, "expected ':' between the branch targets");
      return std::nullopt;
    }
    ++i;
    if (t[i].kind != TokKind::Ident) {
      error(line, t[i].col, "expected the 'else' label");
      return std::nullopt;
    }
    instr.labels.push_back(t[i].text);
    return instr;
  }

  case Opcode::Trap:
    return instr;

  case Opcode::Ret:
    if (head.printed == Ty::Void)
      return instr;
    if (t[i].kind == TokKind::End) {
      error(line, t[i].col, "ret is missing its value");
      return std::nullopt;
    }
    if (std::optional<Value> v = valueAt(i)) {
      instr.args = {*v};
      return instr;
    }
    return std::nullopt;

  case Opcode::Call: {
    if (t[i].kind != TokKind::Global) {
      error(line, t[i].col, "expected '@callee' after the call's return type");
      return std::nullopt;
    }
    instr.callee = t[i].text;
    ++i;
    if (t[i].kind != TokKind::LParen) {
      error(line, t[i].col, "expected '(' after the callee");
      return std::nullopt;
    }
    ++i;
    while (t[i].kind != TokKind::RParen) {
      if (t[i].kind == TokKind::End) {
        error(line, t[i].col, "expected ')' to close the argument list");
        return std::nullopt;
      }
      std::optional<Value> v = parseValue(t[i], Ty::I32, line);
      if (!v)
        return std::nullopt;
      instr.args.push_back(*v);
      ++i;
      if (t[i].kind == TokKind::Comma)
        ++i;
    }
    return instr;
  }

  case Opcode::PrintI32:
  case Opcode::PrintF64:
    if (std::optional<Value> v = valueAt(i)) {
      instr.args = {*v};
      return instr;
    }
    return std::nullopt;

  case Opcode::Alloca:
    if (t[i].kind == TokKind::Comma) {
      ++i;
      std::optional<Value> v = parseValue(t[i], Ty::I32, line);
      if (!v)
        return std::nullopt;
      instr.args = {*v};
    }
    return instr;

  default:
    break;
  }

  // Everything else prints as:  [%dest =] op <ty> [arg {, arg}]
  while (t[i].kind != TokKind::End) {
    std::optional<Value> v = valueAt(i);
    if (!v)
      return std::nullopt;
    instr.args.push_back(*v);
    ++i;
    if (t[i].kind == TokKind::Comma) {
      ++i;
      continue;
    }
    if (t[i].kind != TokKind::End) {
      error(line, t[i].col, "expected ',' or end of instruction");
      return std::nullopt;
    }
  }

  const OpInfo &meta = info(head.op);
  if (meta.arity != kVariadic && instr.args.size() != meta.arity) {
    error(line, t[head.next].col,
          std::string(meta.mnemonic) + " takes " + std::to_string(meta.arity) +
              " operand(s), " + std::to_string(instr.args.size()) + " given");
    return std::nullopt;
  }

  return instr;
}

void Parser::endFunction(int line) {
  Function fn(fnName_, fnParams_, fnRet_);
  std::vector<BasicBlock> blocks;

  for (const PendingLine &pl : fnLines_) {
    const std::vector<Token> &t = pl.tokens;
    // A label line: a bare identifier followed by ':' and nothing else.
    if (t[0].kind == TokKind::Ident && t[1].kind == TokKind::Colon &&
        t[2].kind == TokKind::End) {
      blocks.emplace_back(t[0].text);
      continue;
    }
    if (blocks.empty()) {
      error(pl.line, 1, "instruction before the first block label");
      continue;
    }
    if (std::optional<Instruction> instr = parseInstruction(pl))
      blocks.back().add(std::move(*instr));
  }

  if (blocks.empty())
    error(line, 1, "function @" + fnName_ + " has no basic blocks");

  fn.setBlocks(std::move(blocks));
  module_.addFunction(std::move(fn));
  inFunction_ = false;
}

void Parser::parseGlobal(const PendingLine &pl) {
  const std::vector<Token> &t = pl.tokens;
  std::size_t i = 1; // t[0] is "global"

  if (t[i].kind != TokKind::Global) {
    error(pl.line, t[i].col, "expected a global name after 'global'");
    return;
  }
  Global g;
  g.name = t[i].text;
  ++i;

  if (t[i].kind != TokKind::Colon) {
    error(pl.line, t[i].col, "expected ':' after the global name");
    return;
  }
  ++i;

  std::optional<Ty> ty = expectTy(t[i], pl.line);
  if (!ty)
    return;
  g.ty = *ty;
  ++i;

  if (t[i].kind == TokKind::LBracket) {
    ++i;
    if (t[i].kind != TokKind::Number) {
      error(pl.line, t[i].col, "expected an array length");
      return;
    }
    g.arrayLen = std::atoi(t[i].text.c_str());
    ++i;
    if (t[i].kind != TokKind::RBracket) {
      error(pl.line, t[i].col, "expected ']' after the array length");
      return;
    }
    ++i;
  }

  if (t[i].kind == TokKind::Equals) {
    ++i;
    if (t[i].kind != TokKind::Number) {
      error(pl.line, t[i].col, "a global initialiser must be a literal");
      return;
    }
    const std::string &s = t[i].text;
    if (g.ty == Ty::F64 || s.find('.') != std::string::npos ||
        s.find('e') != std::string::npos || s.find('E') != std::string::npos)
      g.init = Value{ConstFloat{std::strtod(s.c_str(), nullptr)}};
    else
      g.init = Value{ConstInt{static_cast<std::int64_t>(std::strtoll(s.c_str(), nullptr, 10)), g.ty}};
    ++i;
  }

  if (t[i].kind != TokKind::End)
    error(pl.line, t[i].col, "unexpected text after the global declaration");

  module_.addGlobal(std::move(g));
}

void Parser::run(std::string_view text) {
  int lineNo = 0;
  std::size_t pos = 0;

  while (pos <= text.size()) {
    const std::size_t nl = text.find('\n', pos);
    const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
    std::string raw(text.substr(pos, end - pos));
    pos = end + 1;
    ++lineNo;
    if (!raw.empty() && raw.back() == '\r')
      raw.pop_back();

    // Trim, but remember how much so columns stay honest.
    std::size_t first = raw.find_first_not_of(" \t");
    if (first == std::string::npos) {
      if (nl == std::string_view::npos)
        break;
      continue;
    }

    PendingLine pl;
    pl.line = lineNo;
    pl.text = raw;
    pl.tokens = lexLine(raw);

    if (inFunction_) {
      if (pl.tokens[0].kind == TokKind::RBrace && pl.tokens[1].kind == TokKind::End) {
        endFunction(lineNo);
      } else {
        // Pass 1: record what this line defines, so a use may appear in an
        // earlier block than its definition (a loop back edge).
        const Head head = dissect(pl, /*report=*/false);
        if (head.ok && head.dest)
          regTypes_[*head.dest] = resultType(head.op, head.printed);
        fnLines_.push_back(std::move(pl));
      }
    } else if (pl.tokens[0].kind == TokKind::Ident && pl.tokens[0].text == "func") {
      beginFunction(pl);
    } else if (pl.tokens[0].kind == TokKind::Ident && pl.tokens[0].text == "global") {
      parseGlobal(pl);
    } else {
      error(lineNo, pl.tokens[0].col,
            "expected 'func' or 'global', found '" + pl.tokens[0].text + "'");
    }

    if (nl == std::string_view::npos)
      break;
  }

  if (inFunction_)
    error(lineNo, 1, "unterminated function: expected a '}'");
}

} // namespace

ParseResult parseCir(std::string_view text, std::string name, std::string file) {
  Parser parser(std::move(name), std::move(file));
  parser.run(text);
  return parser.finish();
}

} // namespace mtir::cir
