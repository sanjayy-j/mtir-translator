// AST.h -- the MiniLang abstract syntax tree.
//
// Module M1b.  The node set is docs/minilang-spec.md section 2 plus `Conv`,
// which section 4 requires: "int -> long and int -> float are inserted
// automatically by the type checker as explicit conversion nodes".  The
// parser never produces a Conv; semantic analysis inserts them.
//
// Shape: a tagged hierarchy.  Every node carries a NodeKind so a visitor can
// dispatch without RTTI, and children are owned by unique_ptr.  The CIR
// builder only ever reads the tree, so every accessor it needs is const;
// semantic analysis is the one pass that rewrites, and it does so by
// replacing a child unique_ptr (that is how Conv gets inserted).
//
// See docs/frontend-cir-contract.md for the interface the CIR builder relies
// on.
#ifndef MTIR_AST_AST_H
#define MTIR_AST_AST_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mtir/support/SourceLoc.h"

namespace mtir::ast {

enum class NodeKind : std::uint8_t {
  // expressions
  IntLit, FloatLit, BoolLit, VarRef, Unary, Binary, Call, Index, Conv,
  // statements
  Block, Let, Assign, ExprStmt, If, While, For, Break, Continue, Return,
  // declarations
  FnDecl, GlobalDecl,
};

// --------------------------------------------------------------------------
// Types, as written in the source
// --------------------------------------------------------------------------
/// A syntactic type: `int`, `float`, `int[8]`.  Semantic analysis maps this
/// onto a cir::Ty; the parser never infers anything.
struct TypeNode {
  std::string name;                  ///< int | long | float | bool | void
  std::optional<int> arrayLength;    ///< set for `int[8]`
  support::SourceLoc loc;

