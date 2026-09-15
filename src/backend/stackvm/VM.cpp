#include "mtir/backend/stackvm/VM.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "mtir/backend/wasm/RegToStack.h" // sizeOf, so loads and stores use
                                          // the widths the frame was laid out with
#include "mtir/cir/Printer.h" // printDouble, so a printed float round-trips

namespace mtir::backend::stackvm {
namespace {

/// The decoded form of one bytecode instruction.
///
/// Decoding once, before execution, keeps the inner loop a switch over an
/// enum rather than a chain of string comparisons, without giving up the
/// readable mnemonics the .sbc listing needs.
enum class Op {
  Const,
  LocalGet,
  LocalSet,
  GlobalGet,
  GlobalSet,
  Load,
  Store,
  Add,
  Sub,
  Mul,
  DivS,
  DivU,
  RemS,
  RemU,
  And,
  Or,
  Xor,
  Shl,
  ShrS,
  ShrU,
  Neg,
  Eq,
  Ne,
  LtS,
  LeS,
  GtS,
  GeS,
  LtU,
  LeU,
  GtU,
  GeU,
  ExtendS,
  ExtendU,
  Wrap,
  ConvertS,
  TruncS,
  Nop,
  Jmp,
  Jz,
  Call,
  Return,
  Trap,
  Unknown
};

struct Decoded {
  Op op = Op::Unknown;
  cir::Ty ty = cir::Ty::I32;
  std::int64_t imm = 0;
  double fimm = 0.0;
  const std::string *text = nullptr;
  const std::string *mnemonic = nullptr;
};

Op suffixOp(const std::string &suffix) {
  if (suffix == "const") return Op::Const;
  if (suffix == "add") return Op::Add;
  if (suffix == "sub") return Op::Sub;
  if (suffix == "mul") return Op::Mul;
  if (suffix == "div_s") return Op::DivS;
  if (suffix == "div_u") return Op::DivU;
  if (suffix == "div") return Op::DivS; // f64.div; float division never traps
  if (suffix == "rem_s") return Op::RemS;
  if (suffix == "rem_u") return Op::RemU;
  if (suffix == "and") return Op::And;
  if (suffix == "or") return Op::Or;
  if (suffix == "xor") return Op::Xor;
  if (suffix == "shl") return Op::Shl;
  if (suffix == "shr_s") return Op::ShrS;
  if (suffix == "shr_u") return Op::ShrU;
  if (suffix == "neg") return Op::Neg;
  if (suffix == "eq") return Op::Eq;
  if (suffix == "ne") return Op::Ne;
  if (suffix == "lt_s" || suffix == "lt") return Op::LtS;
  if (suffix == "le_s" || suffix == "le") return Op::LeS;
  if (suffix == "gt_s" || suffix == "gt") return Op::GtS;
  if (suffix == "ge_s" || suffix == "ge") return Op::GeS;
  if (suffix == "lt_u") return Op::LtU;
  if (suffix == "le_u") return Op::LeU;
  if (suffix == "gt_u") return Op::GtU;
  if (suffix == "ge_u") return Op::GeU;
  if (suffix == "load") return Op::Load;
  if (suffix == "store") return Op::Store;
  if (suffix == "extend_i32_s") return Op::ExtendS;
  if (suffix == "extend_i32_u") return Op::ExtendU;
  if (suffix == "wrap_i64") return Op::Wrap;
  if (suffix == "convert_i32_s") return Op::ConvertS;
  if (suffix == "trunc_f64_s") return Op::TruncS;
  return Op::Unknown;
}

Decoded decode(const Instruction &instr) {
  Decoded d;
  d.imm = instr.imm;
  d.fimm = instr.fimm;
  d.text = &instr.text;
  d.mnemonic = &instr.mnemonic;

  const std::string &m = instr.mnemonic;
  if (m == "local.get") { d.op = Op::LocalGet; return d; }
  if (m == "local.set") { d.op = Op::LocalSet; return d; }
  if (m == "global.get") { d.op = Op::GlobalGet; return d; }
  if (m == "global.set") { d.op = Op::GlobalSet; return d; }
  if (m == "call") { d.op = Op::Call; return d; }
  if (m == "return") { d.op = Op::Return; return d; }
  if (m == "jmp") { d.op = Op::Jmp; return d; }
  if (m == "jz") { d.op = Op::Jz; return d; }
  if (m == "nop") { d.op = Op::Nop; return d; }
  if (m == "trap" || m == "unreachable") { d.op = Op::Trap; return d; }

  const std::size_t dot = m.find('.');
  if (dot == std::string::npos)
    return d;

  const std::string prefix = m.substr(0, dot);
  if (prefix == "i64")
    d.ty = cir::Ty::I64;
  else if (prefix == "f64")
    d.ty = cir::Ty::F64;
  else if (prefix != "i32")
    return d;

  d.op = suffixOp(m.substr(dot + 1));
  return d;
}

// -- arithmetic in the unsigned domain --------------------------------------
// Signed overflow is undefined in C++17 and C++20 alike, so every wrapping
// operation is done on the unsigned type of the same width and converted
// back.  docs/divergence.md row 4 requires wraparound, not a trap.

std::int64_t wrap32(std::uint32_t v) { return static_cast<std::int32_t>(v); }

std::uint32_t u32(std::int64_t v) { return static_cast<std::uint32_t>(v); }
std::uint64_t u64(std::int64_t v) { return static_cast<std::uint64_t>(v); }

bool is32(cir::Ty ty) { return ty != cir::Ty::I64; }

class Machine {
public:
  Machine(const Program &program, const RunOptions &options)
      : program_(program), options_(options), memory_(options.memoryBytes, 0),
        stackPointer_(static_cast<std::int64_t>(options.memoryBytes)) {}

