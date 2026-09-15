// Driver.cpp -- the mtirc command line.  See Driver.h for the exit codes.
//
// Every stage the Python prototype named is now implemented, so there is no
// longer a "not implemented yet" path: an unknown --emit is simply an error.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "mtir/tool/Driver.h"

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

namespace mtir::tool {
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

int unknownStage(const std::string &stage, std::ostream &err) {
  err << "error: unknown stage '" << stage
      << "'. Known stages: tokens, ast, cir, ll, wat, sbc.\n";
  return 1;
}

int fail(const std::string &message, std::ostream &err) {
  err << "error: " << message << "\n";
  return 1;
}

} // namespace

int runDriver(const std::vector<std::string> &args, std::ostream &out,
              std::ostream &err) {
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "-h" || arg == "--help") {
      out << kUsage;
      return 0;
    }
    if (arg == "--version") {
      out << "mtirc 0.1 (C++17)\n";
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
    } else if (arg == "-o" && i + 1 < args.size()) {
      options.output = args[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      err << "error: unknown option '" << arg << "'\n\n" << kUsage;
      return 1;
    } else if (options.input.empty()) {
      options.input = arg;
    } else {
      return fail("more than one input file given", err);
    }
  }

  if (options.emit != "tokens" && options.emit != "ast" && options.emit != "cir" &&
      options.emit != "ll" && options.emit != "wat" && options.emit != "sbc")
    return unknownStage(options.emit, err);

  if (options.input.empty()) {
    err << kUsage;
    return 1;
  }

  std::ifstream in(options.input, std::ios::binary);
  if (!in) {
    err << "error: no such file: " << options.input << "\n";
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
      return fail("--emit=" + options.emit + " needs a .mini source file", err);

    if (options.emit == "tokens") {
      mtir::frontend::LexResult lexed =
          mtir::frontend::tokenize(source, options.input);
      for (const mtir::ast::Token &tok : lexed.tokens) {
        // Formatted with snprintf for the column alignment, then written to
        // the stream rather than to stdout, so a test can read it back.
        char line[512];
        std::snprintf(line, sizeof line, "%4d:%-4d %-12s '%s'\n", tok.loc.line,
                      tok.loc.col,
                      std::string(mtir::ast::tokKindName(tok.kind)).c_str(),
                      tok.text.c_str());
        out << line;
      }
      if (!lexed.ok()) {
        err << mtir::support::format(lexed.diagnostics);
        return 1;
      }
      return 0;
    }

    mtir::frontend::ParseResult parsedAst =
        mtir::frontend::parse(source, options.input);
    if (!parsedAst.ok()) {
      err << mtir::support::format(parsedAst.diagnostics);
      return 1;
    }
    out << mtir::ast::dump(*parsedAst.program);
    return 0;
  }

  // -- .mini or .cir into a CIR module -------------------------------------
  mtir::cir::Module module;
  if (isMiniLang) {
    mtir::frontend::ParseResult parsedAst =
        mtir::frontend::parse(source, options.input);
    if (!parsedAst.ok()) {
      err << mtir::support::format(parsedAst.diagnostics);
      return 1;
    }

    mtir::sema::AnalysisResult analysis =
        mtir::sema::analyse(*parsedAst.program, options.input);
    if (!analysis.ok()) {
      err << mtir::support::format(analysis.diagnostics);
      return 1;
    }

    mtir::cir::BuildResult built =
        mtir::cir::build(*parsedAst.program, *analysis.types, moduleName);
    if (!built.ok()) {
      err << mtir::support::format(built.diagnostics);
      return 1;
    }
    module = std::move(*built.module);
  } else {
    mtir::cir::ParseResult parsed =
        mtir::cir::parseCir(source, moduleName, options.input);
    if (!parsed.ok()) {
      err << mtir::support::format(parsed.diagnostics);
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
      err << mtir::support::format(diags);
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
      err << mtir::support::format(lowered.diagnostics);
    if (!lowered.ok())
      return 1;

    mtir::backend::stackvm::RunOptions runOptions;
    runOptions.entry = options.entry;
    const mtir::backend::stackvm::RunResult outcome =
        mtir::backend::stackvm::run(lowered.program, runOptions);

    out << outcome.output;
    out.flush();
    if (outcome.trapped) {
      err << "trap: " << outcome.trap << "\n";
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
      err << mtir::support::format(result.diagnostics);
    if (!result.ok())
      return 1;
    text = mtir::backend::stackvm::printProgram(result.program);
  } else if (options.emit == "wat") {
    mtir::backend::wasm::EmitResult result = mtir::backend::wasm::emitModule(module);
    if (!result.diagnostics.empty())
      err << mtir::support::format(result.diagnostics);
    if (!result.ok())
      return 1;
    text = std::move(result.wat);
  } else {
    mtir::backend::llvm::EmitResult result = mtir::backend::llvm::emitModule(module);
    if (!result.ok()) {
      err << mtir::support::format(result.diagnostics);
      return 1;
    }
    text = std::move(result.ir);
  }

  if (options.output.empty()) {
    out << text;
    out.flush();
    return 0;
  }

  std::ofstream file(options.output, std::ios::binary);
  if (!file)
    return fail("cannot write to " + options.output, err);
  file << text;
  return 0;
}

const char *usage() { return kUsage; }

} // namespace mtir::tool
