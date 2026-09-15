// TypeInfo.h -- what semantic analysis tells the CIR builder.
//
// Module M2.  The interface is docs/frontend-cir-contract.md section 4, kept
// deliberately small: four accessors, no symbol table to walk, no constant
// evaluator, no coercion helpers.  By the time the builder runs, conversions
// are ast::Conv nodes and every expression has a type, so there is nothing
// left to derive.
//
// The Python prototype had no semantic analysis at all -- its CIR builder
// re-derived types itself, which is the separation-of-concerns problem this
// layer exists to fix.  None of that logic was ported.
#ifndef MTIR_SEMA_TYPEINFO_H
#define MTIR_SEMA_TYPEINFO_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mtir/ast/AST.h"
#include "mtir/cir/Type.h"

namespace mtir::sema {

enum class SymbolKind { Parameter, Local, Global, Function };

struct Symbol {
  SymbolKind kind = SymbolKind::Local;
  std::string name;
  cir::Ty type = cir::Ty::I32;       ///< element type for an array
  std::optional<int> arrayLength;    ///< set iff declared as T[n]

  bool isArray() const { return arrayLength.has_value(); }
};

struct Signature {
  cir::Ty returnType = cir::Ty::Void;
  std::vector<cir::Ty> parameters;
  bool isBuiltin = false;            ///< print_int / print_float
};

/// Read-only results of semantic analysis.  Every lookup here is total for a
/// program the checker accepted: an unresolved name is E001 and never reaches
/// the builder.
class TypeInfo {
public:
  cir::Ty typeOf(const ast::Expr &expr) const;
  const Symbol *resolve(const ast::VarRef &ref) const;
  const Symbol *resolveTarget(const ast::Expr &target) const;
  const Signature *signatureOf(const ast::Call &call) const;

  /// Whether this parameter is ever the target of an assignment in its
  /// function body.  A parameter that is never assigned has no address, so
  /// the builder keeps it in its incoming register instead of an alloca --
  /// which is why abs() lowers to the shape of docs/examples/abs.cir.
  bool isAssigned(const ast::Param &param) const;

  /// The declared signature of a named function, for the builder's calls.
  const Signature *functionSignature(const std::string &name) const;

  // -- population, used only by the checker ------------------------------
  void setType(const ast::Expr &expr, cir::Ty ty);
  void setSymbol(const ast::Expr &ref, Symbol symbol);
  void setSignature(const ast::Call &call, Signature signature);
  void setFunction(const std::string &name, Signature signature);
  void markAssigned(const ast::Param &param);

private:
  std::unordered_map<const ast::Expr *, cir::Ty> types_;
  std::unordered_map<const ast::Expr *, Symbol> symbols_;
  std::unordered_map<const ast::Call *, Signature> callSignatures_;
  std::unordered_map<std::string, Signature> functions_;
  std::unordered_set<const ast::Param *> assignedParams_;
};

} // namespace mtir::sema

#endif // MTIR_SEMA_TYPEINFO_H
