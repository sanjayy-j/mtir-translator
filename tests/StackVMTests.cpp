// StackVMTests.cpp -- the stack bytecode back end (M7) and its VM.
//
// These are the only tests in the project that *run* a lowered program.
// llvm-as, lli, wat2wasm and wasmtime are all absent on this machine, so the
// LLVM and WebAssembly outputs can be inspected but never executed; the
// bytecode VM is where docs/divergence.md stops being a table of claims and
// becomes a set of observations.  Each divergence test below names the row it
// covers.
//
// There is no Python counterpart: src/backend/stackvm/emit_sbc.py and vm.py
// are stubs that raise NotImplementedError.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "mtir/backend/stackvm/EmitSbc.h"
#include "mtir/backend/stackvm/VM.h"
#include "mtir/cir/Parser.h"
#include "mtir/opt/Pass.h"
#include "mtir_test.h"

using namespace mtir;
using namespace mtir::backend::stackvm;

namespace {

/// Lower CIR text to bytecode.  A parse failure is reported here rather than
/// left to surface as a puzzling "no such function" trap at run time.
Program lower(const std::string &cirText) {
  cir::ParseResult parsed = cir::parseCir(cirText, "t");
  CHECK(parsed.module.has_value());
  if (!parsed.module.has_value())
    return Program{};
  return emitModule(*parsed.module).program;
}

/// Run `entry`, with integer arguments, over bytecode lowered from CIR text.
RunResult exec(const std::string &cirText, const std::string &entry = "main",
               const std::vector<std::int64_t> &args = {}) {
  RunOptions options;
  options.entry = entry;
  for (const std::int64_t a : args)
    options.args.push_back(Value::ofInt(a));
  return run(lower(cirText), options);
}

/// A `main` that computes one i32 expression and returns it.
std::string mainReturning(const std::string &body) {
  return "func @main() -> i32 {\nentry:\n" + body + "\n}\n";
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

const char *kAbsCir = R"(func @abs(i32 %x) -> i32 {
entry:
  %t0 = icmp.slt i32 %x, 0
  br %t0 ? then : exit
then:
  %t1 = sub i32 0, %x
  ret i32 %t1
exit:
  ret i32 %x
}
)";

} // namespace

// --------------------------------------------------------------------------
// The bytecode
// --------------------------------------------------------------------------
MTIR_TEST("sbc", "abs lowers to a flat ten-instruction function") {
  // The same count the peepholed stack lowering produces: going flat costs
  // one `jz` and saves the `br_if` the register-to-stack schema would emit.
  const Program program = lower(kAbsCir);
  CHECK_EQ(program.functions.size(), static_cast<std::size_t>(1));
  CHECK_EQ(instructionCount(program), static_cast<std::size_t>(10));
}

MTIR_TEST("sbc", "a branch target is an absolute address inside the function") {
  const Program program = lower(kAbsCir);
  CHECK(!program.functions.empty());
  if (program.functions.empty())
    return;

  const Function &fn = program.functions[0];
  std::size_t branches = 0;
  for (const Instruction &i : fn.code) {
    if (i.mnemonic != "jmp" && i.mnemonic != "jz")
      continue;
    ++branches;
    CHECK(i.hasImm);
    CHECK(i.imm >= 0);
    CHECK(static_cast<std::size_t>(i.imm) < fn.code.size());
  }
  CHECK(branches > 0);
}

MTIR_TEST("sbc", "a jump to the next block is elided") {
  // `br.cond` lowers to jz + jmp, but the jmp takes the true edge, which for
  // abs is the very next block: control reaches it by falling through.
  const Program program = lower(kAbsCir);
  CHECK(!program.functions.empty());
  if (program.functions.empty())
    return;

  std::size_t jumps = 0;
  for (const Instruction &i : program.functions[0].code)
    if (i.mnemonic == "jmp")
      ++jumps;
  CHECK_EQ(jumps, static_cast<std::size_t>(0));
}

MTIR_TEST("sbc", "a conditional branch jumps on the false edge") {
  const Program program = lower(kAbsCir);
  CHECK(!program.functions.empty());
  if (program.functions.empty())
    return;

  const Function &fn = program.functions[0];
  // `br %t0 ? then : exit`: jz goes to `exit`, which is the last block, so
  // its address is the start of the final two instructions.
  bool found = false;
  for (const Instruction &i : fn.code)
    if (i.mnemonic == "jz" && static_cast<std::size_t>(i.imm) == fn.code.size() - 2)
      found = true;
  CHECK(found);
}