  RunResult run();

private:
  /// Execute one function with `args` already bound; returns false on a trap.
  bool call(const Function &fn, std::vector<Value> args, Value &out);

  bool trap(std::string why) {
    if (!trapped_) {
      trapped_ = true;
      trapReason_ = std::move(why);
    }
    return false;
  }

  bool readMemory(std::int64_t address, std::size_t width, std::uint64_t &out);
  bool writeMemory(std::int64_t address, std::size_t width, std::uint64_t bits);

  const Program &program_;
  const RunOptions &options_;

  std::vector<unsigned char> memory_;
  std::int64_t stackPointer_;
  std::map<std::string, Value> globals_;

  std::string output_;
  std::size_t steps_ = 0;
  std::size_t depth_ = 0;
  bool trapped_ = false;
  std::string trapReason_;
};

bool Machine::readMemory(std::int64_t address, std::size_t width, std::uint64_t &out) {
  if (address < 0 ||
      static_cast<std::uint64_t>(address) + width > memory_.size())
    return trap("load outside linear memory at address " + std::to_string(address));
  out = 0;
  for (std::size_t i = 0; i < width; ++i)
    out |= static_cast<std::uint64_t>(memory_[static_cast<std::size_t>(address) + i])
           << (8 * i);
  return true;
}

bool Machine::writeMemory(std::int64_t address, std::size_t width, std::uint64_t bits) {
  if (address < 0 ||
      static_cast<std::uint64_t>(address) + width > memory_.size())
    return trap("store outside linear memory at address " + std::to_string(address));
  for (std::size_t i = 0; i < width; ++i)
    memory_[static_cast<std::size_t>(address) + i] =
        static_cast<unsigned char>((bits >> (8 * i)) & 0xFF);
  return true;
}

bool Machine::call(const Function &fn, std::vector<Value> args, Value &out) {
  if (++depth_ > options_.callDepthLimit) {
    --depth_;
    return trap("call depth limit of " + std::to_string(options_.callDepthLimit) +
                " exceeded");
  }

  std::vector<Value> locals(fn.locals.size());
  for (std::size_t i = 0; i < args.size() && i < locals.size(); ++i)
    locals[i] = args[i];

  // The frame is carved off the top of the shadow stack and released on the
  // way out, so recursion gets a fresh set of slots.  Slot 0 after the
  // parameters is $__frame when the function needs one; EmitSbc puts it
  // there.
  const std::int64_t savedStackPointer = stackPointer_;
  if (fn.frameSize > 0 && fn.paramTypes.size() < locals.size()) {
    stackPointer_ -= fn.frameSize;
    if (stackPointer_ < 0) {
      stackPointer_ = savedStackPointer;
      --depth_;
      return trap("stack overflow: no room for a frame in " + fn.name);
    }
    locals[fn.paramTypes.size()] = Value::ofInt(stackPointer_);
  }

  std::vector<Decoded> code;
  code.reserve(fn.code.size());
  for (const Instruction &i : fn.code)
    code.push_back(decode(i));

  std::vector<Value> stack;
  std::size_t pc = 0;
  bool returned = false;

  auto pop = [&stack]() -> Value {
    if (stack.empty())
      return Value{};
    const Value v = stack.back();
    stack.pop_back();
    return v;
  };

  while (pc < code.size() && !returned && !trapped_) {
    if (++steps_ > options_.stepLimit) {
      trap("step limit of " + std::to_string(options_.stepLimit) + " exceeded");
      break;
    }

    const Decoded &d = code[pc];
    ++pc;

    switch (d.op) {
    case Op::Const:
      stack.push_back(d.ty == cir::Ty::F64 ? Value::ofFloat(d.fimm)
                                           : Value::ofInt(d.imm));
      break;

    case Op::LocalGet:
      if (d.imm < 0 || static_cast<std::size_t>(d.imm) >= locals.size()) {
        trap("local slot " + std::to_string(d.imm) + " is out of range in " + fn.name);
        break;
      }
      stack.push_back(locals[static_cast<std::size_t>(d.imm)]);
      break;

    case Op::LocalSet:
      if (d.imm < 0 || static_cast<std::size_t>(d.imm) >= locals.size()) {
        trap("local slot " + std::to_string(d.imm) + " is out of range in " + fn.name);
        break;
      }
      locals[static_cast<std::size_t>(d.imm)] = pop();
      break;

    case Op::GlobalGet:
      stack.push_back(globals_[*d.text]);
      break;

    case Op::GlobalSet:
      globals_[*d.text] = pop();
      break;

    case Op::Load: {
      const Value address = pop();
      const std::size_t width = static_cast<std::size_t>(backend::wasm::sizeOf(d.ty));
      std::uint64_t bits = 0;
      if (!readMemory(address.i, width, bits))
        break;
      if (d.ty == cir::Ty::F64) {
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof value);
        stack.push_back(Value::ofFloat(value));
      } else if (d.ty == cir::Ty::I64) {
        stack.push_back(Value::ofInt(static_cast<std::int64_t>(bits)));
      } else {
        stack.push_back(Value::ofInt(wrap32(static_cast<std::uint32_t>(bits))));
      }
      break;
    }

    case Op::Store: {
      // The bytecode pushes the address first, then the value, so the value
      // is on top.
      const Value value = pop();
      const Value address = pop();
      const std::size_t width = static_cast<std::size_t>(backend::wasm::sizeOf(d.ty));
      std::uint64_t bits = 0;
      if (d.ty == cir::Ty::F64)
        std::memcpy(&bits, &value.f, sizeof bits);
      else
        bits = u64(value.i);
      writeMemory(address.i, width, bits);
      break;
    }

    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::And:
    case Op::Or:
    case Op::Xor:
    case Op::Shl:
    case Op::ShrS:
    case Op::ShrU:
    case Op::DivS:
    case Op::DivU:
    case Op::RemS:
    case Op::RemU: {
      const Value b = pop();
      const Value a = pop();

      if (d.ty == cir::Ty::F64) {
        double r = 0.0;
        switch (d.op) {
        case Op::Add: r = a.f + b.f; break;
        case Op::Sub: r = a.f - b.f; break;
        case Op::Mul: r = a.f * b.f; break;
        // IEEE division by zero is infinity, not a trap: row 1 is about
        // integer division only.
        case Op::DivS: r = a.f / b.f; break;
        default:
          trap("operation " + *d.mnemonic + " is not defined for f64");
          break;
        }
        stack.push_back(Value::ofFloat(r));
        break;
      }

      const bool narrow = is32(d.ty);
      const unsigned width = narrow ? 32u : 64u;

      // Division and remainder are the two that can trap (rows 1 and 2).
      if (d.op == Op::DivS || d.op == Op::DivU || d.op == Op::RemS ||
          d.op == Op::RemU) {
        const bool zero = narrow ? u32(b.i) == 0 : u64(b.i) == 0;
        if (zero) {
          trap("division by zero");
          break;
        }
        if (d.op == Op::DivS || d.op == Op::RemS) {
          const std::int64_t lhs = narrow ? wrap32(u32(a.i)) : a.i;
          const std::int64_t rhs = narrow ? wrap32(u32(b.i)) : b.i;
          const std::int64_t minimum =
              narrow ? std::numeric_limits<std::int32_t>::min()
                     : std::numeric_limits<std::int64_t>::min();
          if (lhs == minimum && rhs == -1) {
            // The quotient is not representable; CIR traps rather than
            // wrapping (docs/divergence.md row 2).
            trap("signed division overflow");
            break;
          }
          const std::int64_t r = d.op == Op::DivS ? lhs / rhs : lhs % rhs;
          stack.push_back(Value::ofInt(narrow ? wrap32(u32(r)) : r));
          break;
        }
        if (narrow) {
          const std::uint32_t r =
              d.op == Op::DivU ? u32(a.i) / u32(b.i) : u32(a.i) % u32(b.i);
          stack.push_back(Value::ofInt(wrap32(r)));
        } else {
          const std::uint64_t r =
              d.op == Op::DivU ? u64(a.i) / u64(b.i) : u64(a.i) % u64(b.i);
          stack.push_back(Value::ofInt(static_cast<std::int64_t>(r)));
        }
        break;
      }

      // Everything else wraps.  Shift counts are taken modulo the width
      // (row 3), which is also what WebAssembly and LLVM's masked shifts do.
      const std::uint64_t shift = (narrow ? u32(b.i) : u64(b.i)) % width;
      std::uint64_t r = 0;
      const std::uint64_t lhs = narrow ? u32(a.i) : u64(a.i);
      const std::uint64_t rhs = narrow ? u32(b.i) : u64(b.i);
      switch (d.op) {
      case Op::Add: r = lhs + rhs; break;
      case Op::Sub: r = lhs - rhs; break;
      case Op::Mul: r = lhs * rhs; break;
      case Op::And: r = lhs & rhs; break;
      case Op::Or: r = lhs | rhs; break;
      case Op::Xor: r = lhs ^ rhs; break;
      case Op::Shl: r = lhs << shift; break;
      case Op::ShrU: r = lhs >> shift; break;
      case Op::ShrS: {
        // An arithmetic shift is only well defined on the signed type, and
        // only after the count has been reduced.
        const std::int64_t signedLhs = narrow ? wrap32(u32(a.i)) : a.i;
        r = u64(signedLhs >> shift);
        break;
      }
      default: break;
      }
      stack.push_back(Value::ofInt(narrow ? wrap32(static_cast<std::uint32_t>(r))
                                          : static_cast<std::int64_t>(r)));
      break;
    }

    case Op::Neg: {
      const Value a = pop();
      stack.push_back(d.ty == cir::Ty::F64 ? Value::ofFloat(-a.f)
                                           : Value::ofInt(wrap32(0u - u32(a.i))));
      break;
    }

    case Op::Eq:
    case Op::Ne:
    case Op::LtS:
    case Op::LeS:
    case Op::GtS:
    case Op::GeS:
    case Op::LtU:
    case Op::LeU:
    case Op::GtU:
    case Op::GeU: {
      const Value b = pop();
      const Value a = pop();
      bool r = false;

      if (d.ty == cir::Ty::F64) {
        // Ordered comparisons: false whenever either operand is NaN, which is
        // what CIR's fcmp.o* family means.
        switch (d.op) {
        case Op::Eq: r = a.f == b.f; break;
        case Op::Ne:
          r = !std::isnan(a.f) && !std::isnan(b.f) && a.f != b.f;
          break;
        case Op::LtS: r = a.f < b.f; break;
        case Op::LeS: r = a.f <= b.f; break;
        case Op::GtS: r = a.f > b.f; break;
        case Op::GeS: r = a.f >= b.f; break;
        default: break;
        }
      } else {
        const bool narrow = is32(d.ty);
        const std::int64_t sa = narrow ? wrap32(u32(a.i)) : a.i;
        const std::int64_t sb = narrow ? wrap32(u32(b.i)) : b.i;
        const std::uint64_t ua = narrow ? u32(a.i) : u64(a.i);
        const std::uint64_t ub = narrow ? u32(b.i) : u64(b.i);
        switch (d.op) {
        case Op::Eq: r = sa == sb; break;
        case Op::Ne: r = sa != sb; break;
        case Op::LtS: r = sa < sb; break;
        case Op::LeS: r = sa <= sb; break;
        case Op::GtS: r = sa > sb; break;
        case Op::GeS: r = sa >= sb; break;
        case Op::LtU: r = ua < ub; break;
        case Op::LeU: r = ua <= ub; break;
        case Op::GtU: r = ua > ub; break;
        case Op::GeU: r = ua >= ub; break;
        default: break;
        }
      }
      // An i1 is 0 or 1 and nothing else (row 5).
      stack.push_back(Value::ofInt(r ? 1 : 0));
      break;
    }

    case Op::ExtendS:
      stack.push_back(Value::ofInt(wrap32(u32(pop().i))));
      break;

    case Op::ExtendU:
      stack.push_back(Value::ofInt(static_cast<std::int64_t>(u32(pop().i))));
      break;

    case Op::Wrap:
      stack.push_back(Value::ofInt(wrap32(u32(pop().i))));
      break;

    case Op::ConvertS:
      stack.push_back(Value::ofFloat(static_cast<double>(wrap32(u32(pop().i)))));
      break;

    case Op::TruncS: {
      // Row 6: NaN and out-of-range both trap.  The bounds are compared in
      // double, so the exclusive upper bound is exact for both widths and the
      // lower bound is exact for i64; for i32 the inclusive bound is used.
      const double x = pop().f;
      if (std::isnan(x)) {
        trap("float to int conversion of NaN");
        break;
      }
      if (d.ty == cir::Ty::I64) {
        const double lower = -9223372036854775808.0; // -(2^63), exact
        const double upper = 9223372036854775808.0;  //  2^63, exact
        if (!(x >= lower && x < upper)) {
          trap("float to int conversion out of range for i64");
          break;
        }
        stack.push_back(Value::ofInt(static_cast<std::int64_t>(x)));
      } else {
        if (!(x > -2147483649.0 && x < 2147483648.0)) {
          trap("float to int conversion out of range for i32");
          break;
        }
        stack.push_back(Value::ofInt(static_cast<std::int32_t>(x)));
      }
      break;
    }

    case Op::Nop:
      break;

    case Op::Jmp:
      if (d.imm < 0 || static_cast<std::size_t>(d.imm) > code.size()) {
        trap("jump to " + std::to_string(d.imm) + " is outside " + fn.name);
        break;
      }
      pc = static_cast<std::size_t>(d.imm);
      break;

    case Op::Jz: {
      const Value c = pop();
      if (c.i == 0) {
        if (d.imm < 0 || static_cast<std::size_t>(d.imm) > code.size()) {
          trap("jump to " + std::to_string(d.imm) + " is outside " + fn.name);
          break;
        }
        pc = static_cast<std::size_t>(d.imm);
      }
      break;
    }

    case Op::Call: {
      const std::string &callee = *d.text;

      if (callee == "$print_i32") {
        output_ += std::to_string(wrap32(u32(pop().i)));
        output_ += "\n";
        break;
      }
      if (callee == "$print_f64") {
        output_ += cir::printDouble(pop().f);
        output_ += "\n";
        break;
      }

      // Emitted as "$name"; the program indexes functions by bare name.
      const std::string name = callee.empty() || callee[0] != '$'
                                   ? callee
                                   : callee.substr(1);
      const Function *target = program_.function(name);
      if (target == nullptr) {
        trap("call to an unknown function: " + name);
        break;
      }

      // Arguments were pushed left to right, so the last one is on top.
      std::vector<Value> callArgs(target->paramTypes.size());
      for (std::size_t i = callArgs.size(); i > 0; --i)
        callArgs[i - 1] = pop();

      Value callResult;
      if (!call(*target, std::move(callArgs), callResult))
        break;
      if (target->returnType != cir::Ty::Void)
        stack.push_back(callResult);
      break;
    }

    case Op::Return:
      returned = true;
      break;

    case Op::Trap:
      trap("trap");
      break;

    case Op::Unknown:
    default:
      trap("unknown instruction: " + *d.mnemonic);
      break;
    }
  }

