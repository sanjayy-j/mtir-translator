"""Execute one CIR module on each target.

Module M8b.  Owner: Member 4.

Every runner returns the same triple as the reference interpreter,
``(return value of main, stdout, trapped)``, or raises ``Unavailable`` when
the tool it needs is not installed.  stdout is the program's print_int /
print_float lines joined by newlines, with no trailing newline.
"""
from __future__ import annotations

import os
import re
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
#
# Windows differs in three ways.  The executable needs a .exe suffix.  The
# Microsoft C runtime defines printf inline in <stdio.h>, so IR that calls it
# directly has to link legacy_stdio_definitions.lib.  And llvm.trap (ud2 on
# x64, brk on ARM64) ends the process with an NTSTATUS exception code instead
# of a signal.
_WINDOWS = os.name == "nt"
_WINDOWS_TRAPS = {0xC000001D,   # STATUS_ILLEGAL_INSTRUCTION
                  0x80000003}   # STATUS_BREAKPOINT
# clang on Windows reports a missing MSVC linker like this; that is a tool
# that isn't installed, not IR the back end got wrong.
_NO_LINKER = ("unable to find a Visual Studio installation",
              "unable to execute command")


def _clang():
    for name in ("clang", "/opt/homebrew/opt/llvm/bin/clang",
                 r"C:\Program Files\LLVM\bin\clang.exe"):
        path = shutil.which(name)
        if path: return path
    return None


def _exit_status(returncode: int, windows: bool = _WINDOWS):
    """``(return value, trapped)`` from a native process's exit status.

    On POSIX a trap is a signal (a negative returncode) and the status is 8
    bits.  On Windows the status is main's full 32-bit return value, so only
    the specific exception codes count as a trap.
    """
    if returncode < 0 or (windows and returncode in _WINDOWS_TRAPS):
        return 0, True
    code = returncode & 0xFF          # sign-extend so small negatives survive
    return (code - 256 if code > 127 else code), False


def _native_stdout(stdout: str) -> str:
    """The Microsoft C runtime prints a NaN as `-nan(ind)` or `nan(snan)`;
    reduce that to the `nan` / `-nan` every other target prints."""
    return re.sub(r"nan\([a-z]*\)", "nan", stdout.rstrip("\n"))


def run_llvm(module):
    from src.backend.llvm.emit_ll import emit
    clang = _clang()
    if clang is None:
        raise Unavailable("clang is not installed")
    with tempfile.TemporaryDirectory() as tmp:
        ll = Path(tmp) / "m.ll"
        exe = Path(tmp) / ("m.exe" if _WINDOWS else "m")
        ll.write_text(emit(module))
        cmd = [clang, "-O0", "-w", str(ll), "-o", str(exe)]
        if _WINDOWS:
            cmd.append("-llegacy_stdio_definitions")
        build = subprocess.run(cmd, capture_output=True, text=True)
        if build.returncode != 0:
            if _WINDOWS and any(s in build.stderr for s in _NO_LINKER):
                raise Unavailable("clang found no MSVC linker (install the "
                                  "Visual Studio C++ build tools)")
            raise RuntimeError(f"clang rejected the IR:\n{build.stderr}")
        proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    value, trapped = _exit_status(proc.returncode)
    return value, _native_stdout(proc.stdout), trapped


TARGETS = {"interp": run_interp, "llvm": run_llvm, "wasm": run_wasm, "vm": run_vm}
