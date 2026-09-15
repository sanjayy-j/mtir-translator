// WasmTests.cpp -- CIR -> WebAssembly text format (M6c).
//
// External validation is NOT available on this machine: wat2wasm (WABT) and
// wasmtime are both absent, so nothing here has been assembled or executed.
// Every assertion below is a structural property of the emitted text, checked
// against the WebAssembly text-format rules the emitter is written to, and
// none of them is a substitute for running wat2wasm.  When a toolchain is
// available, cmake/ should gain a WatCheck.cmake alongside LlvmAsCheck.cmake
// and these tests should be backed by it.
//
// There is no Python counterpart: src/backend/wasm/emit_wat.py and
// structurer.py are both stubs that raise NotImplementedError, so the whole
// back end is new work rather than a migration.
#include <cstddef>
#include <string>
#include <vector>

#include "TestSupport.h"
#include "mtir/backend/wasm/EmitWat.h"
#include "mtir/cir/Parser.h"
#include "mtir_test.h"

using namespace mtir;
using namespace mtir::backend::wasm;

namespace {

std::size_t countOf(const std::string &haystack, const std::string &needle) {
  std::size_t n = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size()))
    ++n;
  return n;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::vector<std::string> lines(const std::string &text) {
  std::vector<std::string> out;
  std::string current;
  for (const char c : text) {
    if (c == '\n') {
      out.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty())
    out.push_back(current);
  return out;
}

/// Parenthesis depth at the end of the text, ignoring `;;` line comments and
/// "..." strings.  Zero means balanced; a negative running depth means a
/// stray close, which the caller reports separately.
int parenBalance(const std::string &text, bool &wentNegative) {
  int depth = 0;
  wentNegative = false;
  bool inComment = false;
  bool inString = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (inComment) {
      if (c == '\n')
        inComment = false;
      continue;
    }
    if (inString) {
      if (c == '"')
        inString = false;
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == ';' && i + 1 < text.size() && text[i + 1] == ';') {
      inComment = true;
    } else if (c == '(') {
      ++depth;
    } else if (c == ')') {
      --depth;
      if (depth < 0)
        wentNegative = true;
    }
  }
  return depth;
}

/// Emit a module written as CIR text, failing the test if the CIR itself does
/// not parse.
EmitResult emitFromCir(const std::string &text) {
  cir::ParseResult parsed = cir::parseCir(text, "t");
  if (!parsed.module.has_value()) {
    EmitResult bad;
    bad.diagnostics = parsed.diagnostics;
    return bad;
  }
  return emitModule(*parsed.module);
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

/// One scalar slot and one array, so the frame has a non-trivial layout.
const char *kFrameCir = R"(func @f() -> i32 {
entry:
  %p = alloca i32
  %xs = alloca i32, 4
  store i32 7, %p
  %q = gep i32 %xs, 2
  store i32 9, %q
  %v = load i32 %p
  ret i32 %v
}
)";

} // namespace

// --------------------------------------------------------------------------
// Module shape
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "a module is a balanced s-expression") {
  const EmitResult result = emitModule(mtir::test::demoAbsModule());
  CHECK(result.ok());
  bool negative = false;
  CHECK_EQ(parenBalance(result.wat, negative), 0);
  CHECK(!negative);
}

MTIR_TEST("wasm", "the module opens with (module and names the CIR module") {
  const EmitResult result = emitModule(mtir::test::demoAbsModule());
  CHECK(result.wat.rfind("(module", 0) == 0);
  CHECK(contains(result.wat, "CIR module 'abs'"));
  CHECK(contains(result.wat, "WebAssembly back end (M6c)"));
}

MTIR_TEST("wasm", "every function is defined and exported once") {
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(result.ok());
  // "(func $abs" alone would also match the export, which names the function.
  CHECK_EQ(countOf(result.wat, "(func $abs (param"), static_cast<std::size_t>(1));
  CHECK_EQ(countOf(result.wat, "(export \"abs\" (func $abs))"),
           static_cast<std::size_t>(1));
}

MTIR_TEST("wasm", "a function signature carries its params and result") {
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(contains(result.wat, "(func $abs (param $x i32) (result i32)"));
}

// --------------------------------------------------------------------------
// The dispatch tower
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "the dispatch tower has one case block per CIR block") {
  // abs has three blocks: entry, then, exit.
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(contains(result.wat, "(local $__block i32)"));
  CHECK_EQ(countOf(result.wat, "(block $case0"), static_cast<std::size_t>(1));
  CHECK_EQ(countOf(result.wat, "(block $case1"), static_cast<std::size_t>(1));
  CHECK_EQ(countOf(result.wat, "(block $case2"), static_cast<std::size_t>(1));
  CHECK_EQ(countOf(result.wat, "(block $case3"), static_cast<std::size_t>(0));
}

