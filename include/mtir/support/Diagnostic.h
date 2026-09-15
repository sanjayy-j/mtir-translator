// Diagnostic.h -- one diagnostic representation shared by every phase.
//
// The Python prototype reported CIR problems as bare strings and source
// problems through src/sema/diagnostics.py.  Those are unified here so that a
// caller can collect errors from the parser, the verifier and a back end into
// one list and print them the same way.
//
// This header lives in support/ rather than sema/ on purpose: the CIR core
// must be able to report diagnostics without depending on semantic analysis.
#ifndef MTIR_SUPPORT_DIAGNOSTIC_H
#define MTIR_SUPPORT_DIAGNOSTIC_H

#include <string>
#include <vector>

#include "mtir/support/SourceLoc.h"

namespace mtir::support {

enum class Severity { Error, Warning, Note };

/// Where a diagnostic points.  A diagnostic carries *either* a source
/// position (file/loc, for the front end) *or* an IR position
/// (function/block/instrIndex, for CIR-level checks) -- and occasionally
/// both, when the builder knows which statement produced an instruction.
struct Location {
  std::string file;
  SourceLoc loc;

  std::string function;
  std::string block;
  int instrIndex = -1;

  bool hasSource() const { return loc.isValid(); }
  bool hasIR() const { return !function.empty(); }
};

struct Diagnostic {
  Severity severity = Severity::Error;
  std::string code;    ///< "E001" for source diagnostics, "CIR01" for IR ones.
  std::string message;
  Location location;

  /// "file:line:col: error[E001]: message", or the IR equivalent
  /// "@fn:block: error[CIR03]: message".  Matches the format that
  /// docs/minilang-spec.md section 7 fixes for the front end.
  std::string toString() const;
};

using Diagnostics = std::vector<Diagnostic>;

Diagnostic error(std::string code, std::string message, Location location = {});

bool hasErrors(const Diagnostics &diags);

/// One diagnostic per line, in order.  Used by the CLI and by test failures.
std::string format(const Diagnostics &diags);

} // namespace mtir::support

#endif // MTIR_SUPPORT_DIAGNOSTIC_H
