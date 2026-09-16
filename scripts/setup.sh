#!/usr/bin/env bash
# Environment setup and verification for the Multi-Target IR Translator.
# Owner: Member 2.
#
# This script is the authoritative definition of "a working environment".
# It runs as a CI step, so a member's local machine and the CI environment
# cannot silently diverge.
#
#   bash scripts/setup.sh          # verify only
#   bash scripts/setup.sh --build  # verify, then configure and build
#
# The project is C++17 and nothing else.  Everything below is either the C++
# toolchain, which is required, or a back-end validation tool, which is not:
# without those tools the build and the tests still run, and the checks that
# would have used them report SKIPPED rather than passing.

set -u

FAIL=0
ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; }
warn() { printf '  \033[33mwarn\033[0m  %s\n' "$1"; }
bad()  { printf '  \033[31mMISSING\033[0m %s\n' "$1"; FAIL=1; }

echo "== Required: the C++17 toolchain =="
if command -v cmake >/dev/null 2>&1; then
  ok "cmake $(cmake --version | head -1 | cut -d' ' -f3)"
else
  bad "cmake (3.16+)"
fi

CXX_FOUND=0
for compiler in "${CXX:-}" g++ clang++ c++; do
  [ -z "$compiler" ] && continue
  if command -v "$compiler" >/dev/null 2>&1; then
    ok "$compiler $("$compiler" --version 2>/dev/null | head -1)"
    CXX_FOUND=1
    break
  fi
done
if [ "$CXX_FOUND" -eq 0 ]; then
  if command -v cl >/dev/null 2>&1; then
    ok "cl (MSVC)"
  else
    bad "a C++17 compiler (g++ 8+, clang++ 7+, or MSVC 2019+)"
  fi
fi

command -v git >/dev/null 2>&1 && ok "git $(git --version | cut -d' ' -f3)" || bad "git"

echo
echo "== Optional: back-end validation tools =="
command -v llvm-as  >/dev/null 2>&1 && ok "llvm-as  $(llvm-as --version 2>/dev/null | head -1)" || warn "llvm-as not found  -- the LLVM validation test will report SKIPPED"
command -v lli      >/dev/null 2>&1 && ok "lli"                                                  || warn "lli not found      (needed to execute generated IR)"
command -v wat2wasm >/dev/null 2>&1 && ok "wat2wasm $(wat2wasm --version 2>/dev/null)"           || warn "wat2wasm not found (WABT, needed by the Wasm back end)"
command -v wasmtime >/dev/null 2>&1 && ok "wasmtime $(wasmtime --version 2>/dev/null)"           || warn "wasmtime not found (needed to execute generated Wasm)"
echo
echo "  Note: the stack bytecode target needs none of these.  It is built into"
echo "  mtirc, so 'mtirc --run <file>' works on any machine that can build the"
echo "  project -- which is why it is the one target whose behaviour the test"
echo "  suite checks by execution rather than by inspection."

echo
echo "== Smoke test =="
BUILD_DIR="${BUILD_DIR:-build}"
if [ "${1:-}" = "--build" ]; then
  cmake -S . -B "$BUILD_DIR" >/dev/null && cmake --build "$BUILD_DIR" >/dev/null \
    && ok "configured and built into $BUILD_DIR" || bad "cmake build"
fi

MTIRC=""
for candidate in "$BUILD_DIR/mtirc" "$BUILD_DIR/mtirc.exe" "$BUILD_DIR/Debug/mtirc.exe" "$BUILD_DIR/Release/mtirc.exe"; do
  [ -x "$candidate" ] && MTIRC="$candidate" && break
done

if [ -n "$MTIRC" ]; then
  if "$MTIRC" --emit=cir docs/examples/abs.cir 2>/dev/null | diff -q - docs/examples/abs.cir >/dev/null; then
    ok "mtirc --emit=cir round-trips docs/examples/abs.cir"
  else
    bad "CIR round-trip golden check"
  fi
  if "$MTIRC" --emit=ll docs/examples/abs.cir 2>/dev/null | diff -q - docs/examples/abs.gen.ll >/dev/null; then
    ok "mtirc --emit=ll matches docs/examples/abs.gen.ll"
  else
    bad "LLVM golden check"
  fi
  if command -v llvm-as >/dev/null 2>&1; then
    "$MTIRC" --emit=ll docs/examples/abs.cir -o /tmp/mtir-smoke.ll 2>/dev/null \
      && llvm-as /tmp/mtir-smoke.ll -o /tmp/mtir-smoke.bc 2>/dev/null \
      && ok "llvm-as accepts the generated IR" || bad "llvm-as rejected the generated IR"
  fi
else
  warn "mtirc not built yet -- run 'bash scripts/setup.sh --build' or 'cmake -S . -B build && cmake --build build'"
fi

# The hand-written worked examples, when the toolchain is present.
if command -v llvm-as >/dev/null 2>&1; then
  llvm-as docs/examples/abs.ll -o /tmp/abs.bc 2>/dev/null \
    && ok "docs/examples/abs.ll accepted by llvm-as" || bad "abs.ll failed llvm-as"
fi
if command -v wat2wasm >/dev/null 2>&1; then
  wat2wasm docs/examples/abs.wat -o /tmp/abs.wasm 2>/dev/null \
    && ok "docs/examples/abs.wat accepted by wat2wasm" || bad "abs.wat failed wat2wasm"
fi

echo
if [ "$FAIL" -eq 0 ]; then
  echo "Environment OK. Run: ctest --test-dir $BUILD_DIR --output-on-failure"
else
  echo "Environment INCOMPLETE — see MISSING lines above."
fi
exit "$FAIL"
