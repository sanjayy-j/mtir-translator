// VM.h -- reference stack machine for .sbc bytecode.
//
// Module M7.  A fetch-decode-execute loop over the bytecode EmitSbc produces.
//
// Why this matters more than a third code generator
// -------------------------------------------------
// The project's claim is that one CIR lowers to three targets with a known,
// documented set of semantic divergences (docs/divergence.md).  Two of those
// targets cannot be run here: llvm-as, lli, wat2wasm and wasmtime are all
// absent, so their output can only be inspected, never executed.  This VM is
// the one target that actually runs, which makes it the only place the
// divergence table is executable rather than asserted -- a trap on division
// by zero is either observed or it is not.
//
// Semantics implemented, per docs/divergence.md
// ---------------------------------------------
//   row 1  division or remainder by zero traps
//   row 2  INT_MIN / -1 and INT_MIN % -1 trap
//   row 3  a shift count is taken modulo the operand width
//   row 4  signed overflow wraps (computed in the unsigned domain, since
//          signed overflow is undefined in C++17 and C++20 alike)
//   row 5  an i1 is 0 or 1, never anything else
//   row 6  a float-to-int conversion traps on NaN and out of range
//   row 7  a load or store outside linear memory traps
//
// Output format
// -------------
// The MiniLang spec names print_int and print_float as the entire observable
// surface (section 8) but does not fix their formatting, so this VM defines
// it: the value, then a newline; a float is spelled the way CIR spells it, so
// that a printed value round-trips through the textual IR.  A host for the
// LLVM or WebAssembly targets has to match this for the differential harness
// to compare anything.
#ifndef MTIR_BACKEND_STACKVM_VM_H
#define MTIR_BACKEND_STACKVM_VM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mtir/backend/stackvm/EmitSbc.h"
#include "mtir/cir/Type.h"

namespace mtir::backend::stackvm {

/// One operand-stack slot.  Untagged: the bytecode is typed, so the opcode
/// says which half to read.  Keeping both rather than a union avoids any
/// type-punning question and costs nothing that matters in a reference VM.
struct Value {
  std::int64_t i = 0;
  double f = 0.0;

  static Value ofInt(std::int64_t v) {
    Value x;
    x.i = v;
    return x;
  }
  static Value ofFloat(double v) {
    Value x;
    x.f = v;
    return x;
  }
};

struct RunOptions {
  std::string entry = "main";
  std::vector<Value> args;

  /// Bytes of linear memory.  One WebAssembly page, so that a program which
  /// runs here would also fit the memory the .wat back end declares.
  std::size_t memoryBytes = 65536;

  /// A program that does not terminate would otherwise hang the test suite.
  /// Exceeding this is reported as a trap, not as a result.
  std::size_t stepLimit = 10000000;

  std::size_t callDepthLimit = 512;
};

struct RunResult {
  /// True when execution stopped on a trap: the result is then meaningless
  /// and `trap` says why.  A trap is a defined outcome, not a VM failure.
  bool trapped = false;
  std::string trap;

  Value result;
  cir::Ty resultType = cir::Ty::Void;

  /// Everything print_int and print_float wrote, in order.
  std::string output;

  std::size_t steps = 0;

  bool ok() const { return !trapped; }
};

RunResult run(const Program &program, const RunOptions &options = RunOptions{});

} // namespace mtir::backend::stackvm

#endif // MTIR_BACKEND_STACKVM_VM_H
