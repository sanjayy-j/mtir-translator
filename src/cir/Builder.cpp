#include "mtir/cir/Builder.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mtir/cir/Arith.h"
#include "mtir/cir/CFG.h"
#include "mtir/sema/Analyse.h"

namespace mtir::cir {
namespace {

using namespace mtir::ast;

/// Where a name lives while a function is being lowered.
struct Slot {
  enum class Kind { Register, Memory, Global } kind = Kind::Memory;
  Ty type = Ty::I32;                 ///< element type for an array
  std::optional<int> arrayLength;
  Reg reg{"", Ty::I32};              ///< Register: the incoming parameter
  Value address = ConstInt{0, Ty::I32}; ///< Memory/Global: where it lives
};

Value zeroOf(Ty ty) {
  if (ty == Ty::F64)
    return ConstFloat{0.0};
  return ConstInt{0, ty};
}

Opcode signedCompare(const std::string &op) {
  if (op == "==") return Opcode::ICmpEq;
  if (op == "!=") return Opcode::ICmpNe;
  if (op == "<") return Opcode::ICmpSlt;
  if (op == "<=") return Opcode::ICmpSle;
  if (op == ">") return Opcode::ICmpSgt;
  return Opcode::ICmpSge;
}

Opcode floatCompare(const std::string &op) {
  if (op == "==") return Opcode::FCmpOeq;
  if (op == "!=") return Opcode::FCmpOne;
  if (op == "<") return Opcode::FCmpOlt;
  if (op == "<=") return Opcode::FCmpOle;
  if (op == ">") return Opcode::FCmpOgt;
  return Opcode::FCmpOge;
}

Opcode integerBinop(const std::string &op) {
  if (op == "+") return Opcode::Add;
  if (op == "-") return Opcode::Sub;
  if (op == "*") return Opcode::Mul;
  if (op == "/") return Opcode::SDiv;
  if (op == "%") return Opcode::SRem;
  if (op == "&") return Opcode::And;
  if (op == "|") return Opcode::Or;
  if (op == "^") return Opcode::Xor;
  if (op == "<<") return Opcode::Shl;
  return Opcode::AShr;
}

Opcode floatBinop(const std::string &op) {
  if (op == "+") return Opcode::FAdd;
  if (op == "-") return Opcode::FSub;
  if (op == "*") return Opcode::FMul;
  return Opcode::FDiv;
}

bool isComparison(const std::string &op) {
  return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

// ==========================================================================
// One function
// ==========================================================================
class FunctionBuilder {
public:
  FunctionBuilder(const FnDecl &decl, const sema::TypeInfo &types,
                  const std::unordered_map<std::string, Slot> &globals,
                  support::Diagnostics &diags, std::string file)
      : decl_(decl), types_(types), diags_(diags), file_(std::move(file)) {
    scopes_.push_back(globals);
  }

  Function build();

private:
  // -- naming ------------------------------------------------------------
  Reg temp(Ty ty) {
    const std::string name = "t" + std::to_string(nextTemp_++);
    taken_.insert(name);
    return Reg{name, ty};
  }

  /// A register named after a source name, uniquified on collision --
  /// shadowing is legal in MiniLang but CIR register names are per function.
  std::string uniqueName(const std::string &base) {
    std::string name = base;
    int n = 1;
    while (taken_.count(name) != 0)
      name = base + "." + std::to_string(n++);
    taken_.insert(name);
    return name;
  }

  int nextLabel() { return labelCounter_++; }

  // -- blocks ------------------------------------------------------------
  void openBlock(std::string label);
  BasicBlock &ensureOpen();
  void emit(Instruction instr) { ensureOpen().add(std::move(instr)); }
  void terminate(Instruction instr) {
    ensureOpen().add(std::move(instr));
    open_ = false;
  }
  void br(const std::string &target) { terminate(Instruction::br(target)); }
  void brCond(Value cond, const std::string &t, const std::string &f) {
    terminate(Instruction::brCond(std::move(cond), t, f));
  }

  // -- scopes ------------------------------------------------------------
  void pushScope() { scopes_.emplace_back(); }
  void popScope() { scopes_.pop_back(); }
  void declare(const std::string &name, Slot slot) { scopes_.back()[name] = slot; }
  const Slot *lookup(const std::string &name) const;

  Reg addAlloca(const std::string &name, Ty elem, std::optional<int> count);

  // -- statements --------------------------------------------------------
  void lowerStmt(const Stmt &stmt);
  void lowerLet(const Let &stmt);
  void lowerAssign(const Assign &stmt);
  void lowerReturn(const Return &stmt);
  void lowerIf(const If &stmt);
  void lowerWhile(const While &stmt);
  void lowerFor(const For &stmt);
  void closeFunction();

  // -- expressions -------------------------------------------------------
  Value lowerExpr(const Expr &expr);
  Value lowerVarRef(const VarRef &expr);
  Value lowerUnary(const Unary &expr);
  Value lowerBinary(const Binary &expr);
  Value lowerShortCircuit(const Binary &expr);
  Value lowerCall(const Call &expr);
  Value lowerConv(const Conv &expr);
  /// The address an assignment writes through, and the type stored there.
  std::pair<Value, Ty> addressOf(const Expr &expr);

  void error(std::string message, support::SourceLoc loc) {
    support::Location l;
    l.file = file_;
    l.loc = loc;
    diags_.push_back(support::error("CIRGEN", std::move(message), l));
  }

  const FnDecl &decl_;
  const sema::TypeInfo &types_;
  support::Diagnostics &diags_;
  std::string file_;

  std::vector<BasicBlock> blocks_;
  bool open_ = false;
  std::size_t allocaInsertPoint_ = 0;
  int nextTemp_ = 0;
  int labelCounter_ = 0;
  std::unordered_set<std::string> taken_;
  std::vector<std::unordered_map<std::string, Slot>> scopes_;
  /// (continue target, break target) of the innermost enclosing loop.
  std::vector<std::pair<std::string, std::string>> loops_;
  Ty returnType_ = Ty::Void;
};

const Slot *FunctionBuilder::lookup(const std::string &name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    const auto found = it->find(name);
    if (found != it->end())
      return &found->second;
  }
  return nullptr;
}

void FunctionBuilder::openBlock(std::string label) {
  blocks_.emplace_back(std::move(label));
  open_ = true;
}

BasicBlock &FunctionBuilder::ensureOpen() {
  if (!open_) {
    // Emission continues after a return so the rest of the function still
    // lowers; the block that results has no predecessors and dead-code
    // elimination removes it.
    openBlock("dead." + std::to_string(nextLabel()));
  }
  return blocks_.back();
}

Reg FunctionBuilder::addAlloca(const std::string &name, Ty elem,
                               std::optional<int> count) {
  Reg reg{uniqueName(name), Ty::Ptr};
  Instruction instr = count.has_value()
                          ? Instruction::allocaArray(elem, reg, *count)
                          : Instruction::alloca_(elem, reg);
  // Allocas live at the top of the entry block, so the slot exists on every
  // path -- which is also where LLVM's mem2reg expects to find them.
  auto &entry = blocks_.front().instructions();
  entry.insert(entry.begin() + static_cast<std::ptrdiff_t>(allocaInsertPoint_),
               std::move(instr));
  ++allocaInsertPoint_;
  return reg;
}

// ==========================================================================
// Statements
// ==========================================================================
void FunctionBuilder::lowerStmt(const Stmt &stmt) {
  switch (stmt.kind()) {
  case NodeKind::Block: {
    pushScope();
    for (const StmtPtr &s : static_cast<const Block &>(stmt).statements())
      lowerStmt(*s);
    popScope();
    return;
  }
  case NodeKind::Let:
    lowerLet(static_cast<const Let &>(stmt));
    return;
  case NodeKind::Assign:
    lowerAssign(static_cast<const Assign &>(stmt));
    return;
  case NodeKind::ExprStmt:
    lowerExpr(static_cast<const ExprStmt &>(stmt).expr());
    return;
  case NodeKind::Return:
    lowerReturn(static_cast<const Return &>(stmt));
    return;
  case NodeKind::If:
    lowerIf(static_cast<const If &>(stmt));
    return;
  case NodeKind::While:
    lowerWhile(static_cast<const While &>(stmt));
    return;
  case NodeKind::For:
    lowerFor(static_cast<const For &>(stmt));
    return;
  case NodeKind::Break:
    if (!loops_.empty())
      br(loops_.back().second);
    return;
  case NodeKind::Continue:
    if (!loops_.empty())
      br(loops_.back().first);
    return;
  default:
    return;
  }
}

void FunctionBuilder::lowerLet(const Let &stmt) {
  const Ty elem = sema::typeOfNode(
      TypeNode{stmt.type().name, std::nullopt, stmt.type().loc});

  if (stmt.type().isArray()) {
    Slot slot;
    slot.kind = Slot::Kind::Memory;
    slot.type = elem;
    slot.arrayLength = stmt.type().arrayLength;
    slot.address = addAlloca(stmt.name(), elem, stmt.type().arrayLength);
    declare(stmt.name(), slot);
    return;
  }

  Slot slot;
  slot.kind = Slot::Kind::Memory;
  slot.type = elem;
  slot.address = addAlloca(stmt.name() + ".addr", elem, std::nullopt);
  declare(stmt.name(), slot);

  if (stmt.init() != nullptr)
    emit(Instruction::store(elem, lowerExpr(*stmt.init()), slot.address));
}

void FunctionBuilder::lowerAssign(const Assign &stmt) {
  const std::pair<Value, Ty> target = addressOf(stmt.target());
  Value value = lowerExpr(stmt.value());
  emit(Instruction::store(target.second, std::move(value), target.first));
}

void FunctionBuilder::lowerReturn(const Return &stmt) {
  if (stmt.value() == nullptr) {
    terminate(Instruction::ret());
    return;
  }
  terminate(Instruction::ret(returnType_, lowerExpr(*stmt.value())));
}

void FunctionBuilder::lowerIf(const If &stmt) {
  const int n = nextLabel();
  const std::string thenLabel = "if.then." + std::to_string(n);
  const std::string endLabel = "if.end." + std::to_string(n);
  const std::string elseLabel =
      stmt.elseBlock() != nullptr ? "if.else." + std::to_string(n) : endLabel;

  brCond(lowerExpr(stmt.cond()), thenLabel, elseLabel);

  bool reachesEnd = false;
  openBlock(thenLabel);
  lowerStmt(stmt.thenBlock());
  if (open_) {
    br(endLabel);
    reachesEnd = true;
  }

  if (stmt.elseBlock() != nullptr) {
    openBlock(elseLabel);
    lowerStmt(*stmt.elseBlock());
    if (open_) {
      br(endLabel);
      reachesEnd = true;
    }
  } else {
    reachesEnd = true; // the false edge of br.cond goes straight to end
  }

  // The join block is created lazily: if both arms return there is no edge
  // into it, which is why abs() is three blocks rather than four.
  if (reachesEnd)
    openBlock(endLabel);
  else
    open_ = false;
}

void FunctionBuilder::lowerWhile(const While &stmt) {
  const int n = nextLabel();
  const std::string head = "while.head." + std::to_string(n);
  const std::string body = "while.body." + std::to_string(n);
  const std::string end = "while.end." + std::to_string(n);

  br(head);
  openBlock(head);
  brCond(lowerExpr(stmt.cond()), body, end);

  openBlock(body);
  loops_.emplace_back(head, end); // continue -> head, break -> end
  lowerStmt(stmt.body());
  loops_.pop_back();
  if (open_)
    br(head);

  openBlock(end);
}

void FunctionBuilder::lowerFor(const For &stmt) {
  const int n = nextLabel();
  const std::string head = "for.head." + std::to_string(n);
  const std::string body = "for.body." + std::to_string(n);
  const std::string step = "for.step." + std::to_string(n);
  const std::string end = "for.end." + std::to_string(n);

  pushScope(); // the init clause scopes to the loop
  if (stmt.init() != nullptr)
    lowerStmt(*stmt.init());

  br(head);
  openBlock(head);
  if (stmt.cond() == nullptr)
    br(body); // for (;;) is an infinite loop
  else
    brCond(lowerExpr(*stmt.cond()), body, end);

  openBlock(body);
  loops_.emplace_back(step, end); // continue -> step, break -> end
  lowerStmt(stmt.body());
  loops_.pop_back();
  if (open_)
    br(step);

  openBlock(step);
  if (stmt.step() != nullptr)
    lowerStmt(*stmt.step());
  br(head);

  openBlock(end);
  popScope();
}

void FunctionBuilder::closeFunction() {
  if (!open_)
    return;
  if (returnType_ == Ty::Void) {
    terminate(Instruction::ret());
    return;
  }
  // Semantic analysis has already reported E009 if a path could reach here,
  // so this block is unreachable.  Terminating it with a zero invents no
  // semantics: control never arrives.
  terminate(Instruction::ret(returnType_, zeroOf(returnType_)));
}

// ==========================================================================
// Expressions
// ==========================================================================
std::pair<Value, Ty> FunctionBuilder::addressOf(const Expr &expr) {
  if (const auto *ref = dynCast<VarRef>(&expr)) {
    const Slot *slot = lookup(ref->name());
    if (slot == nullptr) {
      error("no storage for '" + ref->name() + "'", ref->loc());
      return {ConstInt{0, Ty::I32}, Ty::I32};
    }
    if (slot->kind == Slot::Kind::Register) {
      // build() only chooses Register for a parameter sema says is never
      // assigned, so reaching here means the two disagree.
      error("'" + ref->name() + "' is in a register and has no address",
            ref->loc());
      return {ConstInt{0, Ty::I32}, slot->type};
    }
    return {slot->address, slot->type};
  }

  if (const auto *index = dynCast<Index>(&expr)) {
    const auto *base = dynCast<VarRef>(&index->base());
    const Slot *slot = base != nullptr ? lookup(base->name()) : nullptr;
    if (slot == nullptr) {
      error("cannot index this expression", expr.loc());
      return {ConstInt{0, Ty::I32}, Ty::I32};
    }
    Value idx = lowerExpr(index->index());
    Reg ptr = temp(Ty::Ptr);
    emit(Instruction::gep(slot->type, ptr, slot->address, std::move(idx)));
    return {ptr, slot->type};
  }

  error("expression is not assignable", expr.loc());
  return {ConstInt{0, Ty::I32}, Ty::I32};
}

Value FunctionBuilder::lowerVarRef(const VarRef &expr) {
  const Slot *slot = lookup(expr.name());
  if (slot == nullptr) {
    error("undeclared identifier '" + expr.name() + "'", expr.loc());
    return ConstInt{0, Ty::I32};
  }
  if (slot->kind == Slot::Kind::Register)
    return slot->reg;
  if (slot->arrayLength.has_value())
    return slot->address; // an array name is its base address
  Reg dest = temp(slot->type);
  emit(Instruction::load(slot->type, dest, slot->address));
  return dest;
}

Value FunctionBuilder::lowerUnary(const Unary &expr) {
  const Ty ty = types_.typeOf(expr);

  if (expr.op() == "-") {
    // Fold negation of a literal, so -2147483648 is the single constant
    // INT_MIN rather than a subtraction from an unrepresentable positive.
    if (const auto *lit = dynCast<IntLit>(&expr.operand()))
      return ConstInt{wrapInt(-lit->value(), ty), ty};
    if (const auto *lit = dynCast<FloatLit>(&expr.operand()))
      return ConstFloat{-lit->value()};

    Value operand = lowerExpr(expr.operand());
    Reg dest = temp(ty);
    if (ty == Ty::F64) {
      emit(Instruction::unary(Opcode::Neg, ty, dest, std::move(operand)));
    } else {
      // CIR has 'neg', but integer negation is spelled as a subtraction from
      // zero: it is what docs/examples/abs.cir shows, and LLVM has no integer
      // negate instruction either.
      emit(Instruction::binary(Opcode::Sub, ty, dest, zeroOf(ty), std::move(operand)));
    }
    return dest;
  }

  Value operand = lowerExpr(expr.operand());
  Reg dest = temp(ty);
  emit(Instruction::unary(Opcode::Not, ty, dest, std::move(operand)));
  return dest;
}

Value FunctionBuilder::lowerShortCircuit(const Binary &expr) {
  // `&&` and `||` evaluate the right operand only when it can matter.  CIR is
  // not in SSA form, so the result is merged through a one-slot alloca rather
  // than a phi -- the same mechanism every mutable local uses.
  const int n = nextLabel();
  const bool isAnd = expr.op() == "&&";
  const std::string kind = isAnd ? "and" : "or";
  const std::string rhsLabel = kind + ".rhs." + std::to_string(n);
  const std::string endLabel = kind + ".end." + std::to_string(n);

  Reg slot = addAlloca(kind + "." + std::to_string(n), Ty::I1, std::nullopt);

  Value lhs = lowerExpr(expr.lhs());
  emit(Instruction::store(Ty::I1, lhs, slot));
  if (isAnd)
    brCond(lhs, rhsLabel, endLabel);
  else
    brCond(lhs, endLabel, rhsLabel);

  openBlock(rhsLabel);
  Value rhs = lowerExpr(expr.rhs());
  emit(Instruction::store(Ty::I1, std::move(rhs), slot));
  br(endLabel);

  openBlock(endLabel);
  Reg dest = temp(Ty::I1);
  emit(Instruction::load(Ty::I1, dest, slot));
  return dest;
}

Value FunctionBuilder::lowerBinary(const Binary &expr) {
  if (expr.op() == "&&" || expr.op() == "||")
    return lowerShortCircuit(expr);

  Value lhs = lowerExpr(expr.lhs());
  Value rhs = lowerExpr(expr.rhs());
  const Ty operandTy = typeOf(lhs);

  if (isComparison(expr.op())) {
    const Opcode op =
        operandTy == Ty::F64 ? floatCompare(expr.op()) : signedCompare(expr.op());
    Reg dest = temp(Ty::I1);
    emit(Instruction::compare(op, operandTy, dest, std::move(lhs), std::move(rhs)));
    return dest;
  }

  const Ty resultTy = types_.typeOf(expr);
  const Opcode op =
      resultTy == Ty::F64 ? floatBinop(expr.op()) : integerBinop(expr.op());
  Reg dest = temp(resultTy);
  emit(Instruction::binary(op, resultTy, dest, std::move(lhs), std::move(rhs)));
  return dest;
}

Value FunctionBuilder::lowerCall(const Call &expr) {
  const sema::Signature *sig = types_.functionSignature(expr.callee());
  if (sig == nullptr) {
    error("call to undeclared function '" + expr.callee() + "'", expr.loc());
    return ConstInt{0, Ty::I32};
  }

  if (sig->isBuiltin) {
    Value arg = lowerExpr(*expr.args()[0]);
    emit(Instruction::print(sig->parameters[0], std::move(arg)));
    return ConstInt{0, Ty::I32}; // void; the value is never consumed
  }

  std::vector<Value> args;
  args.reserve(expr.args().size());
  for (const ExprPtr &a : expr.args())
    args.push_back(lowerExpr(*a));

  if (sig->returnType == Ty::Void) {
    emit(Instruction::call(Ty::Void, std::nullopt, expr.callee(), std::move(args)));
    return ConstInt{0, Ty::I32};
  }
  Reg dest = temp(sig->returnType);
  emit(Instruction::call(sig->returnType, dest, expr.callee(), std::move(args)));
  return dest;
}

Value FunctionBuilder::lowerConv(const Conv &expr) {
  const Ty to = types_.typeOf(expr);
  const Ty from = types_.typeOf(expr.operand());

  // A literal adopts its context rather than costing an instruction, so
  // `let n: long = 1;` produces `1` typed i64 and not `sext i64 1`.
  if (const auto *lit = dynCast<IntLit>(&expr.operand())) {
    if (to == Ty::F64)
      return ConstFloat{static_cast<double>(lit->value())};
    return ConstInt{wrapInt(lit->value(), to), to};
  }

  Value operand = lowerExpr(expr.operand());
  Reg dest = temp(to);
  Opcode op = Opcode::SExt;
  if (to == Ty::F64)
    op = Opcode::SIToFP;
  else if (from == Ty::I1)
    op = Opcode::ZExt;
  emit(Instruction::convert(op, to, dest, std::move(operand)));
  return dest;
}

Value FunctionBuilder::lowerExpr(const Expr &expr) {
  switch (expr.kind()) {
  case NodeKind::IntLit:
    return ConstInt{static_cast<const IntLit &>(expr).value(), types_.typeOf(expr)};
  case NodeKind::FloatLit:
    return ConstFloat{static_cast<const FloatLit &>(expr).value()};
  case NodeKind::BoolLit:
    return ConstInt{static_cast<const BoolLit &>(expr).value() ? 1 : 0, Ty::I1};
  case NodeKind::VarRef:
    return lowerVarRef(static_cast<const VarRef &>(expr));
  case NodeKind::Unary:
    return lowerUnary(static_cast<const Unary &>(expr));
  case NodeKind::Binary:
    return lowerBinary(static_cast<const Binary &>(expr));
  case NodeKind::Call:
    return lowerCall(static_cast<const Call &>(expr));
  case NodeKind::Conv:
    return lowerConv(static_cast<const Conv &>(expr));
  case NodeKind::Index: {
    const auto &index = static_cast<const Index &>(expr);
    const std::pair<Value, Ty> address = addressOf(index);
    Reg dest = temp(address.second);
    emit(Instruction::load(address.second, dest, address.first));
    return dest;
  }
  default:
    error("cannot lower this expression", expr.loc());
    return ConstInt{0, Ty::I32};
  }
}

Function FunctionBuilder::build() {
  returnType_ = sema::typeOfNode(decl_.returnType());

  std::vector<Param> params;
  for (const ast::Param &p : decl_.params())
    params.push_back(Param{p.name, sema::typeOfNode(p.type)});

  openBlock("entry");
  allocaInsertPoint_ = 0;
  pushScope();

  for (const ast::Param &p : decl_.params()) {
    const Ty ty = sema::typeOfNode(p.type);
    taken_.insert(p.name);

    Slot slot;
    slot.type = p.type.isArray()
                    ? sema::typeOfNode(TypeNode{p.type.name, std::nullopt, p.type.loc})
                    : ty;
    slot.arrayLength = p.type.arrayLength;

    if (types_.isAssigned(p)) {
      // The parameter is written to, so it needs an address.
      slot.kind = Slot::Kind::Memory;
      slot.address = addAlloca(p.name + ".addr", ty, std::nullopt);
      emit(Instruction::store(ty, Reg{p.name, ty}, slot.address));
    } else {
      slot.kind = Slot::Kind::Register;
      slot.reg = Reg{p.name, ty};
    }
    declare(p.name, slot);
  }

  lowerStmt(decl_.body());
  closeFunction();
  popScope();

  Function fn(decl_.name(), std::move(params), returnType_);
  fn.setBlocks(std::move(blocks_));
  return fn;
}

} // namespace

BuildResult build(const ast::Program &program, const sema::TypeInfo &types,
                  std::string moduleName) {
  BuildResult result;
  Module module(moduleName);
  std::unordered_map<std::string, Slot> globals;

  for (const DeclPtr &decl : program.decls()) {
    const auto *g = dynCast<GlobalDecl>(decl.get());
    if (g == nullptr)
      continue;

    const Ty elem = sema::typeOfNode(
        TypeNode{g->type().name, std::nullopt, g->type().loc});

    Global global;
    global.name = g->name();
    global.ty = elem;
    global.arrayLen = g->type().arrayLength;

    if (g->init() != nullptr && !g->type().isArray()) {
      // Semantic analysis has already checked the type; only a literal can
      // appear here, because a global initialiser is not lowered in a
      // function body.
      if (const auto *intLit = dynCast<IntLit>(g->init()))
        global.init = elem == Ty::F64
                          ? Value{ConstFloat{static_cast<double>(intLit->value())}}
                          : Value{ConstInt{wrapInt(intLit->value(), elem), elem}};
      else if (const auto *floatLit = dynCast<FloatLit>(g->init()))
        global.init = Value{ConstFloat{floatLit->value()}};
      else if (const auto *boolLit = dynCast<BoolLit>(g->init()))
        global.init = Value{ConstInt{boolLit->value() ? 1 : 0, Ty::I1}};
      else {
        support::Location l;
        l.loc = g->loc();
        result.diagnostics.push_back(support::error(
            "CIRGEN", "a global initialiser must be a literal", l));
      }
    }
    module.addGlobal(global);

    Slot slot;
    slot.kind = Slot::Kind::Global;
    slot.type = elem;
    slot.arrayLength = g->type().arrayLength;
    slot.address = GlobalRef{g->name()};
    globals[g->name()] = slot;
  }

  for (const DeclPtr &decl : program.decls()) {
    const auto *fn = dynCast<FnDecl>(decl.get());
    if (fn == nullptr)
      continue;
    FunctionBuilder builder(*fn, types, globals, result.diagnostics, moduleName);
    module.addFunction(builder.build());
  }

  if (!support::hasErrors(result.diagnostics))
    result.module = std::move(module);
  return result;
}

} // namespace mtir::cir
