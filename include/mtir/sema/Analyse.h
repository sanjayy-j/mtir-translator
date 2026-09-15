// Analyse.h -- the MiniLang semantic analyser.
//
// Module M2.  Detects the twelve diagnostic classes of
// docs/minilang-spec.md section 7:
//
//   E001 undeclared identifier            E007 wrong number of arguments
//   E002 duplicate declaration in a scope E008 argument type mismatch
//   E003 type mismatch in a binary op     E009 missing return on some path
//   E004 type mismatch in an assignment   E010 return with a value in void
//   E005 narrowing without an explicit cast  E011 break/continue outside a loop
//   E006 call to an undeclared function   E012 bad index
//
// It also does the one AST rewrite the specification asks for: section 4 says
// `int -> long` and `int -> float` "are inserted automatically by the type
// checker as explicit conversion nodes", so the analyser wraps those
// expressions in ast::Conv.  The CIR builder then lowers Conv and never has
// to know a widening rule.
//
// This is why `analyse` takes a mutable Program: it annotates and rewrites.
// Everything downstream sees the tree as const.
#ifndef MTIR_SEMA_ANALYSE_H
#define MTIR_SEMA_ANALYSE_H

#include <memory>

#include "mtir/ast/AST.h"
#include "mtir/sema/TypeInfo.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::sema {

struct AnalysisResult {
  std::unique_ptr<TypeInfo> types;
  support::Diagnostics diagnostics;

  bool ok() const { return !support::hasErrors(diagnostics); }
};

AnalysisResult analyse(ast::Program &program, std::string file = "<input>");

/// The CIR type a syntactic type denotes.  An array decays to `ptr`.
cir::Ty typeOfNode(const ast::TypeNode &node);

} // namespace mtir::sema

#endif // MTIR_SEMA_ANALYSE_H