  bool isArray() const { return arrayLength.has_value(); }
  std::string toString() const;
};

// --------------------------------------------------------------------------
// Base
// --------------------------------------------------------------------------
class Node {
public:
  virtual ~Node() = default;
  NodeKind kind() const { return kind_; }
  support::SourceLoc loc() const { return loc_; }

protected:
  Node(NodeKind kind, support::SourceLoc loc) : kind_(kind), loc_(loc) {}

private:
  NodeKind kind_;
  support::SourceLoc loc_;
};

class Expr : public Node {
protected:
  using Node::Node;
};

class Stmt : public Node {
protected:
  using Node::Node;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// --------------------------------------------------------------------------
// Expressions
// --------------------------------------------------------------------------
class IntLit final : public Expr {
public:
  IntLit(std::int64_t value, support::SourceLoc loc)
      : Expr(NodeKind::IntLit, loc), value_(value) {}
  std::int64_t value() const { return value_; }

private:
  std::int64_t value_;
};

class FloatLit final : public Expr {
public:
  FloatLit(double value, support::SourceLoc loc)
      : Expr(NodeKind::FloatLit, loc), value_(value) {}
  double value() const { return value_; }

private:
  double value_;
};

class BoolLit final : public Expr {
public:
  BoolLit(bool value, support::SourceLoc loc)
      : Expr(NodeKind::BoolLit, loc), value_(value) {}
  bool value() const { return value_; }

private:
  bool value_;
};

class VarRef final : public Expr {
public:
  VarRef(std::string name, support::SourceLoc loc)
      : Expr(NodeKind::VarRef, loc), name_(std::move(name)) {}
  const std::string &name() const { return name_; }

private:
  std::string name_;
};

class Unary final : public Expr {
public:
  Unary(std::string op, ExprPtr operand, support::SourceLoc loc)
      : Expr(NodeKind::Unary, loc), op_(std::move(op)), operand_(std::move(operand)) {}
  const std::string &op() const { return op_; }   ///< "-", "!", "~"
  const Expr &operand() const { return *operand_; }
  ExprPtr &operandSlot() { return operand_; }

private:
  std::string op_;
  ExprPtr operand_;
};

class Binary final : public Expr {
public:
  Binary(std::string op, ExprPtr lhs, ExprPtr rhs, support::SourceLoc loc)
      : Expr(NodeKind::Binary, loc), op_(std::move(op)), lhs_(std::move(lhs)),
        rhs_(std::move(rhs)) {}
  const std::string &op() const { return op_; }
  const Expr &lhs() const { return *lhs_; }
  const Expr &rhs() const { return *rhs_; }
  ExprPtr &lhsSlot() { return lhs_; }
  ExprPtr &rhsSlot() { return rhs_; }

private:
  std::string op_;
  ExprPtr lhs_;
  ExprPtr rhs_;
};

class Call final : public Expr {
public:
  Call(std::string callee, std::vector<ExprPtr> args, support::SourceLoc loc)
      : Expr(NodeKind::Call, loc), callee_(std::move(callee)), args_(std::move(args)) {}
  const std::string &callee() const { return callee_; }
  const std::vector<ExprPtr> &args() const { return args_; }
  std::vector<ExprPtr> &argSlots() { return args_; }

private:
  std::string callee_;
  std::vector<ExprPtr> args_;
};

class Index final : public Expr {
public:
  Index(ExprPtr base, ExprPtr index, support::SourceLoc loc)
      : Expr(NodeKind::Index, loc), base_(std::move(base)), index_(std::move(index)) {}
  const Expr &base() const { return *base_; }
  const Expr &index() const { return *index_; }
  ExprPtr &indexSlot() { return index_; }

private:
  ExprPtr base_;
  ExprPtr index_;
};

/// An explicit widening inserted by semantic analysis.  The parser never
/// builds one.  Its source type is typeOf(operand()) and its destination type
/// is typeOf(*this), both read from sema::TypeInfo -- storing them here would
/// create a second source of truth that could drift.
class Conv final : public Expr {
public:
  Conv(ExprPtr operand, support::SourceLoc loc)
      : Expr(NodeKind::Conv, loc), operand_(std::move(operand)) {}
  const Expr &operand() const { return *operand_; }

private:
  ExprPtr operand_;
};

// --------------------------------------------------------------------------
// Statements
// --------------------------------------------------------------------------
class Block final : public Stmt {
public:
  Block(std::vector<StmtPtr> stmts, support::SourceLoc loc)
      : Stmt(NodeKind::Block, loc), stmts_(std::move(stmts)) {}
  const std::vector<StmtPtr> &statements() const { return stmts_; }
  std::vector<StmtPtr> &statementSlots() { return stmts_; }

private:
  std::vector<StmtPtr> stmts_;
};

class Let final : public Stmt {
public:
  Let(std::string name, TypeNode type, ExprPtr init, support::SourceLoc loc)
      : Stmt(NodeKind::Let, loc), name_(std::move(name)), type_(std::move(type)),
        init_(std::move(init)) {}
  const std::string &name() const { return name_; }
  const TypeNode &type() const { return type_; }
  const Expr *init() const { return init_.get(); }
  ExprPtr &initSlot() { return init_; }

private:
  std::string name_;
  TypeNode type_;
  ExprPtr init_;
};

class Assign final : public Stmt {
public:
  Assign(ExprPtr target, ExprPtr value, support::SourceLoc loc)
      : Stmt(NodeKind::Assign, loc), target_(std::move(target)),
        value_(std::move(value)) {}
  const Expr &target() const { return *target_; }
  const Expr &value() const { return *value_; }
  ExprPtr &targetSlot() { return target_; }
  ExprPtr &valueSlot() { return value_; }

private:
  ExprPtr target_;
  ExprPtr value_;
};

class ExprStmt final : public Stmt {
public:
  ExprStmt(ExprPtr expr, support::SourceLoc loc)
      : Stmt(NodeKind::ExprStmt, loc), expr_(std::move(expr)) {}
  const Expr &expr() const { return *expr_; }
  ExprPtr &exprSlot() { return expr_; }

private:
  ExprPtr expr_;
};

class If final : public Stmt {
public:
  If(ExprPtr cond, StmtPtr thenBlock, StmtPtr elseBlock, support::SourceLoc loc)
      : Stmt(NodeKind::If, loc), cond_(std::move(cond)),
        then_(std::move(thenBlock)), else_(std::move(elseBlock)) {}
  const Expr &cond() const { return *cond_; }
  const Stmt &thenBlock() const { return *then_; }
  const Stmt *elseBlock() const { return else_.get(); }
  ExprPtr &condSlot() { return cond_; }
  StmtPtr &thenSlot() { return then_; }
  StmtPtr &elseSlot() { return else_; }

private:
  ExprPtr cond_;
  StmtPtr then_;
  StmtPtr else_;
};

class While final : public Stmt {
public:
  While(ExprPtr cond, StmtPtr body, support::SourceLoc loc)
      : Stmt(NodeKind::While, loc), cond_(std::move(cond)), body_(std::move(body)) {}
  const Expr &cond() const { return *cond_; }
  const Stmt &body() const { return *body_; }
  ExprPtr &condSlot() { return cond_; }
  StmtPtr &bodySlot() { return body_; }

private:
  ExprPtr cond_;
  StmtPtr body_;
};

class For final : public Stmt {
public:
  For(StmtPtr init, ExprPtr cond, StmtPtr step, StmtPtr body, support::SourceLoc loc)
      : Stmt(NodeKind::For, loc), init_(std::move(init)), cond_(std::move(cond)),
        step_(std::move(step)), body_(std::move(body)) {}
  const Stmt *init() const { return init_.get(); }
  const Expr *cond() const { return cond_.get(); }
  const Stmt *step() const { return step_.get(); }
  const Stmt &body() const { return *body_; }
  StmtPtr &initSlot() { return init_; }
  ExprPtr &condSlot() { return cond_; }
  StmtPtr &stepSlot() { return step_; }
  StmtPtr &bodySlot() { return body_; }

private:
  StmtPtr init_;
  ExprPtr cond_;
  StmtPtr step_;
  StmtPtr body_;
};

class Break final : public Stmt {
public:
  explicit Break(support::SourceLoc loc) : Stmt(NodeKind::Break, loc) {}
};

class Continue final : public Stmt {
public:
  explicit Continue(support::SourceLoc loc) : Stmt(NodeKind::Continue, loc) {}
};

class Return final : public Stmt {
public:
  Return(ExprPtr value, support::SourceLoc loc)
      : Stmt(NodeKind::Return, loc), value_(std::move(value)) {}
  const Expr *value() const { return value_.get(); }
  ExprPtr &valueSlot() { return value_; }

private:
  ExprPtr value_;
};

// --------------------------------------------------------------------------
// Declarations
// --------------------------------------------------------------------------
struct Param {
  std::string name;
  TypeNode type;
  support::SourceLoc loc;
};

class Decl : public Node {
protected:
  using Node::Node;
};

using DeclPtr = std::unique_ptr<Decl>;

class FnDecl final : public Decl {
public:
  FnDecl(std::string name, std::vector<Param> params, TypeNode returnType,
         std::unique_ptr<Block> body, support::SourceLoc loc)
      : Decl(NodeKind::FnDecl, loc), name_(std::move(name)),
        params_(std::move(params)), returnType_(std::move(returnType)),
        body_(std::move(body)) {}