  stackPointer_ = savedStackPointer;
  --depth_;

  if (trapped_)
    return false;

  if (fn.returnType != cir::Ty::Void) {
    if (stack.empty())
      return trap("function " + fn.name + " ended without producing a result");
    out = stack.back();
  }
  return true;
}

RunResult Machine::run() {
  RunResult result;

  for (const cir::Global &g : program_.globals) {
    Value initial;
    if (g.init.has_value()) {
      if (const cir::ConstInt *ci = cir::asConstInt(*g.init))
        initial = Value::ofInt(ci->value);
      else if (const cir::ConstFloat *cf = cir::asConstFloat(*g.init))
        initial = Value::ofFloat(cf->value);
    }
    globals_["$" + g.name] = initial;
  }

  const Function *entry = program_.function(options_.entry);
  if (entry == nullptr) {
    result.trapped = true;
    result.trap = "no such function: " + options_.entry;
    return result;
  }

  Value out;
  const bool ok = call(*entry, options_.args, out);

  result.output = output_;
  result.steps = steps_;
  result.resultType = entry->returnType;
  if (!ok) {
    result.trapped = true;
    result.trap = trapReason_;
    return result;
  }
  result.result = out;
  return result;
}

} // namespace

RunResult run(const Program &program, const RunOptions &options) {
  Machine machine(program, options);
  return machine.run();
}

} // namespace mtir::backend::stackvm
