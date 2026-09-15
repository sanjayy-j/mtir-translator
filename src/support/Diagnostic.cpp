#include "mtir/support/Diagnostic.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace mtir::support {
namespace {

const char *severityName(Severity s) {
  switch (s) {
  case Severity::Error:
    return "error";
  case Severity::Warning:
    return "warning";
  case Severity::Note:
    return "note";
  }
  return "error";
}

} // namespace

std::string Diagnostic::toString() const {
  std::ostringstream out;

  if (location.hasSource()) {
    if (!location.file.empty())
      out << location.file << ':';
    out << location.loc.line << ':' << location.loc.col << ": ";
  } else if (location.hasIR()) {
    out << '@' << location.function;
    if (!location.block.empty())
      out << ':' << location.block;
    if (location.instrIndex >= 0)
      out << '#' << location.instrIndex;
    out << ": ";
  }

  out << severityName(severity);
  if (!code.empty())
    out << '[' << code << ']';
  out << ": " << message;
  return out.str();
}

Diagnostic error(std::string code, std::string message, Location location) {
  Diagnostic d;
  d.severity = Severity::Error;
  d.code = std::move(code);
  d.message = std::move(message);
  d.location = std::move(location);
  return d;
}

bool hasErrors(const Diagnostics &diags) {
  return std::any_of(diags.begin(), diags.end(), [](const Diagnostic &d) {
    return d.severity == Severity::Error;
  });
}

std::string format(const Diagnostics &diags) {
  std::string out;
  for (const Diagnostic &d : diags) {
    out += d.toString();
    out += '\n';
  }
  return out;
}

} // namespace mtir::support