  const std::string &name() const { return name_; }
  const std::vector<Param> &params() const { return params_; }
  const TypeNode &returnType() const { return returnType_; }
  const Block &body() const { return *body_; }
  Block &bodyRef() { return *body_; }

private:
  std::string name_;
  std::vector<Param> params_;
  TypeNode returnType_;
  std::unique_ptr<Block> body_;
};

class GlobalDecl final : public Decl {
public:
  GlobalDecl(std::string name, TypeNode type, ExprPtr init, support::SourceLoc loc)
      : Decl(NodeKind::GlobalDecl, loc), name_(std::move(name)),
        type_(std::move(type)), init_(std::move(init)) {}

  const std::string &name() const { return name_; }
  const TypeNode &type() const { return type_; }
  const Expr *init() const { return init_.get(); }
  ExprPtr &initSlot() { return init_; }

private:
  std::string name_;
  TypeNode type_;
  ExprPtr init_;
};

class Program {
public:
  const std::vector<DeclPtr> &decls() const { return decls_; }
  std::vector<DeclPtr> &declSlots() { return decls_; }
  void add(DeclPtr decl) { decls_.push_back(std::move(decl)); }

private:
  std::vector<DeclPtr> decls_;
};

// --------------------------------------------------------------------------
// Casting helpers
// --------------------------------------------------------------------------
template <typename T> const T *dynCast(const Node *node);
template <typename T> T *dynCast(Node *node);

#define MTIR_AST_CAST(Type)                                                    \
  template <> inline const Type *dynCast<Type>(const Node *node) {             \
    return node != nullptr && node->kind() == NodeKind::Type                   \
               ? static_cast<const Type *>(node)                               \
               : nullptr;                                                      \
  }                                                                            \
  template <> inline Type *dynCast<Type>(Node * node) {                        \
    return node != nullptr && node->kind() == NodeKind::Type                   \
               ? static_cast<Type *>(node)                                     \
               : nullptr;                                                      \
  }

MTIR_AST_CAST(IntLit)
MTIR_AST_CAST(FloatLit)
MTIR_AST_CAST(BoolLit)
MTIR_AST_CAST(VarRef)
MTIR_AST_CAST(Unary)
MTIR_AST_CAST(Binary)
MTIR_AST_CAST(Call)
MTIR_AST_CAST(Index)
MTIR_AST_CAST(Conv)
MTIR_AST_CAST(Block)
MTIR_AST_CAST(Let)
MTIR_AST_CAST(Assign)
MTIR_AST_CAST(ExprStmt)
MTIR_AST_CAST(If)
MTIR_AST_CAST(While)
MTIR_AST_CAST(For)
MTIR_AST_CAST(Break)
MTIR_AST_CAST(Continue)
MTIR_AST_CAST(Return)
MTIR_AST_CAST(FnDecl)
MTIR_AST_CAST(GlobalDecl)

#undef MTIR_AST_CAST

/// An indented tree rendering, for `mtirc --emit=ast`.
std::string dump(const Program &program);

} // namespace mtir::ast

#endif // MTIR_AST_AST_H