MTIR_TEST("wasm", "br_table lists every case, innermost first") {
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(contains(result.wat, "br_table $case0 $case1 $case2"));
  // The selector is pushed immediately before the table.
  const std::size_t table = result.wat.find("br_table");
  const std::size_t get = result.wat.rfind("local.get $__block", table);
  CHECK(get != std::string::npos);
  CHECK(table > get);
}

MTIR_TEST("wasm", "the outermost case block is the least indented") {
  // $case2 encloses $case1 encloses $case0; getting the nesting backwards
  // would send every br_table index to the wrong block.
  const EmitResult result = emitFromCir(kAbsCir);
  std::size_t indentOf[3] = {0, 0, 0};
  for (const std::string &line : lines(result.wat))
    for (int i = 0; i < 3; ++i) {
      const std::string opener = "(block $case" + std::to_string(i);
      const std::size_t at = line.find(opener);
      if (at != std::string::npos)
        indentOf[static_cast<std::size_t>(i)] = at;
    }
  CHECK(indentOf[2] < indentOf[1]);
  CHECK(indentOf[1] < indentOf[0]);
}

MTIR_TEST("wasm", "a conditional branch selects a block index and re-dispatches") {
  const EmitResult result = emitFromCir(kAbsCir);
  // `br %t0 ? then : exit` -- then is block 1, exit is block 2.
  CHECK(contains(result.wat, "if (result i32)"));
  CHECK(contains(result.wat, "local.set $__block"));
  CHECK(contains(result.wat, "br $dispatch"));
}

MTIR_TEST("wasm", "a returning block ends in return, not a dispatch branch") {
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK_EQ(countOf(result.wat, "return"), static_cast<std::size_t>(2));
}

MTIR_TEST("wasm", "a value-returning function ends with unreachable") {
  // Control cannot fall out of the tower, but the body still has to be well
  // typed for the declared result.
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(contains(result.wat, "unreachable"));
}

// --------------------------------------------------------------------------
// The peephole reaches the emitted text
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "a temporary that stays on the stack is never declared") {
  // %t0 and %t1 are each defined and consumed once within their own block,
  // so neither needs a local at all.
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(!contains(result.wat, "(local $t0"));
  CHECK(!contains(result.wat, "(local $t1"));
  CHECK(!contains(result.wat, "local.set $t0"));
  CHECK(!contains(result.wat, "local.get $t1"));
}

// --------------------------------------------------------------------------
// Imports, memory and globals
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "print is imported only when it is used") {
  const EmitResult without = emitFromCir(kAbsCir);
  CHECK(!contains(without.wat, "print_i32"));

  const EmitResult with = emitFromCir(R"(func @p() -> void {
entry:
  print.i32 3
  ret void
}
)");
  CHECK(with.ok());
  CHECK(contains(with.wat, "(import \"env\" \"print_i32\" (func $print_i32 (param i32)))"));
  CHECK(contains(with.wat, "call $print_i32"));
}

MTIR_TEST("wasm", "memory and the shadow stack pointer appear only with memory ops") {
  const EmitResult without = emitFromCir(kAbsCir);
  CHECK(!contains(without.wat, "(memory"));
  CHECK(!contains(without.wat, "$__sp"));

  const EmitResult with = emitFromCir(kFrameCir);
  CHECK(with.ok());
  CHECK(contains(with.wat, "(memory 1)"));
  CHECK(contains(with.wat, "(global $__sp (mut i32) (i32.const 65536))"));
}

MTIR_TEST("wasm", "a CIR global becomes a mutable wasm global") {
  const EmitResult result = emitFromCir(R"(global @counter : i32 = 7
func @g() -> i32 {
entry:
  %v = load i32 @counter
  store i32 8, @counter
  ret i32 %v
}
)");
  CHECK(result.ok());
  CHECK(contains(result.wat, "(global $counter (mut i32) (i32.const 7))"));
  // A wasm global is a value, not an address: it is read and written with
  // global.get/global.set rather than a load or store through a pointer.
  CHECK(contains(result.wat, "global.get $counter"));
  CHECK(contains(result.wat, "global.set $counter"));
  CHECK(!contains(result.wat, "i32.store"));
}

// --------------------------------------------------------------------------
// The shadow stack
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "a function without allocas has no frame") {
  const EmitResult result = emitFromCir(kAbsCir);
  CHECK(!contains(result.wat, "$__fp"));
  CHECK(!contains(result.wat, "$__frame"));
}

