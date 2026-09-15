// Printer.h -- Module -> textual .cir.
//
// The output must stay byte-identical to docs/examples/abs.cir, which is
// Figure 2 of the Review 1 report and the golden file for this subsystem.
//
// Determinism is a hard requirement: no unordered container may be iterated
// into the output, because std::unordered_map gives no order guarantee (the
// Python prototype's determinism partly rested on dict insertion order, which
// C++ does not provide). Everything printed here walks a std::vector.
#ifndef MTIR_CIR_PRINTER_H
#define MTIR_CIR_PRINTER_H

#include <string>

#include "mtir/cir/Module.h"

namespace mtir::cir {

std::string printModule(const Module &module);
std::string printFunction(const Function &fn);
std::string printBlock(const BasicBlock &block);
std::string printInstruction(const Instruction &instr);
std::string printValue(const Value &value);
std::string printGlobal(const Global &g);

/// A double in the shortest form that reads back as the same value.
/// Shared with the LLVM back end so the two never disagree about a literal.
std::string printDouble(double value);

} // namespace mtir::cir

#endif // MTIR_CIR_PRINTER_H
