#include "mtir/sema/Analyse.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mtir::sema {
namespace {

using namespace mtir::ast;
using cir::Ty;

/// The only conversions MiniLang inserts automatically
/// (docs/minilang-spec.md section 4).  Everything else must be written out,
/// and since the language has no cast syntax, "everything else" is E005.
bool isAutomaticWidening(Ty from, Ty to) {
  if (from == Ty::I32 && (to == Ty::I64 || to == Ty::F64))
    return true;
  if (from == Ty::I1 && (to == Ty::I32 || to == Ty::I64))
    return true;
  return false;
}

std::string tyName(Ty t) { return std::string(cir::toString(t)); }

/// A scope chain.  declare() rejects a duplicate in the *current* scope only,
/// so shadowing an outer name is permitted (section 5) but re-declaring in
/// the same block is E002.
class Scopes {
public:
  void push() { scopes_.emplace_back(); }
  void pop() { scopes_.pop_back(); }

  bool declare(const std::string &name, Symbol symbol) {
    auto &top = scopes_.back();
    if (top.count(name) != 0)
      return false;
    top.emplace(name, std::move(symbol));
    return true;
  }

  const Symbol *lookup(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      const auto found = it->find(name);
      if (found != it->end())
        return &found->second;
    }
    return nullptr;
  }

private:
  std::vector<std::unordered_map<std::string, Symbol>> scopes_;
};

class Checker {
public:
  Checker(std::string file, TypeInfo &types) : file_(std::move(file)), types_(types) {}

  void run(Program &program);
  support::Diagnostics take() { return std::move(diags_); }

private:
  // -- diagnostics -------------------------------------------------------
  void error(const std::string &code, std::string message, support::SourceLoc loc) {
    support::Location l;
    l.file = file_;
    l.loc = loc;
    diags_.push_back(support::error(code, std::move(message), l));
  }

  // -- declarations ------------------------------------------------------
  void collectSignatures(Program &program);
  void checkGlobal(GlobalDecl &decl);
  void checkFunction(FnDecl &decl);

  // -- statements --------------------------------------------------------
  void checkStmt(Stmt &stmt);
  void checkBlock(Block &block);
  void checkLet(Let &stmt);
  void checkAssign(Assign &stmt);
  void checkReturn(Return &stmt);

  // -- expressions -------------------------------------------------------
  /// Type an expression.  Takes the owning slot so a Conv can be inserted.
  Ty checkExpr(ExprPtr &slot);
  Ty checkVarRef(VarRef &ref);
  Ty checkUnary(Unary &expr);
  Ty checkBinary(Binary &expr);
  Ty checkCall(Call &expr);
  Ty checkIndex(Index &expr);

  /// Wrap `slot` in a Conv if widening it to `want` is automatic; report E005
  /// (or `code`) when it is not.  Returns true when the value now has `want`.
  bool coerce(ExprPtr &slot, Ty want, const std::string &code,
              const std::string &context);

  /// Does every path through this statement return?  Used for E009.
  static bool alwaysReturns(const Stmt &stmt);

  /// Record every parameter that the body assigns, for TypeInfo::isAssigned.
  void collectAssignedParams(const FnDecl &decl);
  void scanForAssignments(const Stmt &stmt, const FnDecl &decl);

  std::string file_;
  TypeInfo &types_;
  support::Diagnostics diags_;
  Scopes scopes_;
  Ty returnType_ = Ty::Void;
  int loopDepth_ = 0;
};

// ==========================================================================
// Declarations
// ==========================================================================
void Checker::collectSignatures(Program &program) {
  // Two built-ins make up the whole observable-output surface
  // (docs/minilang-spec.md section 8).
  types_.setFunction("print_int", Signature{Ty::Void, {Ty::I32}, true});
  types_.setFunction("print_float", Signature{Ty::Void, {Ty::F64}, true});

  for (const DeclPtr &decl : program.decls()) {
    const auto *fn = dynCast<FnDecl>(decl.get());
    if (fn == nullptr)
      continue;
    if (types_.functionSignature(fn->name()) != nullptr) {
      error("E002", "duplicate declaration of function '" + fn->name() + "'",
            fn->loc());
      continue;
    }
    Signature sig;
    sig.returnType = typeOfNode(fn->returnType());
    for (const Param &p : fn->params())
      sig.parameters.push_back(typeOfNode(p.type));
    types_.setFunction(fn->name(), std::move(sig));
  }
}

