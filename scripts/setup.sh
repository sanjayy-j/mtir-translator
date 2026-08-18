#!/usr/bin/env bash
# Environment setup and verification for the Multi-Target IR Translator.
# Owner: Member 2.
#
# This script is the authoritative definition of "a working environment".
# It runs as the first CI step, so a member's local machine and the CI
# environment cannot silently diverge.
#
#   bash scripts/setup.sh          # verify only
#   bash scripts/setup.sh --install-python-deps

set -u

FAIL=0
ok()   { printf '  \033[32mok\033[0m    %s\n' "$1"; }
warn() { printf '  \033[33mwarn\033[0m  %s\n' "$1"; }
bad()  { printf '  \033[31mMISSING\033[0m %s\n' "$1"; FAIL=1; }

echo "== Required =="
if command -v python3 >/dev/null 2>&1; then
  ok "python3 $(python3 --version 2>&1 | cut -d' ' -f2)"
  python3 - <<'PY' || FAIL=1
import sys
if sys.version_info < (3, 11):
    print("  Python 3.11+ is required (match statement in ast_nodes.dump)")
    raise SystemExit(1)
PY
else
  bad "python3 (3.11+)"
fi

command -v git >/dev/null 2>&1 && ok "git $(git --version | cut -d' ' -f3)" || bad "git"

echo
echo "== Back-end toolchain (needed from Week 6; not required for Review 1) =="
command -v llvm-as  >/dev/null 2>&1 && ok "llvm-as  $(llvm-as --version 2>/dev/null | head -1)"  || warn "llvm-as not found  (LLVM 17+, needed Week 6)"
command -v lli      >/dev/null 2>&1 && ok "lli"      || warn "lli not found      (LLVM 17+, needed Week 7)"
command -v wat2wasm >/dev/null 2>&1 && ok "wat2wasm $(wat2wasm --version 2>/dev/null)"           || warn "wat2wasm not found (WABT 1.0.34+, needed Week 7)"
command -v wasmtime >/dev/null 2>&1 && ok "wasmtime $(wasmtime --version 2>/dev/null)"           || warn "wasmtime not found (18+, needed Week 8)"
command -v node     >/dev/null 2>&1 && ok "node $(node --version)"                               || warn "node not found      (20 LTS, needed Week 8)"

if [ "${1:-}" = "--install-python-deps" ]; then
  echo
  echo "== Installing Python dev dependencies =="
  python3 -m pip install -r requirements.txt
fi

echo
echo "== Smoke test =="
python3 -m src.driver --emit=tokens docs/examples/abs.mini >/dev/null 2>&1 \
  && ok "lexer runs" || { bad "lexer smoke test"; }
python3 -m src.driver --emit=ast docs/examples/abs.mini >/dev/null 2>&1 \
  && ok "parser runs" || { bad "parser smoke test"; }
if python3 -m src.driver --demo-cir 2>/dev/null | diff -q - docs/examples/abs.cir >/dev/null; then
  ok "CIR printer output matches docs/examples/abs.cir"
else
  bad "CIR printer golden-file check"
fi

# Validate the hand-written target files, when the toolchain is present.
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
  echo "Environment OK. Run: python3 -m pytest -q"
else
  echo "Environment INCOMPLETE — see MISSING lines above."
fi
exit "$FAIL"
