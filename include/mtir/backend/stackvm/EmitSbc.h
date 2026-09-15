// EmitSbc.h -- CIR -> flat stack bytecode.
//
// Module M7.  The third target, and the cheap one: it reuses the whole
// register-to-stack pass (M6a) that the WebAssembly back end uses, and
// differs only in how control flow and frames are expressed.
//
// Why a third target at all
// -------------------------
// LLVM accepts a branch to any label, so the LLVM back end is close to a
// printer over the CIR CFG.  WebAssembly accepts no such branch and needs an
// arbitrary CFG re-expressed as nested regions -- the dispatch tower in
// EmitWat.h.  This back end sits at the other extreme: labels stay flat and a
// branch is an absolute index into the instruction stream, exactly as CIR
// means it.  Nothing has to be structured.
//
// The difference in emitted instruction count between this back end and the
// WebAssembly one is therefore a direct measurement of what structuring
// costs, which is the comparison the project exists to make.
//
// Frames
// ------
// A MiniLang variable whose address is taken needs a memory slot rather than
// a local, and before optimisation that is every variable.  The WebAssembly
// back end carves those slots out of linear memory behind a shadow stack
// pointer it has to maintain itself.  Here the VM owns the call stack, so a
// function simply declares its frame size and the VM allocates and releases
// it -- the same FrameLayout, a simpler mechanism.
#ifndef MTIR_BACKEND_STACKVM_EMITSBC_H
#define MTIR_BACKEND_STACKVM_EMITSBC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mtir/cir/Module.h"
#include "mtir/cir/Type.h"
#include "mtir/support/Diagnostic.h"

namespace mtir::backend::stackvm {

/// One bytecode instruction.
///
/// The mnemonic is the one the register-to-stack pass already produces
/// ("i32.add", "local.get", "call"), so the two stack back ends share a
/// vocabulary and the counts are comparable.  Only the branches are new.
struct Instruction {
  std::string mnemonic;

  /// Local name, global name, or callee -- whatever the mnemonic names.
  /// Kept even when `slot` carries the resolved index, because the printed
  /// bytecode is meant to be read.
  std::string text;

  /// Immediate: an integer constant, an absolute branch target, a frame
  /// offset, or a resolved local slot.
  std::int64_t imm = 0;

  double fimm = 0.0;

  bool hasImm = false;
};

bool operator==(const Instruction &a, const Instruction &b);
inline bool operator!=(const Instruction &a, const Instruction &b) { return !(a == b); }

struct Function {
  std::string name;
  std::vector<cir::Ty> paramTypes;
  cir::Ty returnType = cir::Ty::Void;

  /// Local slots, parameters first and in order, so a call can bind
  /// arguments by position.
  std::vector<std::string> locals;

  /// Bytes of frame this function needs; zero when it takes no addresses.
  std::int64_t frameSize = 0;

  std::vector<Instruction> code;
};

struct Program {
  std::string moduleName;
  std::vector<cir::Global> globals;
  std::vector<Function> functions;

  const Function *function(const std::string &name) const;
};

struct EmitResult {
  Program program;
  support::Diagnostics diagnostics;

  bool ok() const { return !support::hasErrors(diagnostics); }
};

EmitResult emitModule(const cir::Module &module);

/// The textual .sbc form: a header, the globals, then one block per function
/// with instructions numbered so that an absolute branch target can be read
/// off the listing.
std::string printProgram(const Program &program);

/// Total instruction count, for the structuring-cost comparison against the
/// WebAssembly back end.
std::size_t instructionCount(const Program &program);

} // namespace mtir::backend::stackvm

#endif // MTIR_BACKEND_STACKVM_EMITSBC_H