void Checker::checkGlobal(GlobalDecl &decl) {
  Symbol sym;
  sym.kind = SymbolKind::Global;
  sym.name = decl.name();
  sym.type = decl.type().isArray() ? typeOfNode(TypeNode{decl.type().name, std::nullopt,
                                                         decl.type().loc})
                                   : typeOfNode(decl.type());
  sym.arrayLength = decl.type().arrayLength;

  if (!scopes_.declare(decl.name(), sym))
    error("E002", "duplicate declaration of '" + decl.name() + "'", decl.loc());

  if (decl.init() == nullptr)
    return;

  if (decl.type().isArray()) {
    // See docs/decisions/0001-array-initialisers.md: the specification gives
    // an array initialiser no meaning, so accepting one would be inventing
    // semantics.  Rejecting it is the documented recommendation.
    error("E004",
          "an array declaration cannot take an initialiser; "
          "see docs/decisions/0001-array-initialisers.md",
          decl.loc());
    return;
  }

  const Ty want = typeOfNode(decl.type());
  coerce(decl.initSlot(), want, "E004",
         "initialiser for global '" + decl.name() + "'");
}

void Checker::checkFunction(FnDecl &decl) {
  returnType_ = typeOfNode(decl.returnType());
  loopDepth_ = 0;

  scopes_.push();
  for (const Param &p : decl.params()) {
    Symbol sym;
    sym.kind = SymbolKind::Parameter;
    sym.name = p.name;
    sym.type = p.type.isArray()
                   ? typeOfNode(TypeNode{p.type.name, std::nullopt, p.type.loc})
                   : typeOfNode(p.type);
    sym.arrayLength = p.type.arrayLength;
    if (!scopes_.declare(p.name, sym))
      error("E002", "duplicate parameter '" + p.name + "'", p.loc);
  }

  collectAssignedParams(decl);
  checkBlock(decl.bodyRef());
  scopes_.pop();

  // E009: a function with a non-void return type must return on every path.
  if (returnType_ != Ty::Void && !alwaysReturns(decl.body()))
    error("E009", "function '" + decl.name() + "' does not return on every path",
          decl.loc());
}

// ==========================================================================
// Assignment scan, for isAssigned
// ==========================================================================
void Checker::scanForAssignments(const Stmt &stmt, const FnDecl &decl) {
  switch (stmt.kind()) {
  case NodeKind::Assign: {
    const auto &a = static_cast<const Assign &>(stmt);
    if (const auto *ref = dynCast<VarRef>(&a.target()))
      for (const Param &p : decl.params())
        if (p.name == ref->name())
          types_.markAssigned(p);
    return;
  }
  case NodeKind::Block:
    for (const StmtPtr &s : static_cast<const Block &>(stmt).statements())
      scanForAssignments(*s, decl);
    return;
  case NodeKind::If: {
    const auto &n = static_cast<const If &>(stmt);
    scanForAssignments(n.thenBlock(), decl);
    if (n.elseBlock() != nullptr)
      scanForAssignments(*n.elseBlock(), decl);
    return;
  }
  case NodeKind::While:
    scanForAssignments(static_cast<const While &>(stmt).body(), decl);
    return;
  case NodeKind::For: {
    const auto &n = static_cast<const For &>(stmt);
    if (n.init() != nullptr)
      scanForAssignments(*n.init(), decl);
    if (n.step() != nullptr)
      scanForAssignments(*n.step(), decl);
    scanForAssignments(n.body(), decl);
    return;
  }
  default:
    return;
  }
}

void Checker::collectAssignedParams(const FnDecl &decl) {
  scanForAssignments(decl.body(), decl);
}

