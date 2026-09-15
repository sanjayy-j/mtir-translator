#include "mtir/cir/Printer.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace mtir::cir {
namespace {

std::string tyText(Ty t) { return std::string(toString(t)); }

std::string joinArgs(const std::vector<Value> &args) {
  std::string out;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0)
      out += ", ";
    out += printValue(args[i]);
  }
  return out;
}

} // namespace

std::string printDouble(double value) {
  if (std::isnan(value))
    return "nan";
  if (std::isinf(value))
    return value < 0 ? "-inf" : "inf";

  // Shortest round-tripping decimal.  std::to_chars would be the modern
  // spelling, but libstdc++ only gained the floating-point overloads in
  // GCC 11, which is newer than several of the team's machines -- so this
  // uses the portable "try increasing precision until it reads back" form.
  char buffer[64];
  for (int precision = 15; precision <= 17; ++precision) {
    std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    if (std::strtod(buffer, nullptr) == value)
      break;
  }

  std::string text(buffer);
  // Keep a decimal point so the result is recognisably a float on the way
  // back in, and so LLVM's lexer -- which only treats a number as floating
  // point once it has seen a '.' -- accepts the same spelling.
  if (text.find('.') == std::string::npos) {
    const std::size_t e = text.find_first_of("eE");
    if (e == std::string::npos)
      text += ".0";
    else
      text.insert(e, ".0");
  }
  return text;
}

std::string printValue(const Value &value) {
  struct Visitor {
    std::string operator()(const Reg &r) const { return "%" + r.name; }
    std::string operator()(const ConstInt &c) const {
      return std::to_string(c.value);
    }
    std::string operator()(const ConstFloat &c) const { return printDouble(c.value); }
    std::string operator()(const GlobalRef &g) const { return "@" + g.name; }
  };
  return std::visit(Visitor{}, value);
}

std::string printInstruction(const Instruction &instr) {
  const std::string args = joinArgs(instr.args);
  const std::string dest = instr.dest.has_value() ? "%" + instr.dest->name : std::string();

  switch (instr.op) {
  case Opcode::Br:
    return "br " + (instr.labels.empty() ? std::string("<missing>") : instr.labels[0]);

  case Opcode::BrCond:
    // Both terminators are spelled `br`; the `?` is what distinguishes them.
    return "br " + args + " ? " + (instr.labels.size() > 0 ? instr.labels[0] : "<missing>") +
           " : " + (instr.labels.size() > 1 ? instr.labels[1] : "<missing>");

  case Opcode::Ret:
    return instr.args.empty() ? "ret void" : "ret " + tyText(instr.ty) + " " + args;

  case Opcode::Store:
    return "store " + tyText(instr.ty) + " " + args;

  case Opcode::Call: {
    const std::string call = "call " + tyText(instr.ty) + " @" + instr.callee + "(" + args + ")";
    return dest.empty() ? call : dest + " = " + call;
  }

  case Opcode::Alloca: {
    const std::string count = instr.args.empty() ? std::string() : ", " + printValue(instr.args[0]);
    return dest + " = alloca " + tyText(instr.ty) + count;
  }

  case Opcode::PrintI32:
  case Opcode::PrintF64:
    return std::string(mnemonic(instr.op)) + " " + args;

  case Opcode::Trap:
    return "trap";

  default:
    break;
  }

  const std::string body = args.empty()
                               ? std::string(mnemonic(instr.op)) + " " + tyText(instr.ty)
                               : std::string(mnemonic(instr.op)) + " " + tyText(instr.ty) + " " + args;
  return dest.empty() ? body : dest + " = " + body;
}

std::string printBlock(const BasicBlock &block) {
  std::string out = block.label() + ":";
  for (const Instruction &instr : block.instructions()) {
    out += "\n  ";
    out += printInstruction(instr);
  }
  return out;
}

std::string printFunction(const Function &fn) {
  std::string params;
  for (std::size_t i = 0; i < fn.params().size(); ++i) {
    if (i != 0)
      params += ", ";
    params += tyText(fn.params()[i].ty) + " %" + fn.params()[i].name;
  }

  std::string out = "func @" + fn.name() + "(" + params + ") -> " +
                    tyText(fn.returnType()) + " {";
  for (std::size_t i = 0; i < fn.blocks().size(); ++i) {
    out += "\n";
    out += printBlock(fn.blocks()[i]);
  }
  out += "\n}";
  return out;
}

std::string printGlobal(const Global &g) {
  std::string ty = tyText(g.ty);
  if (g.arrayLen.has_value())
    ty += "[" + std::to_string(*g.arrayLen) + "]";
  std::string init;
  if (g.init.has_value())
    init = " = " + printValue(*g.init);
  return "global @" + g.name + " : " + ty + init;
}

std::string printModule(const Module &module) {
  std::vector<std::string> parts;
  for (const Global &g : module.globals())
    parts.push_back(printGlobal(g));
  if (!module.globals().empty())
    parts.emplace_back();
  for (const Function &fn : module.functions())
    parts.push_back(printFunction(fn));

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0)
      out += "\n";
    out += parts[i];
  }
  out += "\n";
  return out;
}

} // namespace mtir::cir
