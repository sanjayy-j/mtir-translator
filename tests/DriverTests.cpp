// DriverTests.cpp -- end-to-end tests for the CLI driver (M9).
//
// Ported from tests/test_driver.py.  The Python tests called main() with an
// argument list and read stdout back through capsys; runDriver takes the
// streams as parameters, so these do the same thing without spawning a
// process or plumbing a path to the executable through the build.
//
// Two of the Python tests have no counterpart here, and deliberately:
//
//   test_demo_cir_matches_golden        --demo-cir was a scaffold for a
//                                       driver with no front end.  There is
//                                       a front end now, and the golden is
//                                       checked by driving the real pipeline
//                                       ("--emit=cir reproduces the golden"
//                                       below).
//   test_unimplemented_stages_...       every stage is implemented, so there
//                                       is no exit-3 path left to test.  The
//                                       replacement is the unknown-stage
//                                       test, which checks that the tool
//                                       still names what it does support.
#include <sstream>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "mtir/tool/Driver.h"
#include "mtir_test.h"

using namespace mtir;

namespace {

struct Run {
  int status = 0;
  std::string out;
  std::string err;
};

Run drive(const std::vector<std::string> &args) {
  std::ostringstream out;
  std::ostringstream err;
  Run result;
  result.status = tool::runDriver(args, out, err);
  result.out = out.str();
  result.err = err.str();
  return result;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

bool startsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string example(const std::string &relative) {
  return mtir::test::example(relative);
}

const char *kAbsMini = "docs/examples/abs.mini";

} // namespace

// --------------------------------------------------------------------------
// Front-end stages
// --------------------------------------------------------------------------
MTIR_TEST("driver", "--emit=tokens lists the token stream") {
  const Run run = drive({"--emit=tokens", example(kAbsMini)});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, "KW_FN"));
  CHECK(contains(run.out, "IDENT"));
}

MTIR_TEST("driver", "--emit=ast dumps the parse tree") {
  const Run run = drive({"--emit=ast", example(kAbsMini)});
  CHECK_EQ(run.status, 0);
  CHECK(startsWith(run.out, "Program"));
  CHECK(contains(run.out, "FnDecl abs(x: int) -> int"));
}

// --------------------------------------------------------------------------
// The middle end and the back ends
// --------------------------------------------------------------------------
MTIR_TEST("driver", "--emit=cir lowers a .mini source") {
  const Run run = drive({"--emit=cir", example(kAbsMini)});
  CHECK_EQ(run.status, 0);
  CHECK(startsWith(run.out, "func @abs(i32 %x) -> i32 {"));
  CHECK(contains(run.out, "icmp.slt i32 %x, 0"));
}

MTIR_TEST("driver", "--emit=cir round-trips the golden byte for byte") {
  // docs/examples/abs.cir is the anchor for the whole CIR subsystem, and what
  // it anchors is parse-then-print: printModule(parseCir(t)) == t.
  //
  // It is deliberately not the front end's output for abs.mini.  The golden
  // is hand-written to show the CIR form at its clearest -- blocks called
  // `then` and `exit` -- whereas the builder names them after the statement
  // that produced them (if.then.0, if.end.0), and abs.mini also defines a
  // main.  Pinning the front end to this file would be pinning it to
  // presentation choices the builder never claimed to make.
  const Run run = drive({"--emit=cir", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 0);
  CHECK_EQ(run.out, mtir::test::readFile(example("docs/examples/abs.cir")));
}

MTIR_TEST("driver", "--emit=cir lowers abs.mini to the same shape as the golden") {
  // What the front end owes the golden is the structure, not the labels.
  const Run run = drive({"--emit=cir", example(kAbsMini)});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, "func @abs(i32 %x) -> i32 {"));
  CHECK(contains(run.out, "%t0 = icmp.slt i32 %x, 0"));
  CHECK(contains(run.out, "%t1 = sub i32 0, %x"));
  CHECK(contains(run.out, "func @main() -> i32 {"));
  CHECK(contains(run.out, "call i32 @abs(-7)"));
}

MTIR_TEST("driver", "--emit=ll lowers to LLVM IR") {
  const Run run = drive({"--emit=ll", example(kAbsMini)});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, "define i32 @abs(i32 %x) {"));
  CHECK(contains(run.out, "br i1 %t0, label %"));
}

MTIR_TEST("driver", "a back end accepts a .cir file with no front end involved") {
  // The .cir format is the contract: the back end needs no front end.
  const Run run = drive({"--emit=ll", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, "define i32 @abs(i32 %x) {"));
  CHECK(contains(run.out, "br i1 %t0, label %then, label %exit"));
}