MTIR_TEST("wasm", "a frame is opened in the prologue and restored before return") {
  const EmitResult result = emitFromCir(kFrameCir);
  CHECK(result.ok());
  CHECK(contains(result.wat, "(local $__fp i32)"));
  CHECK(contains(result.wat, "(local $__frame i32)"));

  // Prologue: save the caller's stack pointer, drop the frame below it.
  CHECK(contains(result.wat, "global.get $__sp"));
  CHECK(contains(result.wat, "local.set $__fp"));
  CHECK(contains(result.wat, "local.set $__frame"));
  CHECK(contains(result.wat, "global.set $__sp"));

  // Epilogue: restore, immediately before the return.
  const std::size_t restore = result.wat.find("local.get $__fp\n");
  CHECK(restore != std::string::npos);
  const std::size_t ret = result.wat.find("return", restore);
  CHECK(ret != std::string::npos);
  CHECK(contains(result.wat.substr(restore, ret - restore), "global.set $__sp"));
}

MTIR_TEST("wasm", "the frame is large enough for every slot and stays aligned") {
  // One i32 scalar plus a four-element i32 array is 20 bytes, rounded up to
  // 24 so that the next frame is still 8-byte aligned.
  const EmitResult result = emitFromCir(kFrameCir);
  CHECK(contains(result.wat, "i32.const 24"));
}

MTIR_TEST("wasm", "an alloca becomes a frame-relative address") {
  const EmitResult result = emitFromCir(kFrameCir);
  // %p is the first slot, %xs the second, four bytes along.
  CHECK(contains(result.wat, "local.get $__frame"));
  CHECK(contains(result.wat, "local.set $p"));
  CHECK(contains(result.wat, "local.set $xs"));
  // No alloca should have survived into the lowering.
  CHECK_EQ(countOf(result.wat, "i32.alloca"), static_cast<std::size_t>(0));
}

MTIR_TEST("wasm", "a store pushes the address before the value in the output") {
  const EmitResult result = emitFromCir(kFrameCir);
  const std::vector<std::string> text = lines(result.wat);
  bool found = false;
  for (std::size_t i = 0; i + 2 < text.size(); ++i) {
    if (contains(text[i], "local.get $p") && contains(text[i + 1], "i32.const 7") &&
        contains(text[i + 2], "i32.store"))
      found = true;
  }
  CHECK(found);
}

MTIR_TEST("wasm", "a gep scales its index in the emitted text") {
  const EmitResult result = emitFromCir(kFrameCir);
  const std::vector<std::string> text = lines(result.wat);
  bool found = false;
  for (std::size_t i = 0; i + 3 < text.size(); ++i) {
    if (contains(text[i], "i32.const 2") && contains(text[i + 1], "i32.const 4") &&
        contains(text[i + 2], "i32.mul") && contains(text[i + 3], "i32.add"))
      found = true;
  }
  CHECK(found);
}

// --------------------------------------------------------------------------
// Refusals -- the emitter reports rather than emitting wrong wasm
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "a block with no terminator is reported, not guessed at") {
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
  CHECK(mtir::test::hasCode(result.diagnostics, "WASM01"));
  CHECK(mtir::test::mentions(result.diagnostics, "terminator"));
}

MTIR_TEST("wasm", "indexing a global array is refused rather than mistranslated") {
  // A CIR global becomes a wasm global, which has no address, so a gep off
  // one cannot be expressed.  Emitting `global.get` as if it were a base
  // address would silently read the wrong memory.
  const EmitResult result = emitFromCir(R"(global @xs : i32[4]
func @g() -> i32 {
entry:
  %q = gep i32 @xs, 1
  %v = load i32 %q
  ret i32 %v
}
)");
  CHECK(!result.ok());
  CHECK(mtir::test::hasCode(result.diagnostics, "WASM01"));
  CHECK(mtir::test::mentions(result.diagnostics, "global"));
}

// --------------------------------------------------------------------------
// Whole-corpus smoke test
// --------------------------------------------------------------------------
MTIR_TEST("wasm", "every CIR corpus module lowers to balanced, non-empty wat") {
  const char *names[] = {"arith", "control_flow", "recursion",
                         "div_edge", "nesting", "shift_edge"};
  for (const char *name : names) {
    const std::string source =
        mtir::test::readFile(mtir::test::example(std::string("tests/cir/") + name + ".cir"));
    CHECK(!source.empty());

    cir::ParseResult parsed = cir::parseCir(source, name);
    CHECK(parsed.module.has_value());
    if (!parsed.module.has_value())
      continue;

    const EmitResult result = emitModule(*parsed.module);
    CHECK(result.ok());
    CHECK(!result.wat.empty());

    bool negative = false;
    CHECK_EQ(parenBalance(result.wat, negative), 0);
    CHECK(!negative);

    // Nothing may reach the output as an un-lowered CIR mnemonic.
    CHECK(!contains(result.wat, "i32.alloca"));
    CHECK(!contains(result.wat, "i32.gep"));
    CHECK(!contains(result.wat, "i32.br.cond"));
  }
}