MTIR_TEST("sbc", "locals are numbered with the parameters first") {
  // A call binds arguments by position, so parameter order has to be the
  // slot order.
  const Program program = lower(R"(func @f(i32 %a, i32 %b) -> i32 {
entry:
  %t = add i32 %a, %b
  %u = mul i32 %t, %a
  %v = add i32 %u, %b
  ret i32 %v
}
)");
  CHECK(!program.functions.empty());
  if (program.functions.empty())
    return;

  const Function &fn = program.functions[0];
  CHECK(fn.locals.size() >= 2);
  CHECK_EQ(fn.locals[0], std::string("$a"));
  CHECK_EQ(fn.locals[1], std::string("$b"));

  for (const Instruction &i : fn.code)
    if (i.mnemonic == "local.get" || i.mnemonic == "local.set") {
      CHECK(i.hasImm);
      CHECK(static_cast<std::size_t>(i.imm) < fn.locals.size());
      CHECK_EQ(fn.locals[static_cast<std::size_t>(i.imm)], i.text);
    }
}

MTIR_TEST("sbc", "a function that takes an address declares a frame") {
  const Program withFrame = lower(R"(func @f() -> i32 {
entry:
  %p = alloca i32
  store i32 7, %p
  %v = load i32 %p
  ret i32 %v
}
)");
  CHECK(!withFrame.functions.empty());
  if (!withFrame.functions.empty())
    CHECK(withFrame.functions[0].frameSize > 0);

  const Program withoutFrame = lower(kAbsCir);
  CHECK(!withoutFrame.functions.empty());
  if (!withoutFrame.functions.empty())
    CHECK_EQ(withoutFrame.functions[0].frameSize, static_cast<std::int64_t>(0));
}

MTIR_TEST("sbc", "the listing is addressed so a branch can be followed") {
  const std::string text = printProgram(lower(kAbsCir));
  CHECK(contains(text, "sbc v1"));
  CHECK(contains(text, "stack bytecode back end (M7)"));
  CHECK(contains(text, ".func abs(i32) -> i32"));
  CHECK(contains(text, "  .frame 0"));
  CHECK(contains(text, "  .local 0 $x"));
  CHECK(contains(text, "0000  "));
  CHECK(contains(text, ".end"));
}

MTIR_TEST("sbc", "a block with no terminator is reported, not guessed at") {
  cir::Module m("t");
  cir::BasicBlock entry("entry");
  entry.add(cir::Instruction::binary(cir::Opcode::Add, cir::Ty::I32,
                                     cir::Reg{"a", cir::Ty::I32},
                                     cir::ConstInt{1, cir::Ty::I32},
                                     cir::ConstInt{2, cir::Ty::I32}));
  cir::Function fn("f", {}, cir::Ty::I32);
  fn.setBlocks({entry});
  m.addFunction(std::move(fn));

  const EmitResult result = emitModule(m);
  CHECK(!result.ok());
  CHECK(mtir::test::hasCode(result.diagnostics, "SBC01"));
}

// --------------------------------------------------------------------------
// Execution
// --------------------------------------------------------------------------
MTIR_TEST("vm", "abs runs and returns the absolute value") {
  const RunResult negative = exec(kAbsCir, "abs", {-5});
  CHECK(negative.ok());
  CHECK_EQ(negative.result.i, static_cast<std::int64_t>(5));

  const RunResult positive = exec(kAbsCir, "abs", {7});
  CHECK(positive.ok());
  CHECK_EQ(positive.result.i, static_cast<std::int64_t>(7));

  const RunResult zero = exec(kAbsCir, "abs", {0});
  CHECK(zero.ok());
  CHECK_EQ(zero.result.i, static_cast<std::int64_t>(0));
}

MTIR_TEST("vm", "recursion computes the factorial") {
  const RunResult result = exec(R"(func @fact(i32 %n) -> i32 {
entry:
  %t0 = icmp.sle i32 %n, 1
  br %t0 ? base : step
base:
  ret i32 1
step:
  %t1 = sub i32 %n, 1
  %t2 = call i32 @fact(%t1)
  %t3 = mul i32 %n, %t2
  ret i32 %t3
}
)",
                                 "fact", {10});
  CHECK(result.ok());
  CHECK_EQ(result.result.i, static_cast<std::int64_t>(3628800));
}

