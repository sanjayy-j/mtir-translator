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

#include "mtir/ast/AST.h"
#include "mtir/backend/llvm/EmitLL.h"
#include "mtir/backend/stackvm/EmitSbc.h"
#include "mtir/backend/stackvm/VM.h"
#include "mtir/backend/wasm/EmitWat.h"
#include "mtir/cir/Builder.h"
#include "mtir/cir/Parser.h"
#include "mtir/frontend/Lexer.h"
#include "mtir/frontend/Parser.h"
#include "mtir/sema/Analyse.h"
#include "mtir/cir/Printer.h"
#include "mtir/cir/Verifier.h"
#include "mtir/opt/Pass.h"

namespace {

const char *kUsage = R"(mtirc -- Multi-Target Intermediate Representation Translator

usage: mtirc [options] <file>

  <file>            a .mini source or a .cir module

options:
  --emit=<stage>    tokens | ast | cir | ll | wat | sbc   (default: cir)
  --opt=<level>     0 | 1           (default: 0)
                    1 = constant folding, copy propagation, dead-code
                        elimination, run to a fixed point
  --verify          run the CIR verifier and report any diagnostics
  --run             execute the program on the reference stack VM and print
                    whatever it wrote; the exit status is 4 if it trapped
  --entry=<name>    entry point for --run                 (default: main)
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
  bool runIt = false;
  std::string entry = "main";
};

bool startsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int unknownStage(const std::string &stage) {
  std::cerr << "error: unknown stage '" << stage
            << "'. Known stages: tokens, ast, cir, ll, wat, sbc.\n";
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
    } else if (arg == "--run") {
      options.runIt = true;
    } else if (startsWith(arg, "--entry=")) {
      options.entry = arg.substr(8);
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

  if (options.emit != "tokens" && options.emit != "ast" && options.emit != "cir" &&
      options.emit != "ll" && options.emit != "wat" && options.emit != "sbc")
    return unknownStage(options.emit);

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

  const bool isMiniLang = endsWith(options.input, ".mini");

  // -- the two front-end-only stages ---------------------------------------
  if (options.emit == "tokens" || options.emit == "ast") {
    if (!isMiniLang)
      return fail("--emit=" + options.emit + " needs a .mini source file");

    if (options.emit == "tokens") {
      mtir::frontend::LexResult lexed =
          mtir::frontend::tokenize(source, options.input);
      for (const mtir::ast::Token &tok : lexed.tokens) {
        std::printf("%4d:%-4d %-12s '%s'\n", tok.loc.line, tok.loc.col,
                    std::string(mtir::ast::tokKindName(tok.kind)).c_str(),
                    tok.text.c_str());
      }
      if (!lexed.ok()) {
        std::cerr << mtir::support::format(lexed.diagnostics);
        return 1;
      }
      return 0;
    }

    mtir::frontend::ParseResult parsedAst =
        mtir::frontend::parse(source, options.input);
    if (!parsedAst.ok()) {
      std::cerr << mtir::support::format(parsedAst.diagnostics);
      return 1;
    }
    std::cout << mtir::ast::dump(*parsedAst.program);
    return 0;
  }

  // -- .mini or .cir into a CIR module -------------------------------------
  mtir::cir::Module module;
  if (isMiniLang) {
    mtir::frontend::ParseResult parsedAst =
        mtir::frontend::parse(source, options.input);
    if (!parsedAst.ok()) {
      std::cerr << mtir::support::format(parsedAst.diagnostics);
      return 1;
    }

    mtir::sema::AnalysisResult analysis =
        mtir::sema::analyse(*parsedAst.program, options.input);
    if (!analysis.ok()) {
      std::cerr << mtir::support::format(analysis.diagnostics);
      return 1;
    }

    mtir::cir::BuildResult built =
        mtir::cir::build(*parsedAst.program, *analysis.types, moduleName);
    if (!built.ok()) {
      std::cerr << mtir::support::format(built.diagnostics);
      return 1;
    }
    module = std::move(*built.module);
  } else {
    mtir::cir::ParseResult parsed =
        mtir::cir::parseCir(source, moduleName, options.input);
    if (!parsed.ok()) {
      std::cerr << mtir::support::format(parsed.diagnostics);
      return 1;
    }
    module = std::move(*parsed.module);
  }

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

  // --run short-circuits the emitters: the VM executes the bytecode
  // directly, so nothing needs to be printed as text first.
  if (options.runIt) {
    mtir::backend::stackvm::EmitResult lowered =
        mtir::backend::stackvm::emitModule(module);
    if (!lowered.diagnostics.empty())
      std::cerr << mtir::support::format(lowered.diagnostics);
    if (!lowered.ok())
      return 1;

    mtir::backend::stackvm::RunOptions runOptions;
    runOptions.entry = options.entry;
    const mtir::backend::stackvm::RunResult outcome =
        mtir::backend::stackvm::run(lowered.program, runOptions);

    std::cout << outcome.output;
    std::cout.flush();
    if (outcome.trapped) {
      std::cerr << "trap: " << outcome.trap << "\n";
      return 4;
    }
    return 0;
  }

  std::string text;
  if (options.emit == "cir") {
    text = mtir::cir::printModule(module);
  } else if (options.emit == "sbc") {
    mtir::backend::stackvm::EmitResult result =
        mtir::backend::stackvm::emitModule(module);
    if (!result.diagnostics.empty())
      std::cerr << mtir::support::format(result.diagnostics);
    if (!result.ok())
      return 1;
    text = mtir::backend::stackvm::printProgram(result.program);
  } else if (options.emit == "wat") {
    mtir::backend::wasm::EmitResult result = mtir::backend::wasm::emitModule(module);
    if (!result.diagnostics.empty())
      std::cerr << mtir::support::format(result.diagnostics);
    if (!result.ok())
      return 1;
    text = std::move(result.wat);
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
