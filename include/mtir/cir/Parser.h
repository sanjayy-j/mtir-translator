// Parser.h -- textual .cir -> Module.
//
// The round-trip property of docs/cir-spec.md section 4:
//
//     printModule(*parseCir(t).module) == t
//
// for every t the printer can produce.  This is what makes .cir a real
// interchange format rather than a debug dump, and therefore what lets each
// back end be developed and tested against checked-in .cir files with no
// front end involved at all.
//
// Recovering types.  The textual form carries one type per instruction and
// none on a register *use*, so the parser rebuilds every register's type from
// its definition: parameters are typed by the function header, and
// `%d = <op> <ty> ...` defines %d with resultType(op, ty) -- which is where
// comparisons become i1 and alloca/gep become ptr.  Definitions are collected
// over the whole function before uses are resolved, so a register defined in
// a loop body may legitimately be used by a block that appears earlier in the
// text.
//
// Known limit, recorded rather than worked around: an integer literal takes
// the type its instruction prints, so `call i32 @f(3)` types 3 as i32 even if
// @f takes an i64.  The printed text is identical either way, so the
// round-trip property still holds; the back ends recover argument types from
// the callee's signature, which they have.
//
// Implementation note: this is a hand-written tokenizer, not a regex port.
// The Python prototype used seven std::regex-shaped patterns; translating
// them would have been a mechanical port that gave worse errors and no column
// information.
#ifndef MTIR_CIR_PARSER_H
#define MTIR_CIR_PARSER_H

#include <optional>
#include <string>
#include <string_view>

#include "mtir/cir/Module.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::cir {

struct ParseResult {
  std::optional<Module> module;
  support::Diagnostics diagnostics;

  bool ok() const { return module.has_value(); }
};

/// Parse textual CIR.  `name` becomes the module name; `file` appears in
/// diagnostics.  Errors are accumulated, not thrown: several problems are
/// reported per run where that is possible.
ParseResult parseCir(std::string_view text, std::string name = "module",
                     std::string file = "<cir>");

} // namespace mtir::cir

#endif // MTIR_CIR_PARSER_H