MTIR_TEST("vm", "print writes one value per line") {
  const RunResult result = exec(R"(func @main() -> i32 {
entry:
  print.i32 42
  print.i32 -7
  ret i32 0
}
)");
  CHECK(result.ok());
  CHECK_EQ(result.output, std::string("42\n-7\n"));
}

MTIR_TEST("vm", "a local variable round-trips through the frame") {
  const RunResult result = exec(R"(func @main() -> i32 {
entry:
  %p = alloca i32
  %q = alloca i32
  store i32 11, %p
  store i32 31, %q
  %a = load i32 %p
  %b = load i32 %q
  %s = add i32 %a, %b
  ret i32 %s
}
)");
  CHECK(result.ok());
  CHECK_EQ(result.result.i, static_cast<std::int64_t>(42));
}

MTIR_TEST("vm", "an array element is addressed through a scaled gep") {
  const RunResult result = exec(R"(func @main() -> i32 {
entry:
  %xs = alloca i32, 4
  %e0 = gep i32 %xs, 0
  %e3 = gep i32 %xs, 3
  store i32 100, %e0
  store i32 7, %e3
  %a = load i32 %e0
  %b = load i32 %e3
  %s = add i32 %a, %b
  ret i32 %s
}
)");
  // 107 only if the two elements are distinct addresses: an unscaled index
  // would make xs[3] overlap xs[0].
  CHECK(result.ok());
  CHECK_EQ(result.result.i, static_cast<std::int64_t>(107));
}

MTIR_TEST("vm", "a global keeps its value across calls") {
  const RunResult result = exec(R"(global @counter : i32 = 5

func @bump() -> void {
entry:
  %v = load i32 @counter
  %n = add i32 %v, 1
  store i32 %n, @counter
  ret void
}

func @main() -> i32 {
entry:
  call void @bump()
  call void @bump()
  %v = load i32 @counter
  ret i32 %v
}
)");
  CHECK(result.ok());
  CHECK_EQ(result.result.i, static_cast<std::int64_t>(7));
}

// --------------------------------------------------------------------------
// docs/divergence.md, executed rather than asserted
// --------------------------------------------------------------------------
MTIR_TEST("vm", "row 1: integer division by zero traps") {
  const RunResult divide = exec(mainReturning("  %t = sdiv i32 7, 0\n  ret i32 %t"));
  CHECK(divide.trapped);
  CHECK(contains(divide.trap, "division by zero"));

  const RunResult remainder = exec(mainReturning("  %t = srem i32 7, 0\n  ret i32 %t"));
  CHECK(remainder.trapped);
  CHECK(contains(remainder.trap, "division by zero"));

  const RunResult unsignedDivide =
      exec(mainReturning("  %t = udiv i32 7, 0\n  ret i32 %t"));
  CHECK(unsignedDivide.trapped);
  CHECK(contains(unsignedDivide.trap, "division by zero"));
}

MTIR_TEST("vm", "row 1: float division by zero does not trap") {
  // The row is about integer division; IEEE says infinity.
  const RunResult result = exec(R"(func @main() -> f64 {
entry:
  %t = fdiv f64 1.0, 0.0
  ret f64 %t
}
)");
  CHECK(result.ok());
  CHECK(result.result.f > 0.0);
}

MTIR_TEST("vm", "row 2: INT_MIN / -1 traps rather than wrapping") {
  const RunResult divide =
      exec(mainReturning("  %t = sdiv i32 -2147483648, -1\n  ret i32 %t"));
  CHECK(divide.trapped);
  CHECK(contains(divide.trap, "overflow"));

  const RunResult remainder =
      exec(mainReturning("  %t = srem i32 -2147483648, -1\n  ret i32 %t"));
  CHECK(remainder.trapped);
  CHECK(contains(remainder.trap, "overflow"));
}

MTIR_TEST("vm", "row 3: a shift count is taken modulo the width") {
  const RunResult byZero = exec(mainReturning("  %t = shl i32 1, 0\n  ret i32 %t"));
  CHECK(byZero.ok());
  CHECK_EQ(byZero.result.i, static_cast<std::int64_t>(1));

  // 32 and 33 wrap to 0 and 1 rather than being undefined or zero.
  const RunResult byWidth = exec(mainReturning("  %t = shl i32 1, 32\n  ret i32 %t"));
  CHECK(byWidth.ok());
  CHECK_EQ(byWidth.result.i, static_cast<std::int64_t>(1));

  const RunResult beyond = exec(mainReturning("  %t = shl i32 1, 33\n  ret i32 %t"));
  CHECK(beyond.ok());
  CHECK_EQ(beyond.result.i, static_cast<std::int64_t>(2));
}