// ==========================================================================
// E009: does every path return?
// ==========================================================================
bool Checker::alwaysReturns(const Stmt &stmt) {
  switch (stmt.kind()) {
  case NodeKind::Return:
    return true;
  case NodeKind::Block: {
    for (const StmtPtr &s : static_cast<const Block &>(stmt).statements())
      if (alwaysReturns(*s))
        return true;
    return false;
  }
  case NodeKind::If: {
    const auto &n = static_cast<const If &>(stmt);
    // Only an if/else where *both* arms return covers every path.
    return n.elseBlock() != nullptr && alwaysReturns(n.thenBlock()) &&
           alwaysReturns(*n.elseBlock());
  }
  default:
    // A loop is not counted even when its condition is a constant true: the
    // checker does not evaluate conditions, and claiming otherwise would be
    // an analysis this milestone does not have.
    return false;
  }
}

// ==========================================================================
// Statements
// ==========================================================================
void Checker::checkBlock(Block &block) {
  scopes_.push();
  for (StmtPtr &s : block.statementSlots())
    checkStmt(*s);
  scopes_.pop();
}

void Checker::checkStmt(Stmt &stmt) {
  switch (stmt.kind()) {
  case NodeKind::Block:
    checkBlock(static_cast<Block &>(stmt));
    return;
  case NodeKind::Let:
    checkLet(static_cast<Let &>(stmt));
    return;
  case NodeKind::Assign:
    checkAssign(static_cast<Assign &>(stmt));
    return;
  case NodeKind::ExprStmt:
    checkExpr(static_cast<ExprStmt &>(stmt).exprSlot());
    return;
  case NodeKind::Return:
    checkReturn(static_cast<Return &>(stmt));
    return;
  case NodeKind::If: {
    auto &n = static_cast<If &>(stmt);
    const Ty cond = checkExpr(n.condSlot());
    if (cond != Ty::I1 && cond != Ty::Void)
      error("E003", "condition of 'if' has type " + tyName(cond) + ", expected bool",
            n.cond().loc());
    checkStmt(*n.thenSlot());
    if (n.elseSlot())
      checkStmt(*n.elseSlot());
    return;
  }
  case NodeKind::While: {
    auto &n = static_cast<While &>(stmt);
    const Ty cond = checkExpr(n.condSlot());
    if (cond != Ty::I1 && cond != Ty::Void)
      error("E003", "condition of 'while' has type " + tyName(cond) + ", expected bool",
            n.cond().loc());
    ++loopDepth_;
    checkStmt(*n.bodySlot());
    --loopDepth_;
    return;
  }
  case NodeKind::For: {
    auto &n = static_cast<For &>(stmt);
    // The init clause scopes to the loop.
    scopes_.push();
    if (n.initSlot())
      checkStmt(*n.initSlot());
    if (n.condSlot()) {
      const Ty cond = checkExpr(n.condSlot());
      if (cond != Ty::I1 && cond != Ty::Void)
        error("E003", "condition of 'for' has type " + tyName(cond) + ", expected bool",
              n.cond()->loc());
    }
    ++loopDepth_;
    checkStmt(*n.bodySlot());
    --loopDepth_;
    if (n.stepSlot())
      checkStmt(*n.stepSlot());
    scopes_.pop();
    return;
  }
  case NodeKind::Break:
    if (loopDepth_ == 0)
      error("E011", "'break' outside a loop", stmt.loc());
    return;
  case NodeKind::Continue:
    if (loopDepth_ == 0)
      error("E011", "'continue' outside a loop", stmt.loc());
    return;
  default:
    return;
  }
}

void Checker::checkLet(Let &stmt) {
  Symbol sym;
  sym.kind = SymbolKind::Local;
  sym.name = stmt.name();
  sym.type = stmt.type().isArray()
                 ? typeOfNode(TypeNode{stmt.type().name, std::nullopt, stmt.type().loc})
                 : typeOfNode(stmt.type());
  sym.arrayLength = stmt.type().arrayLength;

  if (stmt.init() != nullptr) {
    if (stmt.type().isArray()) {
      error("E004",
            "an array declaration cannot take an initialiser; "
            "see docs/decisions/0001-array-initialisers.md",
            stmt.loc());
    } else {
      coerce(stmt.initSlot(), typeOfNode(stmt.type()), "E004",
             "initialiser for '" + stmt.name() + "'");
    }
  }

  // Declared after checking the initialiser, so `let x: int = x;` reports
  // E001 against the outer x rather than resolving to itself.
  if (!scopes_.declare(stmt.name(), sym))
    error("E002", "duplicate declaration of '" + stmt.name() + "' in this scope",
          stmt.loc());
}