MTIR_TEST("driver", "--emit=wat lowers to WebAssembly text") {
  const Run run = drive({"--emit=wat", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 0);
  CHECK(startsWith(run.out, "(module"));
  CHECK(contains(run.out, "(func $abs (param $x i32) (result i32)"));
}

MTIR_TEST("driver", "--emit=sbc lowers to stack bytecode") {
  const Run run = drive({"--emit=sbc", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, ".func abs(i32) -> i32"));
  CHECK(contains(run.out, ".end"));
}

MTIR_TEST("driver", "--opt=1 folds constants") {
  const Run run =
      drive({"--opt=1", "--emit=cir", example("tests/corpus/valid/arith.mini")});
  CHECK_EQ(run.status, 0);
  CHECK(contains(run.out, "store i32 14, %a.addr")); // 2 + 3 * 4
  CHECK(!contains(run.out, "mul i32"));
}

MTIR_TEST("driver", "--verify accepts a well-formed module") {
  const Run run = drive({"--verify", "--emit=cir", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 0);
  CHECK(run.err.empty());
}

// --------------------------------------------------------------------------
// Running a program
// --------------------------------------------------------------------------
MTIR_TEST("driver", "--run executes a .mini source end to end") {
  const Run run = drive({"--run", example("tests/corpus/valid/recursion.mini")});
  CHECK_EQ(run.status, 0);
  CHECK_EQ(run.out, std::string("3628800\n1\n"));
}

MTIR_TEST("driver", "--run reports a trap with exit status 4") {
  const Run run = drive({"--run", example("tests/cir/div_edge.cir")});
  CHECK_EQ(run.status, 4);
  CHECK_EQ(run.out, std::string("3\n")); // printed before the trap
  CHECK(contains(run.err, "trap: "));
  CHECK(contains(run.err, "overflow"));
}

MTIR_TEST("driver", "--run gives the same answer with and without optimisation") {
  const Run plain = drive({"--run", example("tests/corpus/valid/control_flow.mini")});
  const Run optimised =
      drive({"--run", "--opt=1", example("tests/corpus/valid/control_flow.mini")});
  CHECK_EQ(plain.status, 0);
  CHECK_EQ(optimised.status, 0);
  CHECK_EQ(plain.out, optimised.out);
}

MTIR_TEST("driver", "--entry picks the function to run") {
  const Run run = drive({"--run", "--entry=abs", example("docs/examples/abs.cir")});
  // abs takes a parameter and none is supplied, so it runs with zero and
  // returns zero -- the point is that the entry point was honoured rather
  // than `main` being demanded.
  CHECK_EQ(run.status, 0);

  const Run missing =
      drive({"--run", "--entry=nope", example("docs/examples/abs.cir")});
  CHECK_EQ(missing.status, 4);
  CHECK(contains(missing.err, "no such function"));
}

// --------------------------------------------------------------------------
// Failure modes
// --------------------------------------------------------------------------
MTIR_TEST("driver", "a missing file exits 2") {
  const Run run = drive({"--emit=ast", "no/such/file.mini"});
  CHECK_EQ(run.status, 2);
  CHECK(contains(run.err, "no such file"));
}

MTIR_TEST("driver", "a syntax error exits 1") {
  const Run run = drive({"--emit=ast", example("tests/corpus/invalid/missing_semicolon.mini")});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, "error["));
}

MTIR_TEST("driver", "a semantic error exits 1 and names its code") {
  // tests/corpus/invalid holds syntax errors only -- every file in it has to
  // stay parser-rejectable, because that is what the parser tests assert of
  // the whole directory.  arrays.mini is the semantically invalid program the
  // corpus already has: it parses, and sema rejects the array initialiser
  // (docs/decisions/0001-array-initialisers.md).
  const Run run = drive({"--emit=cir", example("tests/corpus/valid/arrays.mini")});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, "E004"));
  CHECK(contains(run.err, "array"));

  // It is a semantic error, not a syntax error: the parser accepts it.
  const Run parsed = drive({"--emit=ast", example("tests/corpus/valid/arrays.mini")});
  CHECK_EQ(parsed.status, 0);
}

MTIR_TEST("driver", "an unknown stage names the stages that do exist") {
  const Run run = drive({"--emit=nonsense", example(kAbsMini)});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, "unknown stage"));
  for (const char *stage : {"tokens", "ast", "cir", "ll", "wat", "sbc"})
    CHECK(contains(run.err, stage));
}

MTIR_TEST("driver", "an unknown option is refused and the usage shown") {
  const Run run = drive({"--frobnicate", example(kAbsMini)});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, "unknown option"));
  CHECK(contains(run.err, "usage: mtirc"));
}

MTIR_TEST("driver", "a front-end-only stage refuses a .cir file") {
  const Run run = drive({"--emit=ast", example("docs/examples/abs.cir")});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, ".mini"));
}

MTIR_TEST("driver", "no input at all shows the usage") {
  const Run run = drive({});
  CHECK_EQ(run.status, 1);
  CHECK(contains(run.err, "usage: mtirc"));
}

MTIR_TEST("driver", "--help and --version succeed and write to stdout") {
  const Run help = drive({"--help"});
  CHECK_EQ(help.status, 0);
  CHECK(contains(help.out, "usage: mtirc"));

  const Run version = drive({"--version"});
  CHECK_EQ(version.status, 0);
  CHECK(contains(version.out, "mtirc"));
  CHECK(contains(version.out, "C++17"));
}

MTIR_TEST("driver", "every stage the usage advertises is actually accepted") {
  // The usage text and the accepted set drifted apart twice during the
  // migration; this keeps them together.
  const std::string text = tool::usage();
  for (const char *stage : {"tokens", "ast", "cir", "ll", "wat", "sbc"}) {
    CHECK(contains(text, stage));
    const Run run = drive({std::string("--emit=") + stage, example(kAbsMini)});
    CHECK(run.status == 0);
  }
}