MTIR_TEST("vm", "row 3: an arithmetic shift keeps the sign") {
  const RunResult result = exec(mainReturning("  %t = ashr i32 -8, 1\n  ret i32 %t"));
  CHECK(result.ok());
  CHECK_EQ(result.result.i, static_cast<std::int64_t>(-4));

  const RunResult logical = exec(mainReturning("  %t = lshr i32 -8, 1\n  ret i32 %t"));
  CHECK(logical.ok());
  CHECK_EQ(logical.result.i, static_cast<std::int64_t>(2147483644));
}

MTIR_TEST("vm", "row 4: signed overflow wraps and does not trap") {
  const RunResult add =
      exec(mainReturning("  %t = add i32 2147483647, 1\n  ret i32 %t"));
  CHECK(add.ok());
  CHECK_EQ(add.result.i,
           static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()));

  const RunResult multiply =
      exec(mainReturning("  %t = mul i32 65536, 65536\n  ret i32 %t"));
  CHECK(multiply.ok());
  CHECK_EQ(multiply.result.i, static_cast<std::int64_t>(0));

  const RunResult negate =
      exec(mainReturning("  %t = sub i32 0, -2147483648\n  ret i32 %t"));
  CHECK(negate.ok());
  CHECK_EQ(negate.result.i,
           static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()));
}

MTIR_TEST("vm", "row 5: a comparison yields exactly zero or one") {
  const RunResult t = exec(R"(func @main() -> i1 {
entry:
  %t = icmp.slt i32 -1, 0
  ret i1 %t
}
)");
  CHECK(t.ok());
  CHECK_EQ(t.result.i, static_cast<std::int64_t>(1));

  const RunResult f = exec(R"(func @main() -> i1 {
entry:
  %t = icmp.slt i32 0, -1
  ret i1 %t
}
)");
  CHECK(f.ok());
  CHECK_EQ(f.result.i, static_cast<std::int64_t>(0));
}

MTIR_TEST("vm", "row 5: unsigned and signed comparisons differ on the same bits") {
  const RunResult signedLess =
      exec(R"(func @main() -> i1 {
entry:
  %t = icmp.slt i32 -1, 1
  ret i1 %t
}
)");
  CHECK(signedLess.ok());
  CHECK_EQ(signedLess.result.i, static_cast<std::int64_t>(1));

  // As unsigned, -1 is 4294967295 and is not below 1.
  const RunResult unsignedLess =
      exec(R"(func @main() -> i1 {
entry:
  %t = icmp.ult i32 -1, 1
  ret i1 %t
}
)");
  CHECK(unsignedLess.ok());
  CHECK_EQ(unsignedLess.result.i, static_cast<std::int64_t>(0));
}

MTIR_TEST("vm", "row 6: a float to int conversion traps out of range") {
  const RunResult inRange = exec(R"(func @main() -> i32 {
entry:
  %t = fptosi i32 3.9
  ret i32 %t
}
)");
  CHECK(inRange.ok());
  CHECK_EQ(inRange.result.i, static_cast<std::int64_t>(3)); // truncates toward zero

  const RunResult tooLarge = exec(R"(func @main() -> i32 {
entry:
  %t = fptosi i32 1.0e18
  ret i32 %t
}
)");
  CHECK(tooLarge.trapped);
  CHECK(contains(tooLarge.trap, "out of range"));
}

MTIR_TEST("vm", "row 6: converting NaN traps") {
  const RunResult result = exec(R"(func @main() -> i32 {
entry:
  %n = fdiv f64 0.0, 0.0
  %t = fptosi i32 %n
  ret i32 %t
}
)");
  CHECK(result.trapped);
  CHECK(contains(result.trap, "NaN"));
}

MTIR_TEST("vm", "row 6: an ordered comparison is false when an operand is NaN") {
  const char *shape = R"(func @main() -> i1 {
entry:
  %n = fdiv f64 0.0, 0.0
  %t = PRED f64 %n, 1.0
  ret i1 %t
}
)";
  const char *predicates[] = {"fcmp.oeq", "fcmp.one", "fcmp.olt", "fcmp.ogt"};
  for (const char *predicate : predicates) {
    std::string text = shape;
    text.replace(text.find("PRED"), 4, predicate);
    const RunResult result = exec(text);
    CHECK(result.ok());
    CHECK_EQ(result.result.i, static_cast<std::int64_t>(0));
  }
}