void Checker::checkAssign(Assign &stmt) {
  const Ty target = checkExpr(stmt.targetSlot());
  if (target == Ty::Void)
    return; // the target was already reported

  if (const auto *ref = dynCast<VarRef>(&stmt.target())) {
    const Symbol *sym = types_.resolve(*ref);
    if (sym != nullptr && sym->kind == SymbolKind::Function)
      error("E004", "cannot assign to function '" + ref->name() + "'", stmt.loc());
    if (sym != nullptr && sym->isArray())
      error("E004", "cannot assign to the array '" + ref->name() + "' as a whole",
            stmt.loc());
  }

  coerce(stmt.valueSlot(), target, "E004", "assignment");
}

void Checker::checkReturn(Return &stmt) {
  if (stmt.value() == nullptr) {
    if (returnType_ != Ty::Void)
      error("E009",
            "'return' without a value in a function returning " + tyName(returnType_),
            stmt.loc());
    return;
  }
  if (returnType_ == Ty::Void) {
    checkExpr(stmt.valueSlot());
    error("E010", "'return' with a value in a void function", stmt.loc());
    return;
  }
  coerce(stmt.valueSlot(), returnType_, "E004", "return value");
}

// ==========================================================================
// Expressions
// ==========================================================================
bool Checker::coerce(ExprPtr &slot, Ty want, const std::string &code,
                     const std::string &context) {
  const Ty have = checkExpr(slot);
  if (have == want)
    return true;
  if (have == Ty::Void || want == Ty::Void)
    return false; // already reported, or nothing sensible to say

  if (isAutomaticWidening(have, want)) {
    // docs/minilang-spec.md section 4: the type checker inserts an explicit
    // conversion node.  The builder lowers it and never sees a widening rule.
    const support::SourceLoc loc = slot->loc();
    ExprPtr inner = std::move(slot);
    auto conv = std::make_unique<Conv>(std::move(inner), loc);
    types_.setType(*conv, want);
    slot = std::move(conv);
    return true;
  }

  // Which diagnostic applies depends on *why* the conversion is refused, not
  // on where it appeared.  If the reverse direction would have been an
  // automatic widening then this one is a narrowing, and section 7 has a
  // specific code for that (E005) which is more informative than the generic
  // "types do not match" of the caller's context.  Otherwise the two types
  // are simply unrelated, and the caller's code -- E003 for a binary
  // operation, E004 for an assignment or initialiser, E008 for an argument --
  // is the right one.
  const bool narrowing = isAutomaticWidening(want, have);
  error(narrowing ? "E005" : code,
        (narrowing ? "narrowing conversion from " : "cannot convert ") + tyName(have) +
            " to " + tyName(want) + " in " + context +
            (narrowing ? "; MiniLang has no cast syntax, so this must be avoided"
                       : ""),
        slot->loc());
  return false;
}

Ty Checker::checkExpr(ExprPtr &slot) {
  Expr &expr = *slot;
  Ty result = Ty::Void;

  switch (expr.kind()) {
  case NodeKind::IntLit:
    result = Ty::I32;
    break;
  case NodeKind::FloatLit:
    result = Ty::F64;
    break;
  case NodeKind::BoolLit:
    result = Ty::I1;
    break;
  case NodeKind::VarRef:
    result = checkVarRef(static_cast<VarRef &>(expr));
    break;
  case NodeKind::Unary:
    result = checkUnary(static_cast<Unary &>(expr));
    break;
  case NodeKind::Binary:
    result = checkBinary(static_cast<Binary &>(expr));
    break;
  case NodeKind::Call:
    result = checkCall(static_cast<Call &>(expr));
    break;
  case NodeKind::Index:
    result = checkIndex(static_cast<Index &>(expr));
    break;
  case NodeKind::Conv:
    // Only the checker builds these, and it types them as it does so.
    return types_.typeOf(expr);
  default:
    break;
  }

  types_.setType(expr, result);
  return result;
}

