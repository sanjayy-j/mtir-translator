#include "mtir/cir/Instruction.h"

#include <utility>

namespace mtir::cir {

Instruction Instruction::binary(Opcode op, Ty ty, Reg dest, Value lhs, Value rhs) {
  Instruction i;
  i.op = op;
  i.ty = ty;
  i.dest = std::move(dest);
  i.args = {std::move(lhs), std::move(rhs)};
  return i;
}

Instruction Instruction::unary(Opcode op, Ty ty, Reg dest, Value operand) {
  Instruction i;
  i.op = op;
  i.ty = ty;
  i.dest = std::move(dest);
  i.args = {std::move(operand)};
  return i;
}

Instruction Instruction::compare(Opcode op, Ty operandTy, Reg dest, Value lhs,
                                 Value rhs) {
  // The printed type is the operand type; the destination is i1 (rule 7).
  return binary(op, operandTy, std::move(dest), std::move(lhs), std::move(rhs));
}

Instruction Instruction::convert(Opcode op, Ty resultTy, Reg dest, Value operand) {
  return unary(op, resultTy, std::move(dest), std::move(operand));
}

Instruction Instruction::alloca_(Ty elemTy, Reg dest) {
  Instruction i;
  i.op = Opcode::Alloca;
  i.ty = elemTy;
  i.dest = std::move(dest);
  return i;
}

Instruction Instruction::allocaArray(Ty elemTy, Reg dest, std::int64_t count) {
  Instruction i = alloca_(elemTy, std::move(dest));
  i.args = {ConstInt{count, Ty::I32}};
  return i;
}

Instruction Instruction::load(Ty ty, Reg dest, Value address) {
  return unary(Opcode::Load, ty, std::move(dest), std::move(address));
}

Instruction Instruction::store(Ty ty, Value value, Value address) {
  Instruction i;
  i.op = Opcode::Store;
  i.ty = ty;
  i.args = {std::move(value), std::move(address)};
  return i;
}

Instruction Instruction::gep(Ty elemTy, Reg dest, Value base, Value index) {
  Instruction i;
  i.op = Opcode::GEP;
  i.ty = elemTy;
  i.dest = std::move(dest);
  i.args = {std::move(base), std::move(index)};
  return i;
}

Instruction Instruction::call(Ty retTy, std::optional<Reg> dest, std::string callee,
                              std::vector<Value> args) {
  Instruction i;
  i.op = Opcode::Call;
  i.ty = retTy;
  i.dest = std::move(dest);
  i.callee = std::move(callee);
  i.args = std::move(args);
  return i;
}

Instruction Instruction::print(Ty ty, Value value) {
  Instruction i;
  i.op = ty == Ty::F64 ? Opcode::PrintF64 : Opcode::PrintI32;
  i.ty = ty;
  i.args = {std::move(value)};
  return i;
}

Instruction Instruction::trap() {
  Instruction i;
  i.op = Opcode::Trap;
  i.ty = Ty::Void;
  return i;
}

Instruction Instruction::br(std::string target) {
  Instruction i;
  i.op = Opcode::Br;
  i.ty = Ty::Void;
  i.labels = {std::move(target)};
  return i;
}

Instruction Instruction::brCond(Value cond, std::string ifTrue, std::string ifFalse) {
  Instruction i;
  i.op = Opcode::BrCond;
  i.ty = Ty::I1;
  i.args = {std::move(cond)};
  i.labels = {std::move(ifTrue), std::move(ifFalse)};
  return i;
}

Instruction Instruction::ret() {
  Instruction i;
  i.op = Opcode::Ret;
  i.ty = Ty::Void;
  return i;
}

Instruction Instruction::ret(Ty ty, Value value) {
  Instruction i;
  i.op = Opcode::Ret;
  i.ty = ty;
  i.args = {std::move(value)};
  return i;
}

bool operator==(const Instruction &a, const Instruction &b) {
  return a.op == b.op && a.ty == b.ty && a.dest == b.dest && a.args == b.args &&
         a.labels == b.labels && a.callee == b.callee;
}

} // namespace mtir::cir
