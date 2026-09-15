#include "mtir/cir/Opcode.h"

#include <array>
#include <cstddef>

namespace mtir::cir {
namespace {

constexpr std::size_t kCount = static_cast<std::size_t>(Opcode::Count);

// Shorthands so the table below stays readable at a glance.
constexpr OpCategory kA = OpCategory::Arith;
constexpr OpCategory kB = OpCategory::Bitwise;
constexpr OpCategory kC = OpCategory::Compare;
constexpr OpCategory kV = OpCategory::Convert;
constexpr OpCategory kM = OpCategory::Memory;
constexpr OpCategory kL = OpCategory::Call;
constexpr OpCategory kI = OpCategory::Intrinsic;
constexpr OpCategory kT = OpCategory::Terminator;

constexpr ResultRule kNone = ResultRule::None;
constexpr ResultRule kAs = ResultRule::AsPrinted;
constexpr ResultRule kI1 = ResultRule::AlwaysI1;
constexpr ResultRule kPtr = ResultRule::AlwaysPtr;

// clang-format off
constexpr std::array<OpInfo, kCount> kTable{{
  //  opcode             mnemonic       pred   arity      cat  term   side   result
  { Opcode::Add,       "add",         "",        2,  kA, false, false, kAs  },
  { Opcode::Sub,       "sub",         "",        2,  kA, false, false, kAs  },
  { Opcode::Mul,       "mul",         "",        2,  kA, false, false, kAs  },
  { Opcode::SDiv,      "sdiv",        "",        2,  kA, false, false, kAs  },
  { Opcode::UDiv,      "udiv",        "",        2,  kA, false, false, kAs  },
  { Opcode::SRem,      "srem",        "",        2,  kA, false, false, kAs  },
  { Opcode::URem,      "urem",        "",        2,  kA, false, false, kAs  },
  { Opcode::FAdd,      "fadd",        "",        2,  kA, false, false, kAs  },
  { Opcode::FSub,      "fsub",        "",        2,  kA, false, false, kAs  },
  { Opcode::FMul,      "fmul",        "",        2,  kA, false, false, kAs  },
  { Opcode::FDiv,      "fdiv",        "",        2,  kA, false, false, kAs  },
  { Opcode::Neg,       "neg",         "",        1,  kA, false, false, kAs  },

  { Opcode::And,       "and",         "",        2,  kB, false, false, kAs  },
  { Opcode::Or,        "or",          "",        2,  kB, false, false, kAs  },
  { Opcode::Xor,       "xor",         "",        2,  kB, false, false, kAs  },
  { Opcode::Not,       "not",         "",        1,  kB, false, false, kAs  },
  { Opcode::Shl,       "shl",         "",        2,  kB, false, false, kAs  },
  { Opcode::AShr,      "ashr",        "",        2,  kB, false, false, kAs  },
  { Opcode::LShr,      "lshr",        "",        2,  kB, false, false, kAs  },

  { Opcode::ICmpEq,    "icmp.eq",     "eq",      2,  kC, false, false, kI1  },
  { Opcode::ICmpNe,    "icmp.ne",     "ne",      2,  kC, false, false, kI1  },
  { Opcode::ICmpSlt,   "icmp.slt",    "slt",     2,  kC, false, false, kI1  },
  { Opcode::ICmpSle,   "icmp.sle",    "sle",     2,  kC, false, false, kI1  },
  { Opcode::ICmpSgt,   "icmp.sgt",    "sgt",     2,  kC, false, false, kI1  },
  { Opcode::ICmpSge,   "icmp.sge",    "sge",     2,  kC, false, false, kI1  },
  { Opcode::ICmpUlt,   "icmp.ult",    "ult",     2,  kC, false, false, kI1  },
  { Opcode::ICmpUle,   "icmp.ule",    "ule",     2,  kC, false, false, kI1  },
  { Opcode::ICmpUgt,   "icmp.ugt",    "ugt",     2,  kC, false, false, kI1  },
  { Opcode::ICmpUge,   "icmp.uge",    "uge",     2,  kC, false, false, kI1  },

  { Opcode::FCmpOeq,   "fcmp.oeq",    "oeq",     2,  kC, false, false, kI1  },
  { Opcode::FCmpOne,   "fcmp.one",    "one",     2,  kC, false, false, kI1  },
  { Opcode::FCmpOlt,   "fcmp.olt",    "olt",     2,  kC, false, false, kI1  },
  { Opcode::FCmpOle,   "fcmp.ole",    "ole",     2,  kC, false, false, kI1  },
  { Opcode::FCmpOgt,   "fcmp.ogt",    "ogt",     2,  kC, false, false, kI1  },
  { Opcode::FCmpOge,   "fcmp.oge",    "oge",     2,  kC, false, false, kI1  },

  { Opcode::SExt,      "sext",        "",        1,  kV, false, false, kAs  },
  { Opcode::ZExt,      "zext",        "",        1,  kV, false, false, kAs  },
  { Opcode::Trunc,     "trunc",       "",        1,  kV, false, false, kAs  },
  { Opcode::SIToFP,    "sitofp",      "",        1,  kV, false, false, kAs  },
  { Opcode::FPToSI,    "fptosi",      "",        1,  kV, false, false, kAs  },

  { Opcode::Alloca,    "alloca",      "", kVariadic, kM, false, false, kPtr },
  { Opcode::Load,      "load",        "",        1,  kM, false, false, kAs  },
  { Opcode::Store,     "store",       "",        2,  kM, false, true,  kNone},
  { Opcode::GEP,       "gep",         "",        2,  kM, false, false, kPtr },

  { Opcode::Call,      "call",        "", kVariadic, kL, false, true,  kAs  },

  { Opcode::PrintI32,  "print.i32",   "",        1,  kI, false, true,  kNone},
  { Opcode::PrintF64,  "print.f64",   "",        1,  kI, false, true,  kNone},
  { Opcode::Trap,      "trap",        "",        0,  kI, false, true,  kNone},

  { Opcode::Br,        "br",          "",        0,  kT, true,  false, kNone},
  { Opcode::BrCond,    "br.cond",     "",        1,  kT, true,  false, kNone},
  { Opcode::Ret,       "ret",         "", kVariadic, kT, true,  false, kNone},
}};
// clang-format on

// Entry i must describe opcode i, or every lookup below is wrong.
constexpr bool tableIsOrdered() {
  for (std::size_t i = 0; i < kCount; ++i)
    if (static_cast<std::size_t>(kTable[i].op) != i)
      return false;
  return true;
}
static_assert(tableIsOrdered(), "kTable must be indexed by Opcode");

} // namespace

const OpInfo &info(Opcode op) { return kTable[static_cast<std::size_t>(op)]; }

std::string_view mnemonic(Opcode op) { return info(op).mnemonic; }

std::optional<Opcode> parseOpcode(std::string_view text) {
  for (const OpInfo &e : kTable)
    if (e.mnemonic == text)
      return e.op;
  return std::nullopt;
}

Ty resultType(Opcode op, Ty printed) {
  switch (info(op).resultRule) {
  case ResultRule::None:
    return Ty::Void;
  case ResultRule::AlwaysI1:
    return Ty::I1;
  case ResultRule::AlwaysPtr:
    return Ty::Ptr;
  case ResultRule::AsPrinted:
    break;
  }
  return printed;
}

} // namespace mtir::cir
