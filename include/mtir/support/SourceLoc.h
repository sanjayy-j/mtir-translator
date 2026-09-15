// SourceLoc.h -- positions in a source file.
//
// Support layer.  Deliberately depends on nothing else in the project so that
// AST, semantic analysis, CIR and every back end can all report positions
// without any of them depending on one another.
#ifndef MTIR_SUPPORT_SOURCELOC_H
#define MTIR_SUPPORT_SOURCELOC_H

namespace mtir::support {

/// A 1-based line/column position.  Both zero means "unknown", which is the
/// right answer for IR that was built rather than parsed.
struct SourceLoc {
  int line = 0;
  int col = 0;

  bool isValid() const { return line > 0; }
};

inline bool operator==(const SourceLoc &a, const SourceLoc &b) {
  return a.line == b.line && a.col == b.col;
}
inline bool operator!=(const SourceLoc &a, const SourceLoc &b) { return !(a == b); }

} // namespace mtir::support

#endif // MTIR_SUPPORT_SOURCELOC_H