Ty Checker::checkVarRef(VarRef &ref) {
  const Symbol *sym = scopes_.lookup(ref.name());
  if (sym == nullptr) {
    error("E001", "undeclared identifier '" + ref.name() + "'", ref.loc());
    return Ty::Void;
  }
  types_.setSymbol(ref, *sym);
  // An array name used as a value is its base address.
  return sym->isArray() ? Ty::Ptr : sym->type;
}

Ty Checker::checkUnary(Unary &expr) {
  const Ty operand = checkExpr(expr.operandSlot());
  if (operand == Ty::Void)
    return Ty::Void;

  if (expr.op() == "-") {
    if (!cir::isInteger(operand) && operand != Ty::F64) {
      error("E003", "unary '-' requires a numeric operand, found " + tyName(operand),
            expr.loc());
      return Ty::Void;
    }
    return operand;
  }
  if (expr.op() == "~") {
    if (!cir::isInteger(operand)) {
      error("E003", "'~' requires an integer operand, found " + tyName(operand),
            expr.loc());
      return Ty::Void;
    }
    return operand;
  }
  // "!"
  if (operand != Ty::I1) {
    error("E003", "'!' requires a bool operand, found " + tyName(operand), expr.loc());
    return Ty::Void;
  }
  return Ty::I1;
}

Ty Checker::checkBinary(Binary &expr) {
  const std::string &op = expr.op();

  // Shifts type independently: the count need not match the value's type.
  if (op == "<<" || op == ">>") {
    const Ty lhs = checkExpr(expr.lhsSlot());
    const Ty rhs = checkExpr(expr.rhsSlot());
    if (lhs == Ty::Void || rhs == Ty::Void)
      return Ty::Void;
    if (!cir::isInteger(lhs) || !cir::isInteger(rhs)) {
      error("E003", "shift requires integer operands, found " + tyName(lhs) + " and " +
                        tyName(rhs),
            expr.loc());
      return Ty::Void;
    }
    return lhs;
  }

  if (op == "&&" || op == "||") {
    const Ty lhs = checkExpr(expr.lhsSlot());
    const Ty rhs = checkExpr(expr.rhsSlot());
    if (lhs == Ty::Void || rhs == Ty::Void)
      return Ty::Void;
    if (lhs != Ty::I1 || rhs != Ty::I1) {
      error("E003", "'" + op + "' requires bool operands, found " + tyName(lhs) +
                        " and " + tyName(rhs),
            expr.loc());
      return Ty::Void;
    }
    return Ty::I1;
  }

  Ty lhs = checkExpr(expr.lhsSlot());
  Ty rhs = checkExpr(expr.rhsSlot());
  if (lhs == Ty::Void || rhs == Ty::Void)
    return Ty::Void;

  // Unify by widening the narrower side, using the same automatic rules.
  if (lhs != rhs) {
    if (isAutomaticWidening(lhs, rhs)) {
      coerce(expr.lhsSlot(), rhs, "E003", "binary '" + op + "'");
      lhs = rhs;
    } else if (isAutomaticWidening(rhs, lhs)) {
      coerce(expr.rhsSlot(), lhs, "E003", "binary '" + op + "'");
      rhs = lhs;
    } else {
      error("E003", "operands of '" + op + "' have incompatible types " + tyName(lhs) +
                        " and " + tyName(rhs),
            expr.loc());
      return Ty::Void;
    }
  }

  const bool comparison =
      op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
  if (comparison)
    return Ty::I1;

  const bool bitwise = op == "&" || op == "|" || op == "^";
  const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";

  if (bitwise && !cir::isInteger(lhs)) {
    error("E003", "'" + op + "' requires integer operands, found " + tyName(lhs),
          expr.loc());
    return Ty::Void;
  }
  if (arithmetic && !cir::isInteger(lhs) && lhs != Ty::F64) {
    error("E003", "'" + op + "' is not defined on " + tyName(lhs), expr.loc());
    return Ty::Void;
  }
  if (op == "%" && lhs == Ty::F64) {
    error("E003", "'%' is not defined on float", expr.loc());
    return Ty::Void;
  }
  return lhs;
}

