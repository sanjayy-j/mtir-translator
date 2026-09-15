// mtirc -- the command-line driver for the Multi-Target IR Translator.
//
// Stages that are not built yet exit with status 3 and name the module, its
// owner and the migration phase they are scheduled for, so the state of the
// project is readable from the tool itself rather than only from the plan.
// That convention is inherited from the Python driver and is deliberate: it
// is what stops an unfinished pipeline from looking finished.
//
// Exit codes
//   0  success
//   1  the input was rejected (parse error, verifier error, emitter error)
//   2  the input file could not be read
//   3  the requested stage is not implemented yet

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "mtir/backend/llvm/EmitLL.h"
#include "mtir/cir/Parser.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"
#include "mtir/opt/Pass.h"

namespace {

const char *kUsage = R"(mtirc -- Multi-Target Intermediate Representation Translator

usage: mtirc [options] <file>

  <file>            a .cir module (a .mini source needs the front end, which
                    is not migrated to C++ yet)

options:
  --emit=<stage>    cir | ll        (default: cir)
  --opt=<level>     0 | 1           (default: 0)
                    1 = constant folding, copy propagation, dead-code
                        elimination, run to a fixed point
  --verify          run the CIR verifier and report any diagnostics
  -o <file>         write to <file> instead of standard output
  --version         print the version and exit
  -h, --help        print this message and exit
)";

struct Options {
  std::string input;
  std::string output;
  std::string emit = "cir";
  int optLevel = 0;
  bool verify = false;
};

bool startsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Stages the Python prototype supports that the C++ migration has not
/// reached yet.  Naming the owner and the phase keeps the report honest.
int notYetImplemented(const std::string &stage) {
  struct Pending {
    const char *stage;
    const char *module;
    const char *owner;
    const char *phase;
  };
  static const Pending pending[] = {
      {"tokens", "lexer (M1a)", "Member 1", "migration phase C"},
      {"ast", "parser and AST (M1b)", "Member 1", "migration phase C"},
      {"wat", "WebAssembly back end (M6c)", "Member 4", "migration phase H"},
      {"sbc", "stack bytecode back end (M7)", "Member 4", "migration phase H"},
  };
  for (const Pending &p : pending) {
    if (stage == p.stage) {
      std::cerr << "error: --emit=" << stage << " is not implemented yet.\n"
                << "       " << p.module << " is owned by " << p.owner
                << " and is scheduled for " << p.phase << ".\n"
                << "       Implemented today: --emit=cir, --emit=ll.\n";
      return 3;
    }
  }
  std::cerr << "error: unknown stage '" << stage
            << "'. Known stages: cir, ll.\n";
  return 1;
}

int fail(const std::string &message) {
  std::cerr << "error: " << message << "\n";
  return 1;
}

} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
  // Without this, the CRT translates '\n' to "\r\n" on the way out and a
  // golden-file diff fails on Windows for a reason that has nothing to do
  // with the compiler.
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::cout << kUsage;
      return 0;
    }
    if (arg == "--version") {
      std::cout << "mtirc 0.1 (C++17)\n";
      return 0;
    }
    if (arg == "--verify") {
      options.verify = true;
    } else if (startsWith(arg, "--emit=")) {
      options.emit = arg.substr(7);
    } else if (startsWith(arg, "--opt=")) {
      options.optLevel = std::atoi(arg.c_str() + 6);
    } else if (arg == "-o" && i + 1 < argc) {
      options.output = argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "error: unknown option '" << arg << "'\n\n" << kUsage;
      return 1;
    } else if (options.input.empty()) {
      options.input = arg;
    } else {
      return fail("more than one input file given");
    }
  }

  if (options.emit != "cir" && options.emit != "ll")
    return notYetImplemented(options.emit);

  if (options.input.empty()) {
    std::cerr << kUsage;
    return 1;
  }

  std::ifstream in(options.input, std::ios::binary);
  if (!in) {
    std::cerr << "error: no such file: " << options.input << "\n";
    return 2;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string source = buffer.str();

  if (endsWith(options.input, ".mini")) {
    std::cerr << "error: MiniLang input is not supported yet.\n"
              << "       The C++ front end (M1) is migration phase C; until it\n"
              << "       lands, mtirc reads .cir modules.\n";
    return 3;
  }

  // The module takes its name from the file stem, so `mtirc --emit=ll
  // docs/examples/abs.cir` names the module "abs" -- which is what the golden
  // file records.
  std::string moduleName = options.input;
  const std::size_t slash = moduleName.find_last_of("/\\");
  if (slash != std::string::npos)
    moduleName = moduleName.substr(slash + 1);
  const std::size_t dot = moduleName.find_last_of('.');
  if (dot != std::string::npos && dot != 0)
    moduleName = moduleName.substr(0, dot);

  mtir::cir::ParseResult parsed =
      mtir::cir::parseCir(source, moduleName, options.input);
  if (!parsed.ok()) {
    std::cerr << mtir::support::format(parsed.diagnostics);
    return 1;
  }
  mtir::cir::Module module = std::move(*parsed.module);

  if (options.optLevel > 0) {
    mtir::opt::PassManager pm = mtir::opt::defaultPipeline(options.optLevel);
    pm.run(module);
  }

  if (options.verify) {
    const auto diags = mtir::cir::verify(module);
    if (!diags.empty()) {
      std::cerr << mtir::support::format(diags);
      if (mtir::support::hasErrors(diags))
        return 1;
    }
  }

  std::string text;
  if (options.emit == "cir") {
    text = mtir::cir::printModule(module);
  } else {
    mtir::backend::llvm::EmitResult result = mtir::backend::llvm::emitModule(module);
    if (!result.ok()) {
      std::cerr << mtir::support::format(result.diagnostics);
      return 1;
    }
    text = std::move(result.ir);
  }

  if (options.output.empty()) {
    std::cout << text;
    std::cout.flush();
    return 0;
  }

  std::ofstream out(options.output, std::ios::binary);
  if (!out)
    return fail("cannot write to " + options.output);
  out << text;
  return 0;
}