MTIR_TEST("vm", "row 7: a store outside linear memory traps") {
  // The frame lives at the top of memory, so a large positive index runs off
  // the end rather than wrapping into somebody else's slot.
  RunOptions options;
  options.entry = "main";
  options.memoryBytes = 1024;
  const RunResult result = run(lower(R"(func @main() -> i32 {
entry:
  %xs = alloca i32, 2
  %e = gep i32 %xs, 100000
  store i32 1, %e
  ret i32 0
}
)"),
                               options);
  CHECK(result.trapped);
  CHECK(contains(result.trap, "outside linear memory"));
}

// --------------------------------------------------------------------------
// The VM refuses to hang or recurse without bound
// --------------------------------------------------------------------------
MTIR_TEST("vm", "an endless loop stops at the step limit") {
  RunOptions options;
  options.entry = "main";
  options.stepLimit = 5000;
  const RunResult result = run(lower(R"(func @main() -> i32 {
entry:
  br loop
loop:
  br loop
}
)"),
                               options);
  CHECK(result.trapped);
  CHECK(contains(result.trap, "step limit"));
}

MTIR_TEST("vm", "unbounded recursion stops at the call depth limit") {
  RunOptions options;
  options.entry = "f";
  options.callDepthLimit = 32;
  const RunResult result = run(lower(R"(func @f() -> i32 {
entry:
  %t = call i32 @f()
  ret i32 %t
}
)"),
                               options);
  CHECK(result.trapped);
  CHECK(contains(result.trap, "call depth"));
}

// --------------------------------------------------------------------------
// The optimiser must not change what a program does
// --------------------------------------------------------------------------
MTIR_TEST("vm", "optimisation preserves the observable behaviour of the corpus") {
  // The strongest check the project can make on this machine: every corpus
  // program is run twice, unoptimised and at -O1, and must print the same
  // thing and finish the same way.  Nothing else here executes a program
  // before and after a pass.
  const char *names[] = {"arith", "control_flow", "recursion",
                         "nesting", "shift_edge", "div_edge"};

  for (const char *name : names) {
    const std::string source = mtir::test::readFile(
        mtir::test::example(std::string("tests/cir/") + name + ".cir"));
    CHECK(!source.empty());

    cir::ParseResult plain = cir::parseCir(source, name);
    cir::ParseResult optimised = cir::parseCir(source, name);
    CHECK(plain.module.has_value());
    CHECK(optimised.module.has_value());
    if (!plain.module.has_value() || !optimised.module.has_value())
      continue;

    opt::PassManager pipeline = opt::defaultPipeline(1);
    pipeline.run(*optimised.module);

    const RunResult before = run(emitModule(*plain.module).program);
    const RunResult after = run(emitModule(*optimised.module).program);

    CHECK_EQ(before.output, after.output);
    CHECK_EQ(before.trapped, after.trapped);
    CHECK_EQ(before.result.i, after.result.i);
  }
}

MTIR_TEST("vm", "the corpus produces the results the sources say it should") {
  struct Expected {
    const char *name;
    const char *output;
    bool traps;
  };
  // Each figure is derived from the .mini source, not from a previous run:
  // arith is 14 + 20 + 2 + 19, control_flow sums the odd i below 8, recursion
  // is 10! and is_even(10), nesting is deep(3), and div_edge prints 7 / 2
  // before INT_MIN / -1 traps.
  static const Expected cases[] = {
      {"arith", "55\n", false},
      {"control_flow", "16\n", false},
      {"recursion", "3628800\n1\n", false},
      {"nesting", "3\n", false},
      {"shift_edge", "1\n-2147483648\n1\n2\n", false},
      {"div_edge", "3\n", true},
  };

  for (const Expected &expected : cases) {
    const std::string source = mtir::test::readFile(
        mtir::test::example(std::string("tests/cir/") + expected.name + ".cir"));
    CHECK(!source.empty());

    cir::ParseResult parsed = cir::parseCir(source, expected.name);
    CHECK(parsed.module.has_value());
    if (!parsed.module.has_value())
      continue;

    const RunResult result = run(emitModule(*parsed.module).program);
    CHECK_EQ(result.output, std::string(expected.output));
    CHECK_EQ(result.trapped, expected.traps);
  }
}