Ty Checker::checkCall(Call &expr) {
  const Signature *sig = types_.functionSignature(expr.callee());
  if (sig == nullptr) {
    // Type the arguments anyway, so a second error inside them is still found.
    for (ExprPtr &a : expr.argSlots())
      checkExpr(a);
    error("E006", "call to undeclared function '" + expr.callee() + "'", expr.loc());
    return Ty::Void;
  }

  if (sig->parameters.size() != expr.args().size()) {
    for (ExprPtr &a : expr.argSlots())
      checkExpr(a);
    error("E007", "'" + expr.callee() + "' takes " +
                      std::to_string(sig->parameters.size()) + " argument(s), " +
                      std::to_string(expr.args().size()) + " given",
          expr.loc());
    return sig->returnType;
  }

  for (std::size_t i = 0; i < expr.argSlots().size(); ++i) {
    coerce(expr.argSlots()[i], sig->parameters[i], "E008",
           "argument " + std::to_string(i + 1) + " of '" + expr.callee() + "'");
  }

  types_.setSignature(expr, *sig);
  return sig->returnType;
}

Ty Checker::checkIndex(Index &expr) {
  const auto *base = dynCast<VarRef>(&expr.base());
  if (base == nullptr) {
    checkExpr(expr.indexSlot());
    error("E012", "only a named array can be indexed", expr.loc());
    return Ty::Void;
  }

  const Symbol *sym = scopes_.lookup(base->name());
  if (sym == nullptr) {
    error("E001", "undeclared identifier '" + base->name() + "'", base->loc());
    checkExpr(expr.indexSlot());
    return Ty::Void;
  }
  types_.setSymbol(*base, *sym);
  types_.setType(*base, sym->isArray() ? Ty::Ptr : sym->type);

  if (!sym->isArray()) {
    checkExpr(expr.indexSlot());
    error("E012", "'" + base->name() + "' is not an array", expr.loc());
    return Ty::Void;
  }

  const Ty index = checkExpr(expr.indexSlot());
  if (index != Ty::Void && !cir::isInteger(index)) {
    error("E012", "array index has type " + tyName(index) + ", expected an integer",
          expr.index().loc());
    return Ty::Void;
  }
  // The element type; the builder emits gep + load/store around it.
  types_.setSymbol(expr, *sym);
  return sym->type;
}

void Checker::run(Program &program) {
  scopes_.push(); // the global scope
  collectSignatures(program);

  for (const DeclPtr &decl : program.decls())
    if (const auto *fn = dynCast<FnDecl>(decl.get())) {
      Symbol sym;
      sym.kind = SymbolKind::Function;
      sym.name = fn->name();
      sym.type = typeOfNode(fn->returnType());
      scopes_.declare(fn->name(), sym);
    }

  for (DeclPtr &decl : program.declSlots())
    if (auto *g = dynCast<GlobalDecl>(decl.get()))
      checkGlobal(*g);

  for (DeclPtr &decl : program.declSlots())
    if (auto *fn = dynCast<FnDecl>(decl.get()))
      checkFunction(*fn);

  scopes_.pop();
}

} // namespace

cir::Ty typeOfNode(const ast::TypeNode &node) {
  if (node.isArray())
    return cir::Ty::Ptr;
  if (node.name == "int")
    return cir::Ty::I32;
  if (node.name == "long")
    return cir::Ty::I64;
  if (node.name == "float")
    return cir::Ty::F64;
  if (node.name == "bool")
    return cir::Ty::I1;
  return cir::Ty::Void;
}

AnalysisResult analyse(ast::Program &program, std::string file) {
  AnalysisResult result;
  result.types = std::make_unique<TypeInfo>();
  Checker checker(std::move(file), *result.types);
  checker.run(program);
  result.diagnostics = checker.take();
  return result;
}

} // namespace mtir::sema
