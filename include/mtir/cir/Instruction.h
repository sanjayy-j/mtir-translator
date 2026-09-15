// Instruction.h -- one CIR instruction.
//
// A value type held directly in a std::vector: no instruction in CIR is
// polymorphic and no operand points into a def-use graph, so the intrusive
// lists and unique_ptr ownership that LLVM needs would buy nothing here.
//
// Note on `ty`: the textual form carries one type per instruction, and for
// two families that is not the result type.  See resultType().
#ifndef MTIR_CIR_INSTRUCTION_H
#define MTIR_CIR_INSTRUCTION_H

#include <optional>
#include <string>
#include <vector>

#include "mtir/cir/Opcode.h"
#include "mtir/cir/Value.h"

namespace mtir::cir {

struct Instruction {
  Opcode op = Opcode::Trap;

  /// The type the instruction *prints*: the operand type for a comparison,
  /// the element type for alloca/gep, the result type otherwise.
  Ty ty = Ty::Void;

  /// Empty for instructions that produce no value: store, br, ret, print.*,
  /// trap, and a call to a void function.
  std::optional<Reg> dest;

  std::vector<Value> args;

  /// Branch targets.  One for `br`, two for `br.cond`, none otherwise.
  std::vector<std::string> labels;

  /// Callee name for `call`; empty otherwise.
  std::string callee;

  bool isTerminator() const { return cir::isTerminator(op); }
  bool hasSideEffect() const { return cir::hasSideEffect(op); }
  Ty resultType() const { return cir::resultType(op, ty); }

  // -- factories ---------------------------------------------------------
  // These exist because hand-writing an aggregate initialiser for every
  // instruction makes tests and the builder unreadable.
  static Instruction binary(Opcode op, Ty ty, Reg dest, Value lhs, Value rhs);
  static Instruction unary(Opcode op, Ty ty, Reg dest, Value operand);
  static Instruction compare(Opcode op, Ty operandTy, Reg dest, Value lhs, Value rhs);
  static Instruction convert(Opcode op, Ty resultTy, Reg dest, Value operand);
  static Instruction alloca_(Ty elemTy, Reg dest);
  static Instruction allocaArray(Ty elemTy, Reg dest, std::int64_t count);
  static Instruction load(Ty ty, Reg dest, Value address);
  static Instruction store(Ty ty, Value value, Value address);
  static Instruction gep(Ty elemTy, Reg dest, Value base, Value index);
  static Instruction call(Ty retTy, std::optional<Reg> dest, std::string callee,
                          std::vector<Value> args);
  static Instruction print(Ty ty, Value value);
  static Instruction trap();
  static Instruction br(std::string target);
  static Instruction brCond(Value cond, std::string ifTrue, std::string ifFalse);
  static Instruction ret();
  static Instruction ret(Ty ty, Value value);
};

bool operator==(const Instruction &a, const Instruction &b);
inline bool operator!=(const Instruction &a, const Instruction &b) { return !(a == b); }

} // namespace mtir::cir

#endif // MTIR_CIR_INSTRUCTION_H
