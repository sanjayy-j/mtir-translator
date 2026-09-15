// TestSupport.h -- helpers shared by the CIR test files.
#ifndef MTIR_TESTS_TESTSUPPORT_H
#define MTIR_TESTS_TESTSUPPORT_H

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "mtir/cir/Instruction.h"
#include "mtir/cir/Module.h"
#include "mtir/support/Diagnostic.h"

#ifndef MTIR_PROJECT_ROOT
#error "MTIR_PROJECT_ROOT must be defined by the build so tests can find docs/examples"
#endif

namespace mtir::test {

/// An absolute path to a file in the repository, e.g.
/// example("docs/examples/abs.cir").
inline std::string example(const std::string &relative) {
  return std::string(MTIR_PROJECT_ROOT) + "/" + relative;
}

/// Read a file, normalising CRLF to LF so a golden comparison does not depend
/// on how git checked the file out on Windows.
inline std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  std::string text = buffer.str();
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
      continue;
    out += text[i];
  }
  return out;
}

/// True when some diagnostic's message contains `needle`.
inline bool mentions(const support::Diagnostics &diags, const std::string &needle) {
  for (const support::Diagnostic &d : diags)
    if (d.message.find(needle) != std::string::npos)
      return true;
  return false;
}

inline bool hasCode(const support::Diagnostics &diags, const std::string &code) {
  for (const support::Diagnostic &d : diags)
    if (d.code == code)
      return true;
  return false;
}

/// The abs() module of Figure 2, built by hand.
///
/// This is the C++ counterpart of _demo_cir_module() in the Python driver,
/// and docs/examples/abs.cir is the golden text it must print as.  It is the
/// migration anchor for the whole CIR subsystem: the printer, the parser, the
/// verifier and the LLVM back end are all pinned against it.
inline cir::Module demoAbsModule() {
  using namespace mtir::cir;
  Module m("abs");

  const Reg x{"x", Ty::I32};
  const Reg t0{"t0", Ty::I1};
  const Reg t1{"t1", Ty::I32};

  Function fn("abs", {Param{"x", Ty::I32}}, Ty::I32);

  BasicBlock entry("entry");
  entry.add(Instruction::compare(Opcode::ICmpSlt, Ty::I32, t0, x, ConstInt{0, Ty::I32}));
  entry.add(Instruction::brCond(t0, "then", "exit"));

  BasicBlock then("then");
  then.add(Instruction::binary(Opcode::Sub, Ty::I32, t1, ConstInt{0, Ty::I32}, x));
  then.add(Instruction::ret(Ty::I32, t1));

  BasicBlock exit("exit");
  exit.add(Instruction::ret(Ty::I32, x));

  fn.setBlocks({entry, then, exit});
  m.addFunction(std::move(fn));
  return m;
}

} // namespace mtir::test

#endif // MTIR_TESTS_TESTSUPPORT_H
