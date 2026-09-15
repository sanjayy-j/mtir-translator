#include "mtir/sema/TypeInfo.h"

namespace mtir::sema {

cir::Ty TypeInfo::typeOf(const ast::Expr &expr) const {
  const auto it = types_.find(&expr);
  return it == types_.end() ? cir::Ty::Void : it->second;
}

const Symbol *TypeInfo::resolve(const ast::VarRef &ref) const {
  const auto it = symbols_.find(static_cast<const ast::Expr *>(&ref));
  return it == symbols_.end() ? nullptr : &it->second;
}

const Symbol *TypeInfo::resolveTarget(const ast::Expr &target) const {
  const auto it = symbols_.find(&target);
  return it == symbols_.end() ? nullptr : &it->second;
}

const Signature *TypeInfo::signatureOf(const ast::Call &call) const {
  const auto it = callSignatures_.find(&call);
  return it == callSignatures_.end() ? nullptr : &it->second;
}

bool TypeInfo::isAssigned(const ast::Param &param) const {
  return assignedParams_.count(&param) != 0;
}

const Signature *TypeInfo::functionSignature(const std::string &name) const {
  const auto it = functions_.find(name);
  return it == functions_.end() ? nullptr : &it->second;
}

void TypeInfo::setType(const ast::Expr &expr, cir::Ty ty) { types_[&expr] = ty; }

void TypeInfo::setSymbol(const ast::Expr &ref, Symbol symbol) {
  symbols_[&ref] = std::move(symbol);
}

void TypeInfo::setSignature(const ast::Call &call, Signature signature) {
  callSignatures_[&call] = std::move(signature);
}

void TypeInfo::setFunction(const std::string &name, Signature signature) {
  functions_[name] = std::move(signature);
}

void TypeInfo::markAssigned(const ast::Param &param) { assignedParams_.insert(&param); }

} // namespace mtir::sema
