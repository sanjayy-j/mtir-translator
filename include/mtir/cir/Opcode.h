// Opcode.h -- the CIR instruction set as a strong enum plus one metadata table.
//
// The Python prototype kept opcode knowledge in five places that could drift
// apart: ARITH_OPS, BIT_OPS, CMP_OPS, SIDE_EFFECTING and result_ty().  Here
// there is one table, indexed by the enum, and a static assertion that entry
// i describes opcode i.  Adding an opcode without describing it does not
// compile.
//
// The instruction set is docs/cir-spec.md section 3: 51 mnemonics, or 37
// opcodes counting icmp.* and fcmp.* as one family each.
#ifndef MTIR_CIR_OPCODE_H
#define MTIR_CIR_OPCODE_H

#include <cstdint>
#include <optional>
#include <string_view>

#include "mtir/cir/Type.h"

namespace mtir::cir {

enum class Opcode : std::uint8_t {
  // Arithmetic
  Add, Sub, Mul, SDiv, UDiv, SRem, URem,
  FAdd, FSub, FMul, FDiv, Neg,
  // Bitwise and shift
  And, Or, Xor, Not, Shl, AShr, LShr,
  // Integer comparison
  ICmpEq, ICmpNe, ICmpSlt, ICmpSle, ICmpSgt, ICmpSge,
  ICmpUlt, ICmpUle, ICmpUgt, ICmpUge,
  // Float comparison
  FCmpOeq, FCmpOne, FCmpOlt, FCmpOle, FCmpOgt, FCmpOge,
  // Conversion
  SExt, ZExt, Trunc, SIToFP, FPToSI,
  // Memory
  Alloca, Load, Store, GEP,
  // Call
  Call,
  // Intrinsics
  PrintI32, PrintF64, Trap,
  // Terminators
  Br, BrCond, Ret,

  Count
};

enum class OpCategory : std::uint8_t {
  Arith, Bitwise, Compare, Convert, Memory, Call, Intrinsic, Terminator
};

/// How the register an instruction defines is typed, given the single type
/// the textual form prints.  docs/cir-spec.md section 4.
enum class ResultRule : std::uint8_t {
  None,       ///< defines nothing
  AsPrinted,  ///< the printed type is the result type
  AlwaysI1,   ///< comparisons, per well-formedness rule 7
  AlwaysPtr   ///< alloca and gep print the *element* type
};

/// Operand count that varies with the instruction: ret (0 or 1), alloca
/// (0 or 1), call (any).
inline constexpr std::uint8_t kVariadic = 255;

struct OpInfo {
  Opcode op;
  std::string_view mnemonic;   ///< "icmp.slt"
  std::string_view predicate;  ///< "slt" for comparisons, empty otherwise
  std::uint8_t arity;
  OpCategory category;
  bool isTerminator;
  bool hasSideEffect;          ///< executing it is observable on its own
  ResultRule resultRule;
};

const OpInfo &info(Opcode op);

std::string_view mnemonic(Opcode op);

std::optional<Opcode> parseOpcode(std::string_view text);

inline bool isTerminator(Opcode op) { return info(op).isTerminator; }
inline bool isCompare(Opcode op) { return info(op).category == OpCategory::Compare; }
inline bool hasSideEffect(Opcode op) { return info(op).hasSideEffect; }

/// The type of the register this opcode defines, given its printed type.
Ty resultType(Opcode op, Ty printed);

} // namespace mtir::cir

#endif // MTIR_CIR_OPCODE_H
