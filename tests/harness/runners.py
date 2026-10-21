"""Execute one CIR module on each target.

Module M8b.  Owner: Member 4.

Every runner returns the same triple as the reference interpreter,
``(return value of main, stdout, trapped)``, or raises ``Unavailable`` when
the tool it needs is not installed.  stdout is the program's print_int /
print_float lines joined by newlines, with no trailing newline.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from src.cir.interp import fmt_float, run as run_interp_module


class Unavailable(Exception):
    """The toolchain for this target is not installed."""


def run_interp(module):
    return run_interp_module(module)


# -- WebAssembly ------------------------------------------------------------
def run_wasm(module, structured=True):
    try:
        import wasmtime
    except ImportError as e:
        raise Unavailable("pip install wasmtime") from e
    from src.backend.wasm.emit_wat import emit

    out = []
    engine = wasmtime.Engine()
    store = wasmtime.Store(engine)
    wasm_mod = wasmtime.Module(engine, wasmtime.wat2wasm(emit(module, structured)))
    linker = wasmtime.Linker(engine)
    i32, f64 = wasmtime.ValType.i32(), wasmtime.ValType.f64()
    linker.define_func("env", "print_int", wasmtime.FuncType([i32], []),
                       lambda v: out.append(str(v)))
    linker.define_func("env", "print_float", wasmtime.FuncType([f64], []),
                       lambda v: out.append(fmt_float(v)))
    instance = linker.instantiate(store, wasm_mod)
    main = instance.exports(store)["main"]
    try:
        value = main(store)
    except wasmtime.Trap:
        return 0, "\n".join(out), True
    return (0 if value is None else value), "\n".join(out), False


# -- bytecode VM ------------------------------------------------------------
def run_vm(module):
    from src.backend.stackvm.emit_sbc import emit
    from src.backend.stackvm.vm import run
    return run(emit(module))


# -- LLVM -------------------------------------------------------------------
# lli is not on every machine, but any clang compiles textual IR.  printf
# comes from libc; llvm.trap kills the process with SIGILL/SIGTRAP.
def _clang():
    for name in ("clang", "/opt/homebrew/opt/llvm/bin/clang"):
        path = shutil.which(name)
        if path: return path
    return None


def run_llvm(module):
    from src.backend.llvm.emit_ll import emit
    clang = _clang()
    if clang is None:
        raise Unavailable("clang is not installed")
    with tempfile.TemporaryDirectory() as tmp:
        ll, exe = Path(tmp) / "m.ll", Path(tmp) / "m"
        ll.write_text(emit(module))
        build = subprocess.run([clang, "-O0", "-w", str(ll), "-o", str(exe)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise RuntimeError(f"clang rejected the IR:\n{build.stderr}")
        proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    stdout = proc.stdout.rstrip("\n")
    if proc.returncode < 0:                       # killed by a signal: a trap
        return 0, stdout, True
    # A process exit status is 8 bits; sign-extend so small negatives survive.
    code = proc.returncode
    return (code - 256 if code > 127 else code), stdout, False


TARGETS = {"interp": run_interp, "llvm": run_llvm, "wasm": run_wasm, "vm": run_vm}
